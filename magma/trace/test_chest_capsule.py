#!/usr/bin/env python3
"""Real-Java/native chest-family transient capsule continuation gate."""

import argparse
import json
import math
import os
import pathlib
import subprocess
import sys
import tempfile


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
sys.path.insert(0, str(HERE))

from state_capsule import create_capsule, emit_magma  # noqa: E402
from test_dragon_crystal_notification import request  # noqa: E402
from trace_java import canonicalize  # noqa: E402


TICKS = 7
OPEN = (1, 0x00000000, 0x00000000)
CLOSE = (0, 0x3F19999A, 0x3F333333)  # exact binary32 0.6F / 0.7F


CASES = (
    ("single_chest_open", 54, False, OPEN),
    ("single_trapped_chest_close", 146, False, CLOSE),
    ("double_chest_open", 54, True, OPEN),
    ("double_trapped_chest_close", 146, True, CLOSE),
)


def selected_containers(state, positions, label="fixture"):
    selected = [value for value in state.get("containers", [])
                if (value.get("x"), value.get("y"), value.get("z"))
                in positions]
    selected.sort(key=lambda value: (value["x"], value["y"], value["z"]))
    if len(selected) != len(positions):
        raise AssertionError(
            f"{label}: expected {len(positions)} chest tiles at "
            f"{sorted(positions)!r}, got selected={selected!r}, "
            f"all={state.get('containers', [])!r}")
    return selected


