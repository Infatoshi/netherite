#!/usr/bin/env python3
"""Real-server proof that throwables tick while the player is elsewhere."""

import argparse
import json
import pathlib
import struct
import subprocess
import sys


JAVA = pathlib.Path(__file__).resolve().parents[2] / "java"
sys.path.insert(0, str(JAVA))
from qrl_client import NetheriteEnv  # noqa: E402


CASES = (
    ("egg", "EntityEgg", 7, 771001, 0x123450000001,
     0.25, 0.1, -0.125, -130.0, -33.0, 7),
    ("snowball", "EntitySnowball", 8, 771002, 0x123450000002,
     -0.125, 0.25, 0.375, -58.75, -14.5, 8),
    ("xp_bottle", "EntityExpBottle", 9, 771003, 0x123450000003,
     0.375, -0.05, 0.125, 12.5, 4.0, 9),
    ("ender_pearl", "EntityEnderPearl", 12, 771004, 0x123450000004,
     -0.25, 0.175, -0.25, 83.75, 22.5, 10),
)


def dbits(value):
    return struct.pack(">d", float(value)).hex()


def fbits(value):
    return struct.pack(">f", float(value)).hex()


def java_result(entity, native_type):
    return {
        "ok": True,
        "eid": entity["eid"],
        "type": native_type,
        "position_bits": [dbits(entity[key]) for key in ("x", "y", "z")],
        "motion_bits": [dbits(entity[key]) for key in ("vx", "vy", "vz")],
        "rotation_bits": [
            fbits(entity[key])
            for key in ("yaw", "pitch", "prev_yaw", "prev_pitch")
        ],
        "seed48": entity["entity_seed48"],
        "ticks_existed": entity["age"],
        "ticks_in_air": entity["ticks_in_air"],
        "portal_counter": entity["portal_counter"],
        "portal_cooldown": entity["portal_cooldown"],
        "dead": False,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()
    env = NetheriteEnv(args.host, args.port)
    initial = env.reset({
        "seed": 708,
        "mode": "survival",
        "type": "flat",
        "structures": False,
    })
    if not initial.get("ok"):
        raise RuntimeError(f"oracle reset failed: {initial}")
    locked = env._cmd({"cmd": "server_step_lock"})
    if not locked.get("ok"):
        raise RuntimeError(f"server lock failed: {locked}")
    try:
        source_dimension = int(locked["authoritative"]["dim"])
        if source_dimension != 0:
            moved = env._cmd({
                "cmd": "transfer_dimension_locked", "action": {"id": 0},
            })
            if not moved.get("ok"):
                raise AssertionError(moved)
            source_dimension = 0
        player_dimension = -1
        cleared = env._cmd({"cmd": "clear_entities_locked", "action": {}})
        if not cleared.get("ok"):
            raise AssertionError(cleared)
        # Overworld spawn chunks remain tickable after the player leaves.
        # reset() reports that new world's spawn-side player location, while
        # the parked server player may still be completing the prior client
        # handoff when the lock is first acquired.
        base_x = float(initial["x"]) + 32.0
        base_z = float(initial["z"]) + 32.0
        staged = {}
        for index, case in enumerate(CASES):
            (kind, java_type, native_type, eid, seed48,
             vx, vy, vz, yaw, pitch, age) = case
            x = base_x + index * 8.0
            y = 220.0
            z = base_z
            result = env._cmd({
                "cmd": "summon_locked",
                "action": {
                    "type": kind,
                    "eid": eid,
                    "uuid": f"00000000-0000-4000-8000-{eid:012d}",
                    "entity_seed48": seed48,
                    "x": x, "y": y, "z": z,
                    "mx": vx, "my": vy, "mz": vz,
                    "yaw": yaw, "pitch": pitch,
                    "ticks_existed": age,
                    "ticks_in_air": age,
                },
            })
            if not result.get("ok"):
                raise AssertionError(result)
            entity = next(
                value for value in result["authoritative"]["entities"]
                if value.get("eid") == eid)
            staged[eid] = (entity, native_type, seed48, x, y, z,
                           vx, vy, vz, yaw, pitch, age)
        moved = env._cmd({
            "cmd": "transfer_dimension_locked",
            "action": {"id": player_dimension},
        })
        if not moved.get("ok"):
            raise AssertionError(moved)
        stepped = env._cmd({"cmd": "server_tick_locked", "action": {}})
        if not stepped.get("ok"):
            raise AssertionError(stepped)
        returned = env._cmd({
            "cmd": "transfer_dimension_locked",
            "action": {"id": source_dimension},
        })
        if not returned.get("ok"):
            raise AssertionError(returned)
        by_eid = {
            entity["eid"]: entity
            for entity in returned["authoritative"]["entities"]
        }
        for eid, values in staged.items():
            (before, native_type, seed48, x, y, z,
             vx, vy, vz, yaw, pitch, age) = values
            entity = by_eid.get(eid)
            if entity is None or entity.get("throwable_exact") is not True:
                raise AssertionError(
                    f"missing exact foreign throwable {eid}: {entity}")
            expected = java_result(entity, native_type)
            native = subprocess.run([
                str(args.native.resolve()),
                str(source_dimension), str(player_dimension), str(eid),
                str(native_type), str(seed48),
                str(x), str(y), str(z), str(vx), str(vy), str(vz),
                str(yaw), str(pitch), str(before["prev_yaw"]),
                str(before["prev_pitch"]), str(age), str(age),
            ], check=True, capture_output=True, text=True)
            actual = json.loads(native.stdout)
            if actual != expected:
                keys = sorted(
                    key for key in set(actual) | set(expected)
                    if actual.get(key) != expected.get(key))
                raise AssertionError(
                    f"foreign {entity['type']} mismatch in {keys}\n"
                    f"Java:  {expected}\nNative: {actual}")
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")
    print("PASS real Java/native: 4 foreign-dimension throwable ticks")


if __name__ == "__main__":
    main()
