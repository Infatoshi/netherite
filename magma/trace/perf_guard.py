#!/usr/bin/env python3
"""Machine-local performance regression gate for Netherite parity work.

The baseline is deliberately separate from the global ship pins. It captures
this checkout on this machine before the full-parity expansion and rejects a
median slowdown larger than each metric's configured fraction.

Examples:
    uv run --no-project python trace/perf_guard.py --only cpu_sps
    uv run --no-project python trace/perf_guard.py --only all --gpu 1
"""

import argparse
import json
import os
import pathlib
import re
import statistics
import subprocess
import sys


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
REPO = HERE.parents[1]
BLAZE = REPO / "blaze"
DEFAULT_BASELINE = HERE / "perf_baseline_gpu1.json"
DEFAULT_OUT = HERE / "out" / "perf_guard_latest.json"


def run_checked(command, cwd, env=None):
    proc = subprocess.run(
        command,
        cwd=str(cwd),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"command failed ({proc.returncode}): {' '.join(command)}\n{proc.stdout}"
        )
    return proc.stdout


def require_binary(path, build_hint):
    if not path.is_file() or not os.access(path, os.X_OK):
        raise RuntimeError(f"missing executable {path}; {build_hint}")


def cpu_sps(spec):
    binary = BLAZE / "build" / "bin" / "cpu" / "sps_benchmark"
    require_binary(binary, "run `make -C blaze cpu-sps_benchmark`")
    samples = []
    hashes = set()
    for _ in range(int(spec["runs"])):
        output = run_checked([str(binary)], BLAZE)
        match = re.search(r"sps_benchmark cpu:.*\bsps=(\d+(?:\.\d+)?)", output)
        hash_match = re.search(r"(?m)^([0-9a-f]{16})$", output)
        if not match or not hash_match:
            raise RuntimeError(f"could not parse CPU SPS output:\n{output}")
        samples.append(float(match.group(1)))
        hashes.add(hash_match.group(1))
    if len(hashes) != 1:
        raise RuntimeError(f"CPU benchmark trajectory hash changed between runs: {hashes}")
    return samples, {"trajectory_hash": next(iter(hashes))}


def blaze_sps(spec, gpu):
    binary = MAGMA / "rl" / "blaze" / "blaze_cuda.so"
    require_binary(binary, "run `make -C magma blaze_cuda_so BLAZE_SM=sm_120`")
    env = os.environ.copy()
    env["CUDA_VISIBLE_DEVICES"] = str(gpu)
    samples = []
    for _ in range(int(spec["runs"])):
        command = [
            "uv", "run", "--no-project", "--with", "numpy", "--with", "torch",
            "python", "rl/blaze/verify_cuda.py",
            "--bench", "--t0",
            "--n", str(spec["envs"]),
            "--decisions", str(spec["decisions"]),
            "--repeat", str(spec["repeat"]),
            "--warmup", str(spec["warmup"]),
            "--device", "0",
        ]
        output = run_checked(command, MAGMA, env)
        match = re.search(r"N=\d+:\s+([0-9.]+)M env-ticks/s", output)
        if not match:
            raise RuntimeError(f"could not parse blaze benchmark output:\n{output}")
        samples.append(float(match.group(1)) * 1_000_000.0)
    return samples, {}


def magma_fps(spec, gpu):
    binary = MAGMA / "magma_game_cuda"
    require_binary(
        binary,
        "run `make -C magma game-cuda NVFLAGS_GAME='-O2 --fmad=false "
        "-arch=sm_120 -Icore -I.'`",
    )
    env = os.environ.copy()
    env.update({
        "CUDA_VISIBLE_DEVICES": str(gpu),
        "SDL_VIDEODRIVER": "dummy",
        "MAGMA_BENCH": "1",
        "MAGMA_BENCH_WARMUP": str(spec["warmup"]),
    })
    samples = []
    frame_ms = []
    command = [
        str(binary),
        "--backend", "cuda",
        "--world", "default",
        "--view-distance", str(spec["view_distance"]),
        "--width", str(spec["width"]),
        "--height", str(spec["height"]),
        "--frames", str(spec["frames"]),
    ]
    for _ in range(int(spec["runs"])):
        output = run_checked(command, MAGMA, env)
        fps_match = re.search(r"\[bench\] fps: mean ([0-9.]+)", output)
        ms_match = re.search(r"\[bench\] frame ms: mean ([0-9.]+)", output)
        if not fps_match or not ms_match:
            raise RuntimeError(f"could not parse magma benchmark output:\n{output}")
        samples.append(float(fps_match.group(1)))
        frame_ms.append(float(ms_match.group(1)))
    return samples, {"mean_frame_ms_samples": frame_ms}


def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", default=str(DEFAULT_BASELINE))
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--gpu", type=int, default=1)
    ap.add_argument(
        "--only",
        default="all",
        choices=("all", "cpu_sps", "blaze_t0_gpu_sps", "magma_1080p_cuda_fps"),
    )
    return ap.parse_args()


def main():
    args = parse_args()
    baseline_path = pathlib.Path(args.baseline)
    baseline = json.loads(baseline_path.read_text())
    specs = baseline["metrics"]
    selected = list(specs) if args.only == "all" else [args.only]
    runners = {
        "cpu_sps": lambda spec: cpu_sps(spec),
        "blaze_t0_gpu_sps": lambda spec: blaze_sps(spec, args.gpu),
        "magma_1080p_cuda_fps": lambda spec: magma_fps(spec, args.gpu),
    }

    results = {}
    failed = False
    for name in selected:
        spec = specs[name]
        samples, extra = runners[name](spec)
        median = statistics.median(samples)
        floor = float(spec["baseline"]) * (1.0 - float(spec["max_regression_fraction"]))
        passed = median >= floor
        failed |= not passed
        results[name] = {
            "baseline": spec["baseline"],
            "floor": floor,
            "median": median,
            "passed": passed,
            "samples": samples,
            **extra,
        }
        verdict = "PASS" if passed else "FAIL"
        print(
            f"{name}: {verdict} median={median:.6g} "
            f"baseline={spec['baseline']:.6g} floor={floor:.6g}"
        )

    payload = {
        "schema": 1,
        "baseline": str(baseline_path.resolve()),
        "gpu": args.gpu,
        "results": results,
    }
    out_path = pathlib.Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(f"results: {out_path}")
    return 4 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
