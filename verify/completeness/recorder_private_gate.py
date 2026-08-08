#!/usr/bin/env python3
"""Gate recorder-private observations and optionally refresh them on Java."""

from __future__ import annotations

import argparse
import collections
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import time
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
START = ROOT / "java" / "start_oracle_instance.sh"
MANIFEST = HERE / "recorder_private_manifest.json"
RECORDER = ROOT / "java" / "Minecraft" / "src" / "main" / "java" / "qrl" / "Recorder.java"
MIXINS = ROOT / "java" / "Minecraft" / "src" / "main" / "resources" / "mixins.overclocking.malmomod.json"
REPLAY = ROOT / "verify" / "trace" / "replay_tape.py"
sys.path.insert(0, str(ROOT / "java"))
from qrl_client import NetheriteEnv  # noqa: E402


class RecorderPrivateError(RuntimeError):
    pass


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise RecorderPrivateError(message)


def _static_gate(manifest: dict[str, Any]) -> None:
    _require(manifest.get("version") == 1 and manifest.get("todo") == "HAR-08",
             "invalid recorder-private manifest identity")
    _require(set(manifest.get("required_cases", [])) == {
        "explosion", "cloud", "weather", "hand", "gui"},
        "manifest must own all five HAR-08 cases")
    requirements = manifest.get("requirements", {})
    _require(requirements.get("particle_ids") == [0, 1, 2, 11, 15, 34, 48],
             "manifest particle constructor coverage changed")
    _require(requirements.get("live_particle_ids") == [0, 1, 2, 15],
             "manifest checked-live particle coverage changed")
    _require(requirements.get("render_partial_ticks") == 1.0,
             "render partial contract changed")

    config = json.loads(MIXINS.read_text(encoding="utf-8"))
    clients = set(config.get("client", []))
    _require({
        "AccessorParticleState", "AccessorParticleExplosionLargeState",
        "AccessorParticleExplosionHugeState", "AccessorParticleSpellState",
        "MixinRecordParticles", "MixinRecordGuiInput",
        "MixinRecordGuiTransition", "MixinRecordWindowClick",
    } <= clients, "recorder-private client mixins are incomplete")
    recorder = RECORDER.read_text(encoding="utf-8")
    for token in (
        '"recorder_private_v\\":1', '"render_partial_ticks\\":1.0',
        '"client_total_time\\":', '"server_total_time\\":',
        '"server_player_ticks\\":', '"render_partial\\":1.0',
        '"gin\\":[', 'recordGuiWindowClick', 'recordGuiTransition',
        'AccessorParticleSpellState',
    ):
        _require(token in recorder, f"Recorder lost required surface: {token}")
    replay = REPLAY.read_text(encoding="utf-8")
    for token in ("spawn_particle_state", "in (11, 15, 34, 48)",
                  "render_partial_ticks", "unsupported render_partial"):
        _require(token in replay, f"replay lost required surface: {token}")

    live = manifest.get("last_live")
    _require(isinstance(live, dict),
             "no checked live HAR-08 result; run --live --update")
    _require(set(live.get("cases", {})) == set(manifest["required_cases"]),
             "checked live result is missing a HAR-08 case")
    for name, row in live["cases"].items():
        _require(row.get("java_ab_diff_pixels") == 0,
                 f"{name}: Java A/B is not exact")
        _require(row.get("signal_pixels", 0) >= 1,
                 f"{name}: observation is empty")
    _require(set(map(int, live.get("particle_counts", {}))) >=
             set(requirements["live_particle_ids"]),
             "checked live result lacks an explosion/cloud constructor class")
    _require(set(live.get("gui_event_counts", {})) >=
             set(requirements["gui_events"]),
             "checked live result lacks a GUI interaction class")
    _require(live.get("clock_rows", 0) == live.get("tape_ticks", -1)
             and live.get("tape_ticks", 0) > 0,
             "checked live result lacks clocks on a tape row")


def _oracle(action: str, instance: int, environment: dict[str, str]) -> None:
    command = ["bash", str(START), action, str(instance)]
    if action == "start":
        command.append("88008")
    result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
    if result.returncode:
        raise RecorderPrivateError(
            f"oracle instance {action} failed with rc={result.returncode}")


