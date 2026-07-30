#!/usr/bin/env python3
"""Exact CPU-vs-accelerator oracle for mc-sim.

The fidelity contract (SPEC.md) is internal consistency: the CPU scalar path and the CUDA batch
path are the SAME __host__ __device__ source compiled two ways, and must produce BITWISE-identical
output on the same seed. The native Metal subset uses audited MSL mirrors of the selected leaf
kernels and is held to the same exact stdout contract. This runner builds the requested backends,
runs them on identical args, and reports the first differing line.

  uv run --no-project python oracle/runner.py smoke [seed] [n]
  uv run --no-project python oracle/runner.py --cpu-only cuda_batch_worldgen   # fast dev loop
  uv run --no-project python oracle/runner.py --backends cpu,metal smoke

Float discipline: CPU built with -ffp-contract=off, CUDA with --fmad=false (SPEC rule 4), so the
two agree to the last bit. CUDA arch defaults to sm_120 (this anvil); override with MC_SM.
Use --cpu-only (or MC_CPU_ONLY=1) to skip CUDA during iteration; run full oracle before commit.
"""
import os
import platform
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SM = os.environ.get("MC_SM", "sm_120")
NVCC = ["nvcc", f"-arch={SM}", "-O3", "--fmad=false"]
# Heavy worldgen device functions are MC_NOINLINE (see core/mc.h), so every kernel now compiles
# standalone in seconds - no cached-object / link workaround needed.


def build_cpu(name, tmp):
    out = os.path.join(tmp, name + "_cpu")
    subprocess.run(["cc", "-O2", "-ffp-contract=off", "-o", out,
                    os.path.join(ROOT, "cpu", name + ".c"), "-lm"], check=True)
    return out


def build_cuda(name, tmp):
    out = os.path.join(tmp, name + "_cuda")
    subprocess.run([*NVCC, "-o", out,
                    os.path.join(ROOT, "cuda", name + ".cu")], check=True)
    return out


def build_metal(name, tmp):
    """Build the runtime-MSL oracle; name is selected as its first argument."""
    if name not in ("smoke", "obs_camera"):
        raise SystemExit(f"Metal backend does not implement kernel: {name}")
    out = os.path.join(tmp, "mcsim_metal_oracle")
    subprocess.run([
        "xcrun", "clang++", "-std=gnu++17", "-O2", "-ffp-contract=off",
        "-fobjc-arc", "-I" + os.path.join(ROOT, "core"),
        "-I" + os.path.join(ROOT, "metal"), "-o", out,
        os.path.join(ROOT, "metal", "mcsim_metal.mm"),
        os.path.join(ROOT, "metal", "oracle_main.mm"),
        "-framework", "Metal", "-framework", "Foundation",
    ], check=True)
    return [out, name]


def build_golden(name, tmp):
    """Verbatim-Java golden (vanilla ground truth) at oracle/goldens/<name>/Golden.java, if present."""
    src = os.path.join(ROOT, "oracle", "goldens", name, "Golden.java")
    if not os.path.exists(src):
        return None
    # Keep the regular Linux/CUDA oracle strict: a present golden still needs
    # a working JDK unless the native macOS sweep explicitly records that the
    # external Java prerequisite is unavailable.  `which javac` is not enough
    # on macOS because /usr/bin/javac may only be Apple's no-runtime launcher.
    skip_java = os.environ.get("MC_SKIP_JAVA_GOLDENS", "").lower()
    if skip_java not in ("", "0", "false", "no"):
        return None
    subprocess.run(["javac", "-d", tmp, src], check=True)
    return ["java", "-cp", tmp, "Golden"]


def run(cmd, args):
    cmd = cmd if isinstance(cmd, list) else [cmd]
    p = subprocess.run([*cmd, *args], capture_output=True, text=True)
    if p.returncode != 0:
        if p.stdout:
            print(p.stdout, end="", file=sys.stderr)
        if p.stderr:
            print(p.stderr, end="", file=sys.stderr)
        raise SystemExit(f"command failed ({p.returncode}): {' '.join([*cmd, *args])}")
    return p.stdout.splitlines()


def line_context(kernel, line):
    if kernel != "obs_camera":
        return ""
    pixels = 64 * 36
    poses = 6
    per_seed = 1 + poses * pixels * 3
    seed_index, within_seed = divmod(line, per_seed)
    if within_seed == 0:
        return f" (seed_index={seed_index} header)"
    pose, within_pose = divmod(within_seed - 1, pixels * 3)
    channel_index, pixel = divmod(within_pose, pixels)
    channel = ("id", "depth", "edge")[channel_index]
    y, x = divmod(pixel, 64)
    return (f" (seed_index={seed_index} pose={pose} channel={channel} "
            f"pixel={pixel} x={x} y={y})")


def diff(label_a, a, label_b, b, kernel=None):
    if len(a) != len(b):
        print(f"FAIL  {label_a}=={label_b}: line count {len(a)} vs {len(b)}")
        sys.exit(1)
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            where = line_context(kernel, i)
            print(f"FAIL  {label_a}=={label_b}: line {i}{where}: "
                  f"{label_a}={x!r} {label_b}={y!r}")
            sys.exit(1)
    print(f"  ok  {label_a} == {label_b}  ({len(a)} lines)")


