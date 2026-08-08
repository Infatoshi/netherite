#!/usr/bin/env python3
"""Record a scripted Java-oracle scenario and archive its canonical tape."""

from __future__ import annotations

import argparse
import json
import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import yaml

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
JAVA = REPO / "java"
TRACE = HERE.parent / "trace"
TAPES = HERE.parent / "tapes"
SLUG_RE = re.compile(r"^[a-z0-9][a-z0-9_-]*$")
ORACLE_PORTS = (25575, 25580, 25581, 5900)

sys.path.insert(0, str(JAVA))
from mcwindow_script import load_script  # noqa: E402
from qrl_client import NetheriteEnv  # noqa: E402


def load_spec(path: Path) -> dict:
    spec = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(spec, dict):
        raise ValueError("scenario spec must be a mapping")
    name = spec.get("name")
    if not isinstance(name, str) or not SLUG_RE.fullmatch(name):
        raise ValueError("name must match [a-z0-9][a-z0-9_-]*")

    world = spec.get("world", {})
    if not isinstance(world, dict):
        raise ValueError("world must be a mapping")
    allowed_world = {"seed", "mode", "type", "structures"}
    unknown_world = set(world) - allowed_world
    if unknown_world:
        raise ValueError(f"unknown world fields: {sorted(unknown_world)}")
    world = {
        "seed": int(world.get("seed", 0)),
        "mode": str(world.get("mode", "survival")),
        "type": str(world.get("type", "default")),
        "structures": bool(world.get("structures", True)),
    }
    if world["mode"] not in {"survival", "creative"}:
        raise ValueError("world.mode must be survival or creative")
    if world["type"] not in {"default", "flat"}:
        raise ValueError("world.type must be default or flat")

    commands = spec.get("setup_commands", [])
    if not isinstance(commands, list) or not all(
        isinstance(command, str) and command.strip() for command in commands
    ):
        raise ValueError("setup_commands must be a list of non-empty strings")

    qrl_steps = spec.get("setup_qrl", [])
    if not isinstance(qrl_steps, list) or not all(
        isinstance(step, dict) and isinstance(step.get("cmd"), str)
        for step in qrl_steps
    ):
        raise ValueError("setup_qrl must be a list of {cmd, action?} mappings")

    duration_ticks = int(spec.get("duration_ticks", 0))
    frames_every = int(spec.get("frames_every", 20))
    if duration_ticks <= 0:
        raise ValueError("duration_ticks must be positive")
    if frames_every < 0:
        raise ValueError("frames_every must be non-negative")

    input_spec = spec.get("input")
    if not isinstance(input_spec, dict):
        raise ValueError("input must be a mapping")
    choices = [key for key in ("segments", "file") if key in input_spec]
    if len(choices) != 1:
        raise ValueError("input must declare exactly one of segments or file")

    known = spec.get("known_divergences", [])
    if not isinstance(known, list):
        raise ValueError("known_divergences must be a list")

    return {
        **spec,
        "name": name,
        "world": world,
        "setup_commands": commands,
        "setup_qrl": qrl_steps,
        "duration_ticks": duration_ticks,
        "frames_every": frames_every,
        "input": input_spec,
        "known_divergences": known,
    }


def materialize_segments(spec: dict, spec_path: Path, output: Path) -> list[dict]:
    input_spec = spec["input"]
    if "file" in input_spec:
        source = (spec_path.parent / str(input_spec["file"])).resolve()
        segments = load_script(source)
    else:
        inline = input_spec["segments"]
        if not isinstance(inline, list):
            raise ValueError("input.segments must be a list")
        with tempfile.NamedTemporaryFile("w", suffix=".jsonl", delete=False) as tmp:
            tmp_path = Path(tmp.name)
            for segment in inline:
                tmp.write(json.dumps(segment, separators=(",", ":")) + "\n")
        try:
            segments = load_script(tmp_path)
        finally:
            tmp_path.unlink(missing_ok=True)

    wanted_seconds = spec["duration_ticks"] / 20.0
    scripted_seconds = sum(segment["seconds"] for segment in segments)
    if scripted_seconds > wanted_seconds + 1e-9:
        raise ValueError(
            f"input lasts {scripted_seconds:g}s, longer than duration_ticks "
            f"({wanted_seconds:g}s at 20 TPS)"
        )
    if scripted_seconds < wanted_seconds:
        segments.append(
            {
                "seconds": wanted_seconds - scripted_seconds,
                "keys": [],
                "buttons": [],
                "look": None,
            }
        )

    with output.open("w", encoding="utf-8") as target:
        for segment in segments:
            target.write(json.dumps(segment, separators=(",", ":")) + "\n")
    return segments


