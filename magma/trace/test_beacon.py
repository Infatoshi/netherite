#!/usr/bin/env python3
"""Bit-exact Java/native Beacon state, effects, beam, and NBT gate."""

import argparse
import json
import pathlib
import subprocess
import sys


JAVA = pathlib.Path(__file__).resolve().parents[2] / "java"
sys.path.insert(0, str(JAVA))
from qrl_client import NetheriteEnv  # noqa: E402


CASES = (
    {"pyramid_levels": 0, "primary": 1, "secondary": 0},
    {"pyramid_levels": 1, "primary": 1, "secondary": 0},
    {"pyramid_levels": 2, "primary": 3, "secondary": 0},
    {"pyramid_levels": 3, "primary": 5, "secondary": 10},
    {"pyramid_levels": 4, "primary": 1, "secondary": 1},
    {"pyramid_levels": 4, "primary": 5, "secondary": 10},
    {
        "pyramid_levels": 4, "primary": 1, "secondary": 0,
        "glass": [{"meta": 1}, {"meta": 11, "pane": True}],
    },
    {
        "pyramid_levels": 4, "primary": 1, "secondary": 0,
        "obstruction": 1, "glass": [{"meta": 14}],
    },
    {
        "pyramid_levels": 4, "primary": 11, "secondary": 0,
        "obstruction": 7, "glass": [{"meta": 15, "pane": True}],
    },
    {
        "pyramid_levels": 4, "primary": 1, "secondary": 0,
        "player_dx": 53.0, "player_dz": 0.5,
    },
    {"pyramid_levels": 4, "primary": 2, "secondary": 10},
    {
        "pyramid_levels": 4, "primary": 3, "secondary": 0,
        "glass": [
            {"meta": 14}, {"meta": 14, "pane": True}, {"meta": 0},
        ],
    },
)


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
    negative_checked = False
    try:
        if int(locked["authoritative"]["dim"]) != 0:
            moved = env._cmd({
                "cmd": "transfer_dimension_locked", "action": {"id": 0},
            })
            if not moved.get("ok"):
                raise AssertionError(moved)
        for index, action in enumerate(CASES):
            expected = env._cmd({"cmd": "beacon_locked", "action": action})
            if not expected.get("ok"):
                raise AssertionError(expected)
            actual = json.loads(subprocess.run([
                str(args.native.resolve()), str(index),
            ], check=True, capture_output=True, text=True).stdout)
            if actual != expected:
                keys = sorted(
                    key for key in set(actual) | set(expected)
                    if actual.get(key) != expected.get(key)
                )
                raise AssertionError(
                    f"Beacon case {index} mismatch in {keys}\n"
                    f"Java:  {expected}\nNative: {actual}"
                )
            if not negative_checked:
                sabotaged = json.loads(json.dumps(actual))
                sabotaged["segments"][0]["height"] += 1
                if sabotaged == expected:
                    raise AssertionError("Beacon segment sabotage escaped")
                negative_checked = True
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")
    print("PASS real Java/native: 12 Beacon pyramid, beam, effect, payment, "
          "and NBT cases plus segment-negative control")


if __name__ == "__main__":
    main()
