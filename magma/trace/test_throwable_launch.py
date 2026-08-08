#!/usr/bin/env python3
"""Bit-exact Java/native player throwable construction oracle."""

import argparse
import json
import pathlib
import subprocess
import sys


JAVA = pathlib.Path(__file__).resolve().parents[2] / "java"
sys.path.insert(0, str(JAVA))
from qrl_client import NetheriteEnv  # noqa: E402


CASES = [
    ("egg", 344, 0, 17, 29, 710001, 8.5, 80.0, 8.5,
     0.0, 0.0, 0.0, 0.0, 0.0, True),
    ("snowball", 332, 0, 117, 229, 710101, -14.25, 44.5, 91.75,
     0.125, -0.25, 0.375, 137.25, -31.5, False),
    ("xp_bottle", 384, 0, 9917, 8229, 710201, 128.125, 12.0, -83.5,
     -0.5, 0.75, -0.125, -73.0, 62.25, False),
    ("ender_pearl", 368, 0, 65537, 131071, 710301, 0.25, 220.0, 0.75,
     0.03125, 1.5, -0.0625, 179.9, -89.0, True),
    ("splash_potion", 438, 0, 1234567, 2345678, 710401,
     1.25, 64.0, -2.5, 0.2, -0.1, 0.4, 45.5, 18.25, False),
    ("lingering_potion", 441, 0, 9876543, 8765432, 710501,
     -1.25, 96.0, 2.5, -0.2, 0.1, -0.4, -135.5, -48.25, True),
]

TICK_CASES = [
    ("egg", 344, 0, 8181, 9191, 720001, -150.0, 220.0, -150.0,
     0.125, -0.05, 0.25, 12.5, -18.0, True, 1),
    ("snowball", 332, 0, 8182, 9192, 720101, -150.0, 220.0, -150.0,
     -0.125, 0.2, 0.375, -82.0, 21.5, False, 7),
    ("xp_bottle", 384, 0, 8183, 9193, 720201, -150.0, 220.0, -150.0,
     0.0, 0.0, 0.0, 133.0, -36.0, True, 11),
    ("ender_pearl", 368, 0, 8184, 9194, 720301, -150.0, 220.0, -150.0,
     0.2, 0.1, -0.2, -147.0, 42.0, False, 19),
    ("splash_potion", 438, 0, 8185, 9195, 720401,
     -150.0, 220.0, -150.0, 0.0, 0.3, 0.0, 27.0, 58.0, True, 13),
    ("lingering_potion", 441, 0, 8186, 9196, 720501,
     -150.0, 220.0, -150.0, -0.1, -0.2, 0.15,
     178.0, -71.0, False, 17),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()
    env = NetheriteEnv(args.host, args.port)
    locked = env._cmd({"cmd": "server_step_lock"})
    if not locked.get("ok"):
        raise RuntimeError(f"server lock failed: {locked}")
    try:
        for case in CASES:
            (kind, item, meta, entity_seed, uuid_seed, next_id,
             x, y, z, vx, vy, vz, yaw, pitch, on_ground) = case
            java = env._cmd({
                "cmd": "throwable_launch_locked",
                "action": {
                    "kind": kind,
                    "entity_seed48": entity_seed,
                    "server_uuid_seed48": uuid_seed,
                    "next_entity_id": next_id,
                    "x": x, "y": y, "z": z,
                    "vx": vx, "vy": vy, "vz": vz,
                    "yaw": yaw, "pitch": pitch,
                    "on_ground": on_ground,
                },
            })
            if not java.get("ok"):
                raise AssertionError(java)
            native = subprocess.run([
                str(args.native.resolve()), str(entity_seed), str(uuid_seed),
                str(next_id), str(x), str(y), str(z), str(vx), str(vy),
                str(vz), str(yaw), str(pitch), str(int(on_ground)),
                str(item), str(meta),
            ], check=True, capture_output=True, text=True)
            actual = json.loads(native.stdout)
            if actual != java:
                keys = sorted(key for key in set(actual) | set(java)
                              if actual.get(key) != java.get(key))
                raise AssertionError(
                    f"{kind} launch mismatch in {keys}\n"
                    f"Java:  {java}\nNative: {actual}")
        for case in TICK_CASES:
            (kind, item, meta, entity_seed, uuid_seed, next_id,
             x, y, z, vx, vy, vz, yaw, pitch, on_ground, ticks) = case
            java = env._cmd({
                "cmd": "throwable_launch_locked",
                "action": {
                    "kind": kind,
                    "entity_seed48": entity_seed,
                    "server_uuid_seed48": uuid_seed,
                    "next_entity_id": next_id,
                    "x": x, "y": y, "z": z,
                    "vx": vx, "vy": vy, "vz": vz,
                    "yaw": yaw, "pitch": pitch,
                    "on_ground": on_ground,
                    "ticks": ticks,
                },
            })
            if not java.get("ok"):
                raise AssertionError(java)
            native = subprocess.run([
                str(args.native.resolve()), str(entity_seed), str(uuid_seed),
                str(next_id), str(x), str(y), str(z), str(vx), str(vy),
                str(vz), str(yaw), str(pitch), str(int(on_ground)),
                str(item), str(meta), str(ticks),
            ], check=True, capture_output=True, text=True)
            actual = json.loads(native.stdout)
            if actual != java:
                keys = sorted(key for key in set(actual) | set(java)
                              if actual.get(key) != java.get(key))
                raise AssertionError(
                    f"{kind} {ticks}-tick mismatch in {keys}\n"
                    f"Java:  {java}\nNative: {actual}")
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")
    print("PASS real Java/native: "
          f"{len(CASES)} launches + {len(TICK_CASES)} continuations")


if __name__ == "__main__":
    main()