def run(command: list[str], *, cwd: Path = REPO, env: dict | None = None) -> str:
    print("[scenario] +", subprocess.list2cmdline(command), flush=True)
    result = subprocess.run(
        command, cwd=cwd, env=env, text=True, capture_output=True, check=False
    )
    if result.stdout:
        print(result.stdout, end="", flush=True)
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr, flush=True)
    if result.returncode:
        raise RuntimeError(f"command failed with rc={result.returncode}: {command[0]}")
    return result.stdout


def port_is_open(port: int) -> bool:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.15):
            return True
    except OSError:
        return False


def process_snapshot() -> set[int]:
    return {int(path.name) for path in Path("/proc").iterdir() if path.name.isdigit()}


def process_command(pid: int) -> tuple[str, str]:
    try:
        comm = (Path("/proc") / str(pid) / "comm").read_text().strip()
        raw = (Path("/proc") / str(pid) / "cmdline").read_bytes()
        return comm, raw.replace(b"\0", b" ").decode(errors="replace")
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        return "", ""


def is_oracle_process(pid: int) -> bool:
    comm, command = process_command(pid)
    if comm in {"Xvfb", "x11vnc", "openbox"}:
        return True
    if comm == "java" and (
        "GradleStart --username" in command
        or ("GradleWrapperMain" in command and "runClient" in command)
    ):
        return True
    return "mcwindow_server.py" in command and "scenario.py" not in command


def assert_clean_oracle() -> set[int]:
    occupied = [port for port in ORACLE_PORTS if port_is_open(port)]
    oracle_pids = [pid for pid in process_snapshot() if is_oracle_process(pid)]
    if occupied or oracle_pids or Path("/tmp/.X11-unix/X1").exists():
        raise RuntimeError(
            "exclusive lock acquired but an oracle session is still present: "
            f"ports={occupied} pids={oracle_pids}; refusing start_vnc_client cleanup"
        )
    return process_snapshot()


def wait_for_qrl(timeout: float = 600.0) -> NetheriteEnv:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        # The bridge serves ONE client at a time: a probe that times out but
        # leaves its socket open wedges the port for every retry until TCP
        # resets it (~5 min), which is longer than this whole loop. Always
        # close the probe env on failure, and give the first reset enough
        # time to ride out a slow initial world load.
        env = None
        try:
            env = NetheriteEnv()
            response = env.reset(timeout=30.0)
            if response.get("ok"):
                return env
        except (ConnectionError, OSError, TimeoutError) as error:
            last_error = error
        if env is not None:
            try:
                env.close()
            except OSError:
                pass
        time.sleep(1.0)
    raise TimeoutError(f"qrl bridge did not become ready: {last_error}")


