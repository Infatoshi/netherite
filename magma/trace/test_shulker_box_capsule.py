#!/usr/bin/env python3
"""Real-Java/native Shulker Box transient capsule continuation gate."""

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

import nbt_codec  # noqa: E402
from state_capsule import create_capsule, emit_magma  # noqa: E402
from test_dragon_crystal_notification import request  # noqa: E402
from trace_java import canonicalize  # noqa: E402


TICKS = 7
PROGRESS_OLD_BITS = 0x3E99999A  # exact binary32 0.3F
PROGRESS_BITS = 0x3ECCCCCD      # exact binary32 0.4F


def one_box(state):
    boxes = [value for value in state["containers"]
             if value.get("type") == "shulker_box"]
    if len(boxes) != 1:
        raise AssertionError(f"expected one Shulker Box, got {boxes!r}")
    return boxes[0]


def compared(box):
    return {
        key: box[key]
        for key in (
            "type", "x", "y", "z", "size", "block", "facing",
            "open_count", "animation_status", "progress_bits",
            "progress_old_bits", "items", "item_tag_nbt",
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
            prefix="shulker_box_capsule_", dir=temp_root) as raw_temp:
        temp = pathlib.Path(raw_temp)
        observation = request(args.port, "obs")
        try:
            locked_state = request(args.port, "server_step_lock")
            locked = True
            player = locked_state["authoritative"]
            x = math.floor(float(player["x"])) + 12
            y = 200
            z = math.floor(float(player["z"]))
            staged = request(args.port, "setblocks_locked", {
                "blocks": [[x, y, z, 229, 1]],
            })
            if not staged.get("ok"):
                raise AssertionError(f"Shulker Box staging failed: {staged}")
            empty_tile_nbt = nbt_codec.encode_hex({
                "name": "",
                "tag": {"type": "compound", "value": {}},
            })
            restored = request(args.port, "set_shulker_nbt_locked", {
                "x": x, "y": y, "z": z, "nbt": empty_tile_nbt,
                "open_count": 1,
                "animation_status": 1,
                "progress_old_bits": PROGRESS_OLD_BITS,
                "progress_bits": PROGRESS_BITS,
            })
            if not restored.get("ok"):
                raise AssertionError(
                    f"Java transient restore failed: {restored}")
            authoritative = restored["authoritative"]
            java_before = one_box(authoritative)
            if java_before["progress_old_bits"] != PROGRESS_OLD_BITS \
                    or java_before["progress_bits"] != PROGRESS_BITS \
                    or java_before["animation_status"] != 1 \
                    or java_before["open_count"] != 1:
                raise AssertionError(
                    f"Java did not expose exact transient bits: {java_before}")

            box = [x, y, z, x, y, z]
            blocks = temp / "blocks.bin"
            dumped = request(args.port, "getblocks_locked", {
                "x0": x, "y0": y, "z0": z,
                "x1": x, "y1": y, "z1": z,
                "file": str(blocks),
            })
            if not dumped.get("ok"):
                raise AssertionError(f"block capture failed: {dumped}")
            observation["authoritative"] = authoritative
            state = canonicalize(-1, observation, box)
            state_file = temp / "state.json"
            state_file.write_text(json.dumps(state), encoding="utf-8")
            capsule = temp / "capsule"
            create_capsule(
                state_file, blocks, box, capsule, seed=0,
                source_engine="minecraft-java",
                source_version="1.11.2",
            )
            script = temp / "load.jsonl"
            emit_magma(capsule, script)
            java_states = [request(args.port, "step")["authoritative"]
                           for _ in range(TICKS)]
        finally:
            if locked:
                request(args.port, "server_step_unlock")

        native_file = temp / "native.jsonl"
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
                f"native emitted {len(native_states)} continuation ticks")
        for tick, (java, native) in enumerate(
                zip(java_states, native_states), start=1):
            expected = compared(one_box(java))
            actual = compared(one_box(native))
            if expected != actual:
                raise AssertionError(
                    f"Shulker Box tick {tick} differs: "
                    f"Java={expected!r} native={actual!r}")

    print("PASS real Java/capsule/native: Shulker Box raw progress bits, "
          "progressOld, animation status, viewer count, inventory payload, "
          f"and open clamp over {TICKS} ticks")


if __name__ == "__main__":
    main()