def _step(env: NetheriteEnv, count: int) -> None:
    for _ in range(count):
        result = env.step({})
        _require(result.get("ok", True), f"oracle step failed: {result}")


def _command(env: NetheriteEnv, command: str) -> None:
    result = env._cmd({"cmd": "runcmds", "action": {"cmds": [command]}})
    _require(result.get("ok") and result.get("failed", 0) == 0,
             f"oracle command failed: {command}: {result}")


def _pixel_difference(left: pathlib.Path, right: pathlib.Path) -> int:
    from PIL import Image, ImageChops
    with Image.open(left).convert("RGB") as a, Image.open(right).convert("RGB") as b:
        _require(a.size == b.size and a.size[0] > 0 and a.size[1] > 0,
                 f"invalid image pair: {left}, {right}")
        raw = ImageChops.difference(a, b).tobytes()
        return sum(raw[index] != 0 or raw[index + 1] != 0
                   or raw[index + 2] != 0
                   for index in range(0, len(raw), 3))


def _pair(env: NetheriteEnv, root: pathlib.Path, name: str,
          baseline: pathlib.Path | None = None, *,
          separate: bool = False) -> tuple[dict[str, int], pathlib.Path]:
    left = root / f"{name}_a.png"
    right = root / f"{name}_b.png"
    if separate:
        results = [env._cmd({"cmd": "frame", "action": {"file": str(path)}})
                   for path in (left, right)]
    else:
        results = [env._cmd({"cmd": "frame_pair", "action": {
            "file_a": str(left), "file_b": str(right)}})]
    _require(all(result.get("ok")
                 and result.get("tb") == 1 and result.get("hud") == 1
                 and result.get("render_partial") == 1.0
                 for result in results),
             f"{name}: frame capture failed: {results}")
    if not separate:
        _require(results[0].get("pair") == 1,
                 f"{name}: frame_pair did not report a pair")
    ab = _pixel_difference(left, right)
    signal = _pixel_difference(baseline, left) if baseline else 1
    return {"java_ab_diff_pixels": ab, "signal_pixels": signal}, left


def _x11_pair(display: str, window: str, root: pathlib.Path, name: str,
              baseline: pathlib.Path) -> tuple[dict[str, int], pathlib.Path]:
    """Capture a naturally rendered GUI twice, matching capture_gui.sh."""
    xenv = {**os.environ, "DISPLAY": display}
    info = subprocess.check_output(
        ["xwininfo", "-id", window], env=xenv, text=True)

    def field(label: str) -> int:
        match = re.search(rf"^\s*{re.escape(label)}:\s*(-?\d+)\s*$",
                          info, flags=re.MULTILINE)
        _require(match is not None,
                 f"{name}: xwininfo lacks {label}: {info}")
        return int(match.group(1))

    x = field("Absolute upper-left X")
    y = field("Absolute upper-left Y")
    width = field("Width")
    height = field("Height")
    _require(width > 0 and height > 0,
             f"{name}: invalid X11 window geometry {width}x{height}")
    subprocess.run(
        ["xdotool", "mousemove", str(x + 5), str(y + 5)],
        env=xenv, check=True)
    time.sleep(0.4)
    paths = [root / f"{name}_a.png", root / f"{name}_b.png"]
    for index, path in enumerate(paths):
        if index:
            time.sleep(0.5)
        subprocess.run([
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "x11grab", "-draw_mouse", "0",
            "-video_size", f"{width}x{height}",
            "-i", f"{display}.0+{x},{y}", "-frames:v", "1", str(path),
        ], env=xenv, check=True)
        _require(path.is_file() and path.stat().st_size > 0,
                 f"{name}: empty X11 capture {path}")
    ab = _pixel_difference(paths[0], paths[1])
    signal = _pixel_difference(baseline, paths[0])
    return {"java_ab_diff_pixels": ab, "signal_pixels": signal}, paths[0]


def _rows(path: pathlib.Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip()]