def compared(container):
    return {
        key: container[key]
        for key in (
            "type", "x", "y", "z", "size", "num_players_using",
            "lid_angle_bits", "prev_lid_angle_bits", "ticks_since_sync",
            "items",
        )
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25699)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA), "game"], check=True,
        stdout=subprocess.DEVNULL,
    )
    temp_root = pathlib.Path(os.environ.get(
        "TMPDIR", str(MAGMA.parent / ".tmp")))
    temp_root.mkdir(parents=True, exist_ok=True)
    locked = False
    with tempfile.TemporaryDirectory(
            prefix="chest_capsule_", dir=temp_root) as raw_temp:
        root = pathlib.Path(raw_temp)
        observation = request(args.port, "obs")
        try:
            locked_state = request(args.port, "server_step_lock")
            locked = True
            player = locked_state["authoritative"]
            existing_containers = locked_state["authoritative"].get(
                "containers", [])
            if existing_containers:
                cleared = request(args.port, "setblocks_locked", {
                    "blocks": [[
                        value["x"], value["y"], value["z"], 0, 0,
                    ] for value in existing_containers],
                })
                if not cleared.get("ok"):
                    raise AssertionError(
                        f"initial isolated-container cleanup failed: {cleared}")
            cleared_entities = request(args.port, "clear_entities_locked")
            if not cleared_entities.get("ok"):
                raise AssertionError(
                    f"initial entity cleanup failed: {cleared_entities}")
            base_x = math.floor(float(player["x"])) + 20
            z = math.floor(float(player["z"]))
            y = 200
            for case_index, (name, block, double, transient) in enumerate(
                    CASES):
                x = base_x
                positions = {(x, y, z)}
                if double:
                    positions.add((x + 1, y, z))
                staging = []
                for clear_x in range(x - 1, x + 3):
                    for clear_z in range(z - 1, z + 2):
                        staging.append([clear_x, y, clear_z, 0, 0])
                staging.append([x, y, z, block, 2])
                if double:
                    staging.append([x + 1, y, z, block, 2])
                staged = request(args.port, "setblocks_locked", {
                    "blocks": staging,
                })
                if not staged.get("ok"):
                    raise AssertionError(f"{name}: staging failed: {staged}")
                viewers, lid_bits, prev_bits = transient
                authoritative = None
                for half, (tile_x, tile_y, tile_z) in enumerate(
                        sorted(positions)):
                    restored = request(
                        args.port, "set_container_slot_locked", {
                            "x": tile_x, "y": tile_y, "z": tile_z,
                            "slot": 0,
                            "item": 264 if half == 0 else 297,
                            "count": half + 1,
                            "meta": 0,
                            "num_players_using": viewers,
                            "lid_angle_bits": lid_bits,
                            "prev_lid_angle_bits": prev_bits,
                            "ticks_since_sync": 17 + half,
                        })
                    if not restored.get("ok"):
                        raise AssertionError(
                            f"{name}: Java transient restore failed: "
                            f"{restored}")
                    authoritative = restored["authoritative"]
                before = selected_containers(authoritative, positions, name)
                if any(
                        value["num_players_using"] != viewers
                        or value["lid_angle_bits"] != lid_bits
                        or value["prev_lid_angle_bits"] != prev_bits
                        for value in before):
                    raise AssertionError(
                        f"{name}: Java did not expose exact transient state")

                box = [x - 1, y, z - 1, x + 2, y, z + 1]
                case_root = root / name
                case_root.mkdir()
                blocks = case_root / "blocks.bin"
                dumped = request(args.port, "getblocks_locked", {
                    "x0": box[0], "y0": box[1], "z0": box[2],
                    "x1": box[3], "y1": box[4], "z1": box[5],
                    "file": str(blocks),
                })
                if not dumped.get("ok"):
                    raise AssertionError(f"{name}: block capture failed")
                observation["authoritative"] = authoritative
                state = canonicalize(-1, observation, box)
                state_file = case_root / "state.json"
                state_file.write_text(json.dumps(state), encoding="utf-8")
                capsule = case_root / "capsule"
                create_capsule(
                    state_file, blocks, box, capsule, seed=0,
                    source_engine="minecraft-java",
                    source_version="1.11.2",
                )
                script = case_root / "load.jsonl"
                emit_magma(capsule, script)
                java_states = [request(args.port, "step")["authoritative"]
                               for _ in range(TICKS)]

                native_file = case_root / "native.jsonl"
                native_env = os.environ.copy()
                native_env["MAGMA_CAPSULE_DIR"] = str(capsule)
                subprocess.run([
                    str(MAGMA / "magma_game"), "--seed", "0",
                    "--world", "superflat", "--view-distance", "1",
                    "--headless", "--ticks", str(TICKS), "--mobs", "off",
                    "--script", str(script), "--state-out", str(native_file),
                    "--render", "off", "--pace", "unlimited",
                    "--weather", "off", "--daylight", "off",
                ], cwd=MAGMA, env=native_env, check=True,
                   stdout=subprocess.DEVNULL)
                native_states = [json.loads(line) for line in
                    native_file.read_text(encoding="utf-8").splitlines()]
                if len(native_states) != TICKS:
                    raise AssertionError(
                        f"{name}: native emitted {len(native_states)} ticks")
                for tick, (java, native) in enumerate(
                        zip(java_states, native_states), start=1):
                    expected = [compared(value) for value in
                        selected_containers(java, positions, name)]
                    actual = [compared(value) for value in
                        selected_containers(native, positions, name)]
                    if expected != actual:
                        raise AssertionError(
                            f"{name} tick {tick} differs: "
                            f"Java={expected!r} native={actual!r}")
                cleared = request(args.port, "setblocks_locked", {
                    "blocks": [[px, py, pz, 0, 0]
                               for px, py, pz in sorted(positions)],
                })
                if not cleared.get("ok"):
                    raise AssertionError(f"{name}: cleanup failed: {cleared}")
                cleared_entities = request(
                    args.port, "clear_entities_locked")
                if not cleared_entities.get("ok"):
                    raise AssertionError(
                        f"{name}: entity cleanup failed: {cleared_entities}")
        finally:
            if locked:
                request(args.port, "server_step_unlock")

    print(
        "PASS real Java/capsule/native: ordinary/trapped single/double "
        "chest viewer count, raw lid floats, private sync counter, inventory, "
        f"and open/close clamp over {TICKS} ticks")


if __name__ == "__main__":
    main()
