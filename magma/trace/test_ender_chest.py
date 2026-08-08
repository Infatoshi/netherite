#!/usr/bin/env python3
"""Bit-exact Java/native Ender Chest activation and lid gate."""

import argparse
import json
import pathlib
import subprocess
import sys


JAVA = pathlib.Path(__file__).resolve().parents[2] / "java"
sys.path.insert(0, str(JAVA))
from qrl_client import NetheriteEnv  # noqa: E402


CASES = ((1, 1), (10, 6), (17, 17))
WORLD_SEED = 0x23456789ABCD
DISPLAY_SEED = 0x13579BDF2468


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
        for open_ticks, close_ticks in CASES:
            action = {
                "open_ticks": open_ticks, "close_ticks": close_ticks,
                "world_seed48": WORLD_SEED,
                "display_seed48": DISPLAY_SEED,
            }
            expected = env._cmd({
                "cmd": "ender_chest_locked", "action": action,
            })
            if not expected.get("ok"):
                raise AssertionError(expected)
            actual = json.loads(subprocess.run([
                str(args.native.resolve()), str(open_ticks), str(close_ticks),
                str(WORLD_SEED), str(DISPLAY_SEED),
            ], check=True, capture_output=True, text=True).stdout)
            if actual != expected:
                keys = sorted(
                    key for key in set(actual) | set(expected)
                    if actual.get(key) != expected.get(key))
                raise AssertionError(
                    f"Ender Chest {open_ticks}/{close_ticks} mismatch "
                    f"in {keys}\nJava:  {expected}\nNative: {actual}")
            if not negative_checked:
                sabotaged = dict(actual)
                sabotaged["close_sync_ticks"] += 1
                if sabotaged == expected:
                    raise AssertionError("Ender Chest sabotage escaped")
                negative_checked = True
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")
    print("PASS real Java/native: Ender Chest blocking, container, inventory, "
          "lid ticks, sounds, display particles, RNG cursors, and negative control")


if __name__ == "__main__":
    main()