def parse_args(argv):
    cpu_only = os.environ.get("MC_CPU_ONLY", "") not in ("", "0", "false")
    backends = None
    rest = list(argv)
    while rest and rest[0].startswith("-"):
        if rest[0] == "--cpu-only":
            cpu_only = True
            rest = rest[1:]
        elif rest[0] == "--backends":
            if len(rest) < 2:
                raise SystemExit("--backends requires a comma-separated value")
            backends = rest[1].split(",")
            rest = rest[2:]
        elif rest[0].startswith("--backends="):
            backends = rest[0].split("=", 1)[1].split(",")
            rest = rest[1:]
        else:
            raise SystemExit(f"unknown flag: {rest[0]}")
    if len(rest) < 1:
        raise SystemExit(
            "usage: runner.py [--cpu-only|--backends cpu,cuda,metal] <name> [args...]"
        )
    if cpu_only:
        if backends is not None and backends != ["cpu"]:
            raise SystemExit("--cpu-only conflicts with non-CPU --backends")
        backends = ["cpu"]
    elif backends is None:
        backends = ["cpu", "cuda"]
    valid = {"cpu", "cuda", "metal"}
    if not backends or any(x not in valid for x in backends):
        raise SystemExit("--backends must contain cpu, cuda, and/or metal")
    if "cpu" not in backends:
        raise SystemExit("the exact oracle requires the cpu reference backend")
    return backends, rest[0], rest[1:]


def run_py_gym_env_smoke(args, cpu_only=False):
    """CPU==CUDA + determinism replay + optional pybind11 schema oracle."""
    name = "py_gym_env_smoke"
    have_cuda = (not cpu_only
                 and subprocess.run(["which", "nvcc"], capture_output=True).returncode == 0)
    with tempfile.TemporaryDirectory() as tmp:
        cpu_bin = build_cpu(name, tmp)
        cpu_out = run(cpu_bin, args)
        cpu_out2 = run(cpu_bin, args)
        cuda_out = run(build_cuda(name, tmp), args) if have_cuda else None

    skip = "  [cpu-only]" if cpu_only else ""
    print(f"== {name} (arch {SM}) ==  {len(cpu_out)} lines"
          f"{skip}"
          f"{'' if cuda_out else '  [nvcc skipped]' if cpu_only else '  [nvcc missing, CPU only]'}")
    diff("cpu_run1", cpu_out, "cpu_run2", cpu_out2)
    if cuda_out is not None:
        diff("cpu", cpu_out, "cuda", cuda_out)

    oracle = os.path.join(ROOT, "oracle", "py_gym_env_smoke.py")
    subprocess.run(
        [sys.executable, oracle, *args],
        check=True,
        cwd=ROOT,
    )
    print("PASS")


def run_metal_smoke_matrix(cpu_bin, metal_cmd):
    """RNG seeds, zero/edge counts, non-multiple tails, and uint64 limits."""
    seeds = ["0", "7", "42", "12345", "18446744073709551615"]
    counts = ["0", "1", "31", "32", "33", "257"]
    print(f"== smoke CPU==Metal matrix ({len(seeds) * len(counts)} cases) ==")
    for seed in seeds:
        for count in counts:
            args = [seed, count]
            cpu_out = run(cpu_bin, args)
            metal_out = run(metal_cmd, args)
            diff(f"cpu[{seed},{count}]", cpu_out,
                 f"metal[{seed},{count}]", metal_out)
    print("PASS")


def main():
    backends, name, args = parse_args(sys.argv[1:])
    cpu_only = backends == ["cpu"]
    if name == "py_gym_env_smoke":
        if "metal" in backends:
            raise SystemExit("Metal backend does not implement py_gym_env_smoke")
        run_py_gym_env_smoke(args, cpu_only=cpu_only)
        return
    have_cuda = ("cuda" in backends and shutil.which("nvcc") is not None)
    want_metal = "metal" in backends
    if want_metal and platform.system() != "Darwin":
        raise SystemExit("Metal backend requires macOS")
    if want_metal and shutil.which("xcrun") is None:
        raise SystemExit("Metal backend requires xcrun and the macOS SDK")

    with tempfile.TemporaryDirectory() as tmp:
        cpu_bin = build_cpu(name, tmp)
        metal_cmd = build_metal(name, tmp) if want_metal else None
        if want_metal and name == "smoke" and not args:
            run_metal_smoke_matrix(cpu_bin, metal_cmd)
            return

        cpu_out = run(cpu_bin, args)
        gcmd = build_golden(name, tmp)
        golden_out = run(gcmd, args) if gcmd else None
        cuda_out = run(build_cuda(name, tmp), args) if have_cuda else None
        metal_out = run(metal_cmd, args) if metal_cmd else None

    skip_note = "  [cpu-only]" if cpu_only else ""
    if cuda_out is not None:
        nvcc_note = ""
    elif "cuda" not in backends:
        nvcc_note = ""
    else:
        nvcc_note = "  [nvcc missing]"
    print(f"== {name} (arch {SM}) ==  {len(cpu_out)} lines"
          f"{'' if golden_out else '  [no Java golden]'}"
          f"{skip_note}{nvcc_note}")
    # Vanilla faithfulness (worldgen only): CPU must match the verbatim-Java golden.
    if golden_out is not None:
        diff("java", golden_out, "cpu", cpu_out)
    # Internal consistency (always): CPU must match CUDA bitwise.
    if cuda_out is not None:
        diff("cpu", cpu_out, "cuda", cuda_out)
    if metal_out is not None:
        diff("cpu", cpu_out, "metal", metal_out, kernel=name)
    print("PASS")


if __name__ == "__main__":
    main()