def _wait_for_particle(path: pathlib.Path, env: NetheriteEnv,
                       particle_id: int, limit: int) -> None:
    for _ in range(limit):
        _step(env, 1)
        if path.exists() and any(
            int(particle[0]) == particle_id
            for row in _rows(path)[1:]
            for particle in row.get("pcl", [])
        ):
            return
    raise RecorderPrivateError(
        f"particle id {particle_id} was not observed within {limit} ticks")


def _capture_live(instance: int) -> dict[str, Any]:
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
            prefix="recorder-private-", dir=ROOT / ".tmp") as temporary:
        run_root = pathlib.Path(temporary)
        environment = dict(os.environ)
        environment.update({
            "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
            "ORACLE_POOL_OUT_ROOT": str(run_root / "oracle"),
            "ORACLE_POOL_WAIT": "1",
            "ORACLE_POOL_WORLD_TYPE": "flat",
            "ORACLE_POOL_STRUCTURES": "0",
            "TMPDIR": str(ROOT / ".tmp"),
            "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache" / "uv"),
        })
        port = int(environment.get("ORACLE_POOL_PORT_BASE", "25600")) + instance
        display = ":" + str(int(environment.get(
            "ORACLE_POOL_DISPLAY_BASE", "20")) + instance)
        started = False
        env: NetheriteEnv | None = None
        recording = False
        tape = run_root / "private.jsonl"
        try:
            _oracle("start", instance, environment)
            started = True
            deadline = time.monotonic() + 300.0
            while True:
                try:
                    env = NetheriteEnv(port=port)
                    result = env.reset(timeout=300.0)
                    if result.get("ok"):
                        break
                    env.close()
                    env = None
                except (ConnectionError, OSError, TimeoutError):
                    if env is not None:
                        env.close()
                        env = None
                if time.monotonic() >= deadline:
                    raise RecorderPrivateError("oracle world did not become ready")
                time.sleep(0.2)
            _command(env, "/difficulty peaceful")
            _command(env, "/gamerule doMobSpawning false")
            _command(env, "/gamerule doDaylightCycle false")
            _command(env, "/gamerule doWeatherCycle false")
            _command(env, "/time set 6000")
            _command(env, "/weather clear")
            _command(env, "/gamemode creative @p")
            _step(env, 40)
            observation = env.obs()
            x, y, z = (float(observation[key]) for key in ("x", "y", "z"))
            empty_hotbar = [None] * 9
            pin = env._cmd({"cmd": "hud_pin", "action": {
                "x": x, "y": y, "z": z, "yaw": 0.0, "pitch": 0.0,
                "hotbar": empty_hotbar, "hotbar_sel": 0,
                "health": 20.0, "food": 20}})
            _require(pin.get("ok"), f"baseline pin failed: {pin}")
            baseline_case, baseline = _pair(env, run_root, "baseline")
            _require(baseline_case["java_ab_diff_pixels"] == 0,
                     "baseline Java A/B is not exact")

            start = env._cmd({"cmd": "recstart", "action": {
                "file": str(tape), "frames_every": 0}})
            _require(start.get("ok"), f"recstart failed: {start}")
            recording = True

            cases: dict[str, dict[str, int]] = {}
            hand_pin = env._cmd({"cmd": "hud_pin", "action": {
                "x": x, "y": y, "z": z, "yaw": 0.0, "pitch": 0.0,
                "hotbar": [[1, 1, 0]] + [None] * 8, "hotbar_sel": 0,
                "health": 20.0, "food": 20}})
            _require(hand_pin.get("ok"), f"hand pin failed: {hand_pin}")
            cases["hand"], _ = _pair(env, run_root, "hand", baseline)

            _command(env, "/weather rain")
            _step(env, 120)
            weather_rows = _rows(tape)[1:]
            _require(any(float(row.get("rain", 0.0)) > 0.0
                         for row in weather_rows),
                     "weather tape never observed rain strength")
            cases["weather"], _ = _pair(env, run_root, "weather", baseline)
            _command(env, "/weather clear")
            _step(env, 120)

            _command(env, "/summon minecraft:area_effect_cloud "
                     f"{x:.6f} {y + 1.0:.6f} {z + 3.0:.6f} "
                     "{Duration:400,WaitTime:0,Radius:2.0f,Color:16711935}")
            _wait_for_particle(tape, env, 15, 20)
            cases["cloud"], _ = _pair(env, run_root, "cloud", baseline)
            _command(env, "/kill @e[type=minecraft:area_effect_cloud]")
            _step(env, 100)

            _command(env, "/summon minecraft:tnt "
                     f"{x:.6f} {y + 1.0:.6f} {z + 3.0:.6f} {{Fuse:10s}}")
            _wait_for_particle(tape, env, 2, 40)
            cases["explosion"], _ = _pair(
                env, run_root, "explosion", baseline)
            _step(env, 100)

            _command(env, "/gamemode survival @p")
            _command(env,
                     "/replaceitem entity @p slot.inventory.0 minecraft:stone 8")
            preview = env._cmd({"cmd": "pin_preview_anim", "action": {
                "enable": True, "ticks_existed": -1}})
            _require(preview.get("ok"), f"preview pin failed: {preview}")
            _step(env, 5)
            xenv = {**os.environ, "DISPLAY": display}
            window = subprocess.check_output(
                ["xdotool", "search", "--name", "Minecraft"],
                env=xenv, text=True).splitlines()[0]
            subprocess.run(
                ["xdotool", "windowfocus", "--sync", window],
                env=xenv, check=True)
            subprocess.run(
                ["xdotool", "key", "--window", window, "e"],
                env=xenv, check=True)
            _step(env, 8)
            cases["gui"], _ = _x11_pair(
                display, window, run_root, "gui", baseline)
            subprocess.run(
                ["xdotool", "mousemove", "--window", window, "282", "258"],
                env=xenv, check=True)
            subprocess.run(
                ["xdotool", "click", "--window", window, "1"],
                env=xenv, check=True)
            _step(env, 8)

            stop = env._cmd({"cmd": "recstop", "action": {}})
            _require(stop.get("ok"), f"recstop failed: {stop}")
            recording = False
            rows = _rows(tape)
            header, ticks = rows[0], rows[1:]
            particles = collections.Counter(
                int(particle[0]) for row in ticks
                for particle in row.get("pcl", []))
            events = collections.Counter(
                event["kind"] for row in ticks for event in row.get("gin", []))
            clock_names = (
                "client_total_time", "player_ticks", "server_tick",
                "server_total_time", "server_player_ticks", "portal_phase")
            clock_rows = sum(all(name in row for name in clock_names)
                             for row in ticks)
            _require(header.get("recorder_private_v") == 1
                     and header.get("render_partial_ticks") == 1.0,
                     "live tape lacks recorder-private header contract")
            _require(all(len(particle) == 8
                         and particle[7].get("v") == 1
                         for row in ticks for particle in row.get("pcl", [])),
                     "live tape contains a legacy particle row")
            result = {
                "seed": 88008,
                "cases": cases,
                "particle_counts": {str(key): value
                                    for key, value in sorted(particles.items())},
                "gui_event_counts": dict(sorted(events.items())),
                "clock_rows": clock_rows,
                "tape_ticks": len(ticks),
            }
            return result
        finally:
            if env is not None:
                if recording:
                    try:
                        env._cmd({"cmd": "recstop", "action": {}})
                    except Exception:
                        pass
                env.close()
            if started:
                _oracle("stop", instance, environment)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--live", action="store_true")
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--instance", type=int, default=99)
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if args.live:
        live = _capture_live(args.instance)
        manifest["last_live"] = live
        _static_gate(manifest)
        if args.update:
            MANIFEST.write_text(
                json.dumps(manifest, indent=2, sort_keys=False) + "\n",
                encoding="utf-8")
    elif args.update:
        raise RecorderPrivateError("--update requires --live")
    _static_gate(manifest)
    live = manifest["last_live"]
    print("recorder-private gate: PASS "
          f"({live['tape_ticks']} ticks, particles={live['particle_counts']}, "
          f"gui={live['gui_event_counts']})")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RecorderPrivateError as error:
        print(f"recorder-private gate: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