def wait_for_port(port: int, timeout: float = 60.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if port_is_open(port):
            return
        time.sleep(0.25)
    raise TimeoutError(f"port {port} did not become ready")


def terminate_process_group(pgid: int | None) -> None:
    if pgid is None:
        return
    try:
        os.killpg(pgid, signal.SIGTERM)
    except ProcessLookupError:
        return


def cleanup_started(baseline: set[int], process_groups: list[int]) -> None:
    for pgid in reversed(process_groups):
        terminate_process_group(pgid)
    deadline = time.monotonic() + 8.0
    while time.monotonic() < deadline:
        live = [pid for pid in process_snapshot() - baseline if is_oracle_process(pid)]
        if not live:
            return
        for pid in live:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        time.sleep(0.25)
    live = [pid for pid in process_snapshot() - baseline if is_oracle_process(pid)]
    for pid in live:
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def rewrite_tape_frames(tape: Path, frames_dir: Path) -> None:
    rewritten = tape.with_suffix(".jsonl.rewrite")
    with (
        tape.open(encoding="utf-8") as source,
        rewritten.open("w", encoding="utf-8") as target,
    ):
        for line in source:
            row = json.loads(line)
            if "frame" in row:
                row["frame"] = str(frames_dir / Path(row["frame"]).name)
            target.write(json.dumps(row, separators=(",", ":")) + "\n")
    rewritten.replace(tape)


def archive_tape(source_tape: Path, spec: dict, spec_path: Path) -> Path:
    source_base = source_tape.with_suffix("")
    meta_path = source_base.with_suffix(".meta.json")
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    stamp = str(meta["created_utc"])
    target_base = source_tape.parent / f"scenario_{spec['name']}_{stamp}"
    target_tape = target_base.with_suffix(".jsonl")
    if target_tape.exists():
        raise FileExistsError(f"scenario archive already exists: {target_tape}")

    siblings = sorted(source_tape.parent.glob(source_base.name + "*"))
    for source in siblings:
        suffix = source.name[len(source_base.name) :]
        source.rename(source_tape.parent / (target_base.name + suffix))

    target_frames = Path(str(target_base) + "_frames")
    rewrite_tape_frames(target_tape, target_frames)

    target_meta = target_base.with_suffix(".meta.json")
    meta = json.loads(target_meta.read_text(encoding="utf-8"))
    meta.update(
        {
            "name": target_base.name,
            "tape_jsonl": str(target_tape),
            "frames_dir": str(target_frames),
            "parquet": str(target_base.with_suffix(".parquet")),
            "scenario": spec["name"],
            "scenario_spec": str(spec_path.resolve()),
            "duration_ticks": spec["duration_ticks"],
        }
    )
    target_meta.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")

    known = spec["known_divergences"]
    if known:
        known_path = target_base.with_suffix(".known_divergences.json")
        known_path.write_text(
            json.dumps({"version": 1, "divergences": known}, indent=2) + "\n",
            encoding="utf-8",
        )

    run([sys.executable, str(TRACE / "tape.py"), "pack", str(target_tape)])
    return target_tape


def configure_launch(world: dict) -> None:
    overrides = [
        f"world.seed={world['seed']}",
        f"world.mode={world['mode']}",
        f"world.type={world['type']}",
        f"world.structures={'true' if world['structures'] else 'false'}",
    ]
    command = [
        sys.executable,
        str(JAVA / "mc_cli.py"),
        "--config",
        str(JAVA / "fast.yaml"),
        "--no-launch",
    ]
    for override in overrides:
        command.extend(("--set", override))
    run(command)


def start_oracle(process_groups: list[int]) -> None:
    print("[scenario] starting Java oracle with java/start_vnc_client.sh", flush=True)
    process = subprocess.Popen(
        ["bash", str(JAVA / "start_vnc_client.sh")],
        cwd=REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    process_groups.append(process.pid)
    stdout, _ = process.communicate(timeout=60)
    if stdout:
        print(stdout, end="", flush=True)
    if process.returncode:
        raise RuntimeError(f"start_vnc_client.sh failed with rc={process.returncode}")


def start_mcwindow(process_groups: list[int]) -> None:
    log_path = Path("/tmp/netherite_scenario_mcwindow.log")
    log = log_path.open("w")
    env = {
        **os.environ,
        "DISPLAY": ":1",
        "MCW_W": "854",
        "MCW_H": "480",
        "PYTHONUNBUFFERED": "1",
    }
    process = subprocess.Popen(
        [sys.executable, str(JAVA / "mcwindow_server.py")],
        cwd=REPO,
        env=env,
        stdout=log,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    log.close()
    process_groups.append(process.pid)
    wait_for_port(25581)
    if process.poll() is not None:
        raise RuntimeError(f"mcwindow server exited; see {log_path}")


def record(spec_path: Path, result_file: Path) -> Path:
    if os.environ.get("SCENARIO_ORACLE_LOCKED") != "1":
        raise RuntimeError("run through run_scenario.sh so the oracle lock is held")
    spec = load_spec(spec_path)
    baseline = assert_clean_oracle()
    process_groups: list[int] = []
    qrl: NetheriteEnv | None = None
    recording = False
    source_tape: Path | None = None
    archived: Path | None = None
    with tempfile.TemporaryDirectory(prefix="netherite_scenario_") as tmp:
        segments_path = Path(tmp) / "segments.jsonl"
        segments = materialize_segments(spec, spec_path, segments_path)
        idle_input = all(
            not segment.get("keys") and not segment.get("buttons")
            and segment.get("look") in (None, [0, 0])
            for segment in segments
        )
        print(
            f"[scenario] {spec['name']}: {spec['duration_ticks']} target ticks, "
            f"{len(segments)} input segments, frames every {spec['frames_every']}",
            flush=True,
        )
        try:
            configure_launch(spec["world"])
            start_oracle(process_groups)
            qrl = wait_for_qrl()
            world = {**spec["world"], "fresh": True}
            response = qrl.reset(world, timeout=300.0)
            if not response.get("ok"):
                raise RuntimeError(f"fresh world reset failed: {response}")

            last_position = None
            stable = 0
            for _ in range(200):
                observation = qrl.step({})
                position = (
                    observation["x"],
                    observation["y"],
                    observation["z"],
                )
                stable = stable + 1 if position == last_position else 0
                last_position = position
                if stable >= 10:
                    break
            else:
                raise RuntimeError("player position did not stabilize after reset")

            # All commands in one runcmds batch execute in a single server
            # tick, so a summon aimed near a just-teleported player lands in a
            # chunk that only loads on a later tick and fails (executeCommand
            # returns 0, as with fill into an unloaded chunk). Run each command
            # in its own batch with settle ticks in between so tp-triggered
            # chunk loads complete before dependent summons run.
            for command in spec["setup_commands"]:
                response = qrl._cmd(
                    {"cmd": "runcmds", "action": {"cmds": [command]}}
                )
                if not response.get("ok") or response.get("failed"):
                    raise RuntimeError(
                        f"scenario setup command failed: {command!r} -> {response}"
                    )
                for _ in range(int(spec.get("setup_settle_ticks", 5))):
                    qrl.step({})
            print(f"[scenario] setup: {len(spec['setup_commands'])} commands ok",
                  flush=True)

            # Raw bridge commands (dim/portal_touch/use_end_eye/...) run after
            # chat-command setup so gear and rules are staged in the overworld
            # before any cross-dimension transfer.
            for step in spec["setup_qrl"]:
                payload = {"cmd": step["cmd"]}
                if "action" in step:
                    payload["action"] = step["action"]
                response = qrl._cmd(payload, read_deadline=300.0)
                if not response.get("ok"):
                    raise RuntimeError(
                        f"scenario setup_qrl step failed: {payload!r} -> {response}"
                    )
                for _ in range(
                    int(step.get("settle_ticks",
                                 spec.get("setup_settle_ticks", 5)))
                ):
                    qrl.step({})
            if spec["setup_qrl"]:
                print(f"[scenario] setup_qrl: {len(spec['setup_qrl'])} steps ok",
                      flush=True)

            # The qrl bridge serves one socket at a time (serve() accepts the
            # next client only after the current one closes); release ours
            # before tape.py opens its own connection for recstart/recstop.
            qrl.close()
            qrl = None

            if not idle_input:
                start_mcwindow(process_groups)
            stdout = run(
                [
                    sys.executable,
                    str(TRACE / "tape.py"),
                    "start",
                    "--seed",
                    str(spec["world"]["seed"]),
                    "--frames-every",
                    str(spec["frames_every"]),
                ]
            )
            tape_line = next(
                (line for line in stdout.splitlines() if line.startswith("tape: ")),
                None,
            )
            if tape_line is None:
                raise RuntimeError("tape.py start did not print the tape path")
            source_tape = Path(tape_line.removeprefix("tape: ")).resolve()
            recording = True
            if idle_input:
                qrl = wait_for_qrl()
                try:
                    for _ in range(spec["duration_ticks"]):
                        qrl.step({})
                finally:
                    qrl.close()
                    qrl = None
            else:
                run([sys.executable, str(JAVA / "mcwindow_script.py"), str(segments_path)])
            run([sys.executable, str(TRACE / "tape.py"), "stop"])
            recording = False
            archived = archive_tape(source_tape, spec, spec_path)
        finally:
            if recording:
                try:
                    run([sys.executable, str(TRACE / "tape.py"), "stop"])
                except Exception as error:
                    print(
                        f"[scenario] emergency recstop failed: {error}", file=sys.stderr
                    )
            if qrl is not None:
                qrl.close()
            cleanup_started(baseline, process_groups)

    if archived is None:
        raise RuntimeError("recording ended without an archived tape")
    result_file.write_text(str(archived) + "\n", encoding="utf-8")
    return archived


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    record_parser = subparsers.add_parser("record")
    record_parser.add_argument("spec", type=Path)
    record_parser.add_argument("--result-file", required=True, type=Path)
    args = parser.parse_args()
    if args.command == "record":
        tape = record(args.spec.resolve(), args.result_file.resolve())
        print(f"[scenario] recording archived: {tape}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
