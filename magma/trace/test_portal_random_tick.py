#!/usr/bin/env python3
"""Bit-exact Java/native BlockPortal random-tick pigman spawn seam."""

import argparse
import json
import pathlib
import subprocess
import sys


JAVA = pathlib.Path(__file__).resolve().parents[2] / "java"
ROOT = JAVA.parent
sys.path.insert(0, str(JAVA))
from qrl_client import NetheriteEnv  # noqa: E402


ENTITY_SEED = 0x23456789ABCD
UUID_SEED = 0x123456789ABC
MATH_SEED = 0x3456789ABCDE
NEXT_ID = 760000
CASES = (
    (0, 0, 0, True, None, None, "immediate support, baby"),
    (1, 0, 1816, True, None, None, "deep support, adult"),
    (2, 0, 1, True, None, None, "rare roll misses"),
    (3, 0, 0, False, None, None, "mob spawning disabled"),
    (4, 1, 734846, True, None, None,
     "baby mounts existing chicken"),
    (5, 2, 767451, True, None, None, "baby creates chicken jockey"),
    (6, 2, 767451, True, 1, None,
     "new chicken jockey first continuation"),
    (7, 2, 767451, True, 2, None,
     "new chicken jockey second continuation"),
    (8, 2, 767451, True, 5, 2,
     "new chicken jockey checkpoint continuation"),
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
        for (fixture, jockey_fixture, world_seed,
             mob_spawning, continuation_ticks, checkpoint_tick,
             label) in CASES:
            action = {
                "case": fixture,
                "jockey_fixture": jockey_fixture,
                "world_seed48": world_seed,
                "entity_seed48": ENTITY_SEED,
                "server_uuid_seed48": UUID_SEED,
                "math_seed48": MATH_SEED,
                "next_entity_id": NEXT_ID,
                "mob_spawning": mob_spawning,
            }
            if continuation_ticks is not None:
                action["continuation_ticks"] = continuation_ticks
            expected = env._cmd({
                "cmd": "portal_random_tick_locked", "action": action,
            })
            if not expected.get("ok"):
                raise AssertionError(expected)
            native_command = [
                str(args.native.resolve()), str(fixture),
                str(jockey_fixture), str(world_seed),
                str(ENTITY_SEED), str(UUID_SEED), str(MATH_SEED),
                str(NEXT_ID), "1" if mob_spawning else "0",
            ]
            if continuation_ticks is not None:
                native_command.append(str(continuation_ticks))
            if checkpoint_tick is not None:
                checkpoint_path = (
                    ROOT / ".tmp" / "portal_random_tick_checkpoint.bin")
                native_command.extend([
                    str(checkpoint_tick), str(checkpoint_path),
                ])
            actual = json.loads(subprocess.run(
                native_command, check=True, capture_output=True,
                text=True).stdout)
            if actual != expected:
                keys = sorted(
                    key for key in set(actual) | set(expected)
                    if actual.get(key) != expected.get(key))
                raise AssertionError(
                    f"{label} mismatch in {keys}\n"
                    f"Java:  {expected}\nNative: {actual}")
            if actual["spawned"] and not negative_checked:
                sabotaged = json.loads(json.dumps(actual))
                sabotaged["portal_cooldown"] -= 1
                if sabotaged == expected:
                    raise AssertionError("portal cooldown sabotage escaped")
                negative_checked = True
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")
    print("PASS real Java/native: portal random-tick adult, baby, both "
          "chicken-jockey branches, miss, gamerule suppression, five-tick "
          "continuation, tick-two native checkpoint/reload, and "
          "cooldown-negative control")


if __name__ == "__main__":
    main()
