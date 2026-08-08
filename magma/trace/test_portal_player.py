#!/usr/bin/env python3
"""Bit-exact Java/native EntityPlayerMP portal placement seam."""

import argparse
import json
import pathlib
import subprocess
import sys


JAVA = pathlib.Path(__file__).resolve().parents[2] / "java"
sys.path.insert(0, str(JAVA))
from qrl_client import NetheriteEnv  # noqa: E402


DIRECTIONS = ("south", "west", "north", "east")
SIDES = ("negative", "none", "positive")
SIDE_VALUES = {"negative": -1, "none": 0, "positive": 1}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()
    env = NetheriteEnv(args.host, args.port)
    initial = env.reset({
        "seed": 708, "mode": "survival", "type": "flat",
        "structures": False,
    })
    if not initial.get("ok"):
        raise RuntimeError(f"oracle reset failed: {initial}")
    locked = env._cmd({"cmd": "server_step_lock"})
    if not locked.get("ok"):
        raise RuntimeError(f"server lock failed: {locked}")
    comparisons = 0
    negative_checked = False
    try:
        if int(locked["authoritative"]["dim"]) != 0:
            moved = env._cmd({
                "cmd": "transfer_dimension_locked", "action": {"id": 0},
            })
            if not moved.get("ok"):
                raise AssertionError(moved)
        for axis in ("x", "z"):
            for direction_index, direction in enumerate(DIRECTIONS):
                for side_index, side in enumerate(SIDES):
                    fixture = comparisons
                    action = {
                        "case": fixture, "axis": axis,
                        "teleport_direction": direction,
                        "obstruction_side": side,
                        "portal_vec_x": 0.125 + 0.25 * side_index,
                        "portal_vec_y": 0.2 + 0.15 * direction_index,
                        "vx": 0.03125 * (direction_index + 1),
                        "vz": -0.046875 * (side_index + 1),
                        "yaw": -153.75 + 37.5 * fixture,
                        "pitch": -22.5 + 3.25 * side_index,
                    }
                    expected = env._cmd({
                        "cmd": "teleporter_player_place_locked",
                        "action": action,
                    })
                    if not expected.get("ok"):
                        raise AssertionError(expected)
                    actual = json.loads(subprocess.run([
                        str(args.native.resolve()),
                        str(expected["portal_x"]),
                        str(expected["portal_y"]),
                        str(expected["portal_z"]),
                        "1" if axis == "x" else "2",
                        str(action["portal_vec_x"]),
                        str(action["portal_vec_y"]),
                        str(direction_index),
                        str(SIDE_VALUES[side]),
                        str(action["vx"]), str(action["vz"]),
                        str(action["yaw"]), str(action["pitch"]), "708",
                    ], check=True, capture_output=True, text=True).stdout)
                    if actual != expected:
                        raise AssertionError(
                            f"case {fixture} mismatch\n"
                            f"Java:  {expected}\nNative: {actual}")
                    if not negative_checked:
                        sabotaged = json.loads(json.dumps(actual))
                        sabotaged["position_bits"][1] = "0000000000000000"
                        if sabotaged == expected:
                            raise AssertionError("player-position sabotage escaped")
                        negative_checked = True
                    comparisons += 1
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")
    print("PASS real Java/native: 24 exact player portal placements plus "
          "position-negative control")


if __name__ == "__main__":
    main()
