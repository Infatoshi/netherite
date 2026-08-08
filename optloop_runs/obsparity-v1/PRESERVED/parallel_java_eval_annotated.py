#!/usr/bin/env python3
"""Parallel, resumable Java side of the pinned paper sim-to-real sweep.

This is deliberately outside the git worktree.  It orchestrates an unmodified
d55 checkout, starts one Minecraft client per isolated run directory/display/
port, evaluates one seed at a time, and merges the one-seed artifacts back into
the existing netherite.sim2real.v1 schema.

The default action is a non-mutating plan.  Use --run only after inspecting it.

OBS-PARITY AUDIT ANNOTATIONS (2026-08-02, comments only):

* This driver pins source provenance and action-result metadata, but does not
  compare observations or task state against Blaze.
* evaluate_seed sets FRAME_EVERY=0.  The archived pilot therefore contains no
  policy frame or scalar samples from which live Java distributions can be
  reconstructed.
* qrl_chain_demo sends four socket steps per decision and verifies their
  client-side sequence/FNV receipt.  That proves delivery to applyAction(),
  not that the independently overclocked integrated server advanced one tick
  per step.
* make_launch_config requests frozen time/weather gamerules.  Recorder applies
  that launch configuration once per client process, while this driver creates
  a fresh world for every attempt.  The archive records 16 fresh launches but
  only one "launch settings applied" event.
* merge_fragments and pair_sim2real validate provenance/result schema.  They do
  not gate camera/scalar values, spawn pose or local terrain, action semantics,
  gamerules, or client/server tick counts.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import contextlib
import hashlib
import importlib.util
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

PIN = "d55c7f01c1139299be3f7fa0b98ef11b82c3b473"
DEFAULT_ROOT = Path("/home/infatoshi/dev/nw/paper-t3")
DEFAULT_STATE = Path("/home/infatoshi/dev/nw/.java-eval-d55")
DEFAULT_SEEDS = (2, 3, 10, 11, 14, 16, 20, 27, 29, 32, 33, 44, 46)
MERGE_FIELDS = (
    "schema",
    "environment",
    "commit",
    "tracked_clean",
    "checkpoint",
    "checkpoint_sha256",
    "tries",
    "ep_ticks",
    "repeat",
    "sampling",
    "rng_protocol",
    "success_source",
)


def run_checked(argv: list[str], *, cwd: Path, env: dict[str, str] | None = None) -> str:
    return subprocess.check_output(argv, cwd=cwd, env=env, text=True).strip()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_pin(root: Path, checkpoint: Path, expected_checkpoint_sha256: str | None) -> str:
    commit = run_checked(["git", "rev-parse", "HEAD"], cwd=root)
    if commit != PIN:
        raise RuntimeError(f"measurement checkout is {commit}, expected {PIN}")
    if subprocess.run(
        ["git", "diff-index", "--quiet", "HEAD", "--"], cwd=root, check=False
    ).returncode:
        raise RuntimeError("measurement checkout has tracked modifications")
    checkpoint_sha256 = sha256_file(checkpoint)
    if expected_checkpoint_sha256 and checkpoint_sha256 != expected_checkpoint_sha256:
        raise RuntimeError(
            "checkpoint digest mismatch: "
            f"{checkpoint_sha256} != {expected_checkpoint_sha256}"
        )
    return checkpoint_sha256


def merge_fragments(paths: list[Path], seeds: tuple[int, ...]) -> dict:
    by_seed: dict[int, dict] = {}
    for path in paths:
        with path.open() as stream:
            artifact = json.load(stream)
        fragment_seeds = artifact.get("seeds")
        if not isinstance(fragment_seeds, list) or len(fragment_seeds) != 1:
            raise ValueError(f"{path}: expected exactly one seed")
        seed = int(fragment_seeds[0])
        if seed not in seeds:
            raise ValueError(f"{path}: undeclared seed {seed}")
        if seed in by_seed:
            raise ValueError(f"duplicate fragment for seed {seed}")
        by_seed[seed] = artifact
    missing = [seed for seed in seeds if seed not in by_seed]
    if missing:
        raise ValueError(f"missing seed fragments: {missing}")

    first = by_seed[seeds[0]]
    for seed in seeds[1:]:
        artifact = by_seed[seed]
        for field in MERGE_FIELDS:
            if artifact.get(field) != first.get(field):
                raise ValueError(
                    f"seed {seed}: {field} differs: "
                    f"{artifact.get(field)!r} != {first.get(field)!r}"
                )

    attempts: list[dict] = []
    per_seed_reached: dict[str, int] = {}
    for seed in seeds:
        artifact = by_seed[seed]
        seed_attempts = artifact.get("attempts", [])
        if any(int(attempt.get("seed", -1)) != seed for attempt in seed_attempts):
            raise ValueError(f"seed {seed}: fragment contains a foreign attempt")
        ids = [int(attempt.get("attempt", -1)) for attempt in seed_attempts]
        if ids != list(range(len(ids))):
            raise ValueError(f"seed {seed}: attempts are not contiguous from zero")
        attempts.extend(seed_attempts)
        per_seed_reached[str(seed)] = int(artifact["per_seed_reached"][str(seed)])

    merged = {field: first[field] for field in MERGE_FIELDS}
    merged.update(
        {
            "seeds": list(seeds),
            "attempts": attempts,
            "per_seed_reached": per_seed_reached,
        }
    )
    return merged


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")
    temporary.replace(path)


def port_available(port: int) -> bool:
    with socket.socket() as sock:
        try:
            sock.bind(("127.0.0.1", port))
        except OSError:
            return False
    return True


def wait_for_port(port: int, process: subprocess.Popen, timeout: float = 240.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Minecraft for port {port} exited rc={process.returncode}")
        with socket.socket() as sock:
            sock.settimeout(0.25)
            if sock.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.5)
    raise TimeoutError(f"Minecraft did not bind port {port} within {timeout}s")


def make_launch_config(root: Path, run_dir: Path, port: int) -> None:
    # AUDIT: the intended world settings are written correctly here.  The
    # one-shot application bug is downstream in Recorder's launchApplied flag.
    # Use the pinned launcher's own config functions without touching its shared RUN.
    sys.path.insert(0, str(root / "java"))
    import mc_cli  # pylint: disable=import-error,import-outside-toplevel
    import yaml  # pylint: disable=import-error,import-outside-toplevel

    with (root / "java" / "fast.yaml").open() as stream:
        cfg = yaml.safe_load(stream)
    run_dir.mkdir(parents=True, exist_ok=True)
    options = mc_cli.options_patch(cfg.get("video", {}), cfg.get("ui", {}))
    with (run_dir / "options.txt").open("w") as stream:
        for key, value in options.items():
            stream.write(f"{key}:{value}\n")
    launch = {
        "port": port,
        "profile": "fast",
        "chat": bool(cfg.get("ui", {}).get("chat", True)),
        "hide_gui": bool(cfg.get("ui", {}).get("hide_gui", False)),
        "strip": {key: bool(value) for key, value in cfg.get("strip", {}).items()},
        "determinism": {
            key: bool(value) for key, value in cfg.get("determinism", {}).items()
        },
        "world": cfg.get("world", {}),
    }
    gamerules = launch["world"].get("gamerules", {})
    launch["world"]["gamerules"] = {
        key: str(value).lower() for key, value in gamerules.items()
    }
    write_json(run_dir / "qrl_launch.json", launch)


def client_environment(run_dir: Path, port: int, display: int, worker: int) -> dict[str, str]:
    env = os.environ.copy()
    java_home = Path("/usr/lib/jvm/java-8-openjdk-amd64")
    env.update(
        {
            "JAVA_HOME": str(java_home),
            "PATH": f"{java_home / 'bin'}:{env['PATH']}",
            "DISPLAY": f":{display}",
            "QRL_PORT": str(port),
            "QRL_LAUNCH_JSON": str(run_dir / "qrl_launch.json"),
            "MC_USERNAME": f"PaperEval{worker}",
            "LIBGL_ALWAYS_SOFTWARE": "1",
            "MESA_GL_VERSION_OVERRIDE": "2.1",
            "JAVA_TOOL_OPTIONS": (
                env.get("JAVA_TOOL_OPTIONS", "")
                + " -Djavax.accessibility.assistive_technologies="
            ).strip(),
        }
    )
    return env


def start_clients(root: Path, state: Path, workers: int, base_port: int, base_display: int):
    minecraft = root / "java" / "Minecraft"
    gradle = [
        "./gradlew",
        "-g",
        "run/gradle",
        "--offline",
        "-x",
        "getAssets",
    ]
    # Compile and create launch metadata once, before concurrent JavaExec tasks.
    subprocess.run(gradle + ["classes", "jar", "makeStart"], cwd=minecraft, check=True)

    clients = []
    for worker in range(workers):
        port = base_port + worker
        display = base_display + worker
        if not port_available(port):
            raise RuntimeError(f"port {port} is already in use")
        run_dir = state / f"run{worker}"
        make_launch_config(root, run_dir, port)
        env = client_environment(run_dir, port, display, worker)
        xvfb_log = (state / f"xvfb{worker}.log").open("ab")
        xvfb = subprocess.Popen(
            [
                "Xvfb",
                f":{display}",
                "-screen",
                "0",
                "1280x720x24",
                "+extension",
                "GLX",
                "+render",
                "-noreset",
            ],
            env=env,
            stdout=xvfb_log,
            stderr=xvfb_log,
            start_new_session=True,
        )
        time.sleep(0.5)
        if xvfb.poll() is not None:
            raise RuntimeError(f"Xvfb :{display} exited rc={xvfb.returncode}")
        client_log = (state / f"client{worker}.log").open("ab")
        command = gradle + [
            "--no-daemon",
            "-x",
            "makeStart",
            "-x",
            "classes",
            "-x",
            "jar",
            "runClient",
            "--stacktrace",
            f"-PrunDir={run_dir}",
        ]
        client = subprocess.Popen(
            command,
            cwd=minecraft,
            env=env,
            stdout=client_log,
            stderr=client_log,
            start_new_session=True,
        )
        clients.append((worker, port, display, run_dir, client, xvfb))
    for _, port, _, _, client, _ in clients:
        wait_for_port(port, client)
    return clients


def evaluate_seed(
    root: Path,
    state: Path,
    seed: int,
    worker_slot: tuple[int, int],
    checkpoint_name: str,
) -> Path:
    worker, port = worker_slot
    fragment = state / "fragments" / f"seed_{seed}.json"
    log_path = state / "logs" / f"seed_{seed}.log"
    fragment.parent.mkdir(parents=True, exist_ok=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(
        {
            "QRL_PORT": str(port),
            "CHAIN_NET": checkpoint_name,
            "TRIES": "5",
            "EP_TICKS": "6000",
            "FRAME_EVERY": "0",  # AUDIT: no archived live observation samples.
            "RESULT_JSON": str(fragment),
            "FRAMES_ROOT": str(state / "no_frames" / f"worker_{worker}"),
            "PYTHONHASHSEED": "0",
        }
    )
    # Patch only the imported evaluator's client constructor.  All policy,
    # observation, RNG, receipts, and artifact code remains the pinned file.
    # AUDIT: those semantics therefore come from pinned qrl_chain_demo.py,
    # ppo_chain_cu.py, Recorder.java, and SemanticCamera.java, not this driver.
    wrapper = (
        "import functools, os, sys; "
        f"sys.path.insert(0, {str(root / 'java')!r}); "
        f"sys.path.insert(0, {str(root / 'blaze' / 'env')!r}); "
        "import qrl_chain_demo as q; "
        "q.QRLEnv = functools.partial(q.QRLEnv, "
        "port=int(os.environ['QRL_PORT'])); "
        "q.main()"
    )
    command = [
        "uv",
        "run",
        "--no-project",
        "--with",
        "torch,numpy",
        "python",
        "-c",
        wrapper,
        str(seed),
    ]
    with log_path.open("ab") as log:
        subprocess.run(command, cwd=root, env=env, stdout=log, stderr=log, check=True)
    return fragment


def run_evaluation(args, checkpoint_sha256: str) -> None:
    state = args.state.resolve()
    state.mkdir(parents=True, exist_ok=True)
    clients = []
    try:
        clients = start_clients(
            args.root, state, args.workers, args.base_port, args.base_display
        )
        available = __import__("queue").Queue()
        for worker, port, *_ in clients:
            available.put((worker, port))

        def task(seed: int) -> Path:
            fragment = state / "fragments" / f"seed_{seed}.json"
            if args.resume and fragment.exists():
                return fragment
            slot = available.get()
            try:
                return evaluate_seed(
                    args.root, state, seed, slot, args.checkpoint.name
                )
            finally:
                available.put(slot)

        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
            fragments = list(pool.map(task, args.seeds))
        merged = merge_fragments(fragments, args.seeds)
        if merged["commit"] != PIN or not merged["tracked_clean"]:
            raise RuntimeError("merged result did not preserve pinned clean provenance")
        if merged["checkpoint_sha256"] != checkpoint_sha256:
            raise RuntimeError("merged result checkpoint digest changed")
        write_json(args.output, merged)

        # Exercise the repository's strict validator against the completed sim artifact.
        # AUDIT: pair_sim2real checks schema/provenance/outcomes, not interface parity.
        subprocess.run(
            [
                "uv",
                "run",
                "--no-project",
                "python",
                str(args.root / "paper" / "tools" / "pair_sim2real.py"),
                "--sim",
                str(args.sim),
                "--java",
                str(args.output),
                "--out-json",
                str(state / "paired.json"),
                "--out-md",
                str(state / "paired.md"),
            ],
            cwd=args.root,
            check=True,
        )
    finally:
        for *_, client, xvfb in clients:
            for process in (client, xvfb):
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(process.pid, signal.SIGTERM)
        for *_, client, xvfb in clients:
            for process in (client, xvfb):
                with contextlib.suppress(subprocess.TimeoutExpired):
                    process.wait(timeout=10)
                if process.poll() is None:
                    with contextlib.suppress(ProcessLookupError):
                        os.killpg(process.pid, signal.SIGKILL)


def self_test(root: Path) -> None:
    seeds = DEFAULT_SEEDS
    with tempfile.TemporaryDirectory() as tmp:
        paths = []
        for seed in reversed(seeds):
            artifact = {
                field: {
                    "schema": "netherite.sim2real.v1",
                    "environment": "java-1.11.2",
                    "commit": PIN,
                    "tracked_clean": True,
                    "checkpoint": "blaze/rl/out/paper_chain_1p92b.pt",
                    "checkpoint_sha256": "abc",
                    "tries": 5,
                    "ep_ticks": 6000,
                    "repeat": 4,
                    "sampling": "categorical",
                    "rng_protocol": "torch.manual_seed(seed*100+attempt)",
                    "success_source": "live_java_obs.inv_counts[torch]",
                }[field]
                for field in MERGE_FIELDS
            }
            artifact.update(
                {
                    "seeds": [seed],
                    "attempts": [
                        {
                            "seed": seed,
                            "attempt": attempt,
                            "policy_rng_seed": seed * 100 + attempt,
                            "world_seed": seed,
                            "actions_sent": 4,
                            "non_noop_steps": 1,
                            "success": False,
                            "observed_torches": 0,
                            "reached": 0,
                            "success_source": "live_java_obs.inv_counts[torch]",
                            "bridge_action_seq": 4,
                            "bridge_action_fnv64": "abcd",
                            "local_action_fnv64": "abcd",
                        }
                        for attempt in range(5)
                    ],
                    "per_seed_reached": {str(seed): 0},
                }
            )
            path = Path(tmp) / f"{seed}.json"
            write_json(path, artifact)
            paths.append(path)
        merged = merge_fragments(paths, seeds)
        assert merged["seeds"] == list(seeds)
        assert [attempt["seed"] for attempt in merged["attempts"]] == [
            seed for seed in seeds for _ in range(5)
        ]
        assert merged["per_seed_reached"] == {str(seed): 0 for seed in seeds}

        pair_path = root / "paper" / "tools" / "pair_sim2real.py"
        spec = importlib.util.spec_from_file_location("pair_sim2real", pair_path)
        if spec is None or spec.loader is None:
            raise RuntimeError(f"cannot import strict validator from {pair_path}")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        sim = json.loads(json.dumps(merged))
        sim["environment"] = "magma"
        for attempt in sim["attempts"]:
            attempt.pop("bridge_action_seq")
            attempt.pop("bridge_action_fnv64")
            attempt.pop("local_action_fnv64")
            attempt["success_source"] = "magma_state.inventory[torch]"
        module.validate_pair(sim, merged)
    print("SELFTEST PASS")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--state", type=Path, default=DEFAULT_STATE)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--sim", type=Path)
    parser.add_argument("--checkpoint", type=Path)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--base-port", type=int, default=25610)
    parser.add_argument("--base-display", type=int, default=11)
    parser.add_argument("--seeds", type=int, nargs="+", default=list(DEFAULT_SEEDS))
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    args.root = args.root.resolve()
    args.seeds = tuple(args.seeds)
    args.checkpoint = args.checkpoint or (
        args.root / "blaze" / "rl" / "out" / "paper_chain_1p92b.pt"
    )
    args.output = args.output or (args.state / "java.json")
    args.sim = args.sim or (args.root / "paper" / "results" / "sim.json")
    return args


def main() -> None:
    args = parse_args()
    if args.self_test:
        self_test(args.root)
        return
    if not 1 <= args.workers <= len(args.seeds):
        raise SystemExit("workers must be between 1 and the seed count")
    expected = None
    if args.sim.exists():
        with args.sim.open() as stream:
            expected = json.load(stream).get("checkpoint_sha256")
    checkpoint_sha256 = verify_pin(args.root, args.checkpoint, expected)
    plan = {
        "pin": PIN,
        "tracked_clean": True,
        "checkpoint": str(args.checkpoint),
        "checkpoint_sha256": checkpoint_sha256,
        "seeds": list(args.seeds),
        "workers": args.workers,
        "ports": [args.base_port + i for i in range(args.workers)],
        "displays": [f":{args.base_display + i}" for i in range(args.workers)],
        "state": str(args.state),
        "output": str(args.output),
        "frame_every": 0,
        "tries": 5,
        "ep_ticks": 6000,
    }
    print(json.dumps(plan, indent=2, sort_keys=True))
    if args.run:
        run_evaluation(args, checkpoint_sha256)


if __name__ == "__main__":
    main()
