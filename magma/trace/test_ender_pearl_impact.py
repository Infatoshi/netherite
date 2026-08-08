#!/usr/bin/env python3
"""Exact ordinary EntityEnderPearl impact comparison against MC 1.11.2."""

import argparse
import json
import pathlib
import subprocess

from test_dragon_crystal_notification import request


CASES = (
    ("fresh", 17, 29, 43, 59, 71, 5000, False,
     20.0, 0, 0.0, 28.125, 224.75, 21.5),
    ("endermite", 5, 73, 83, 89, 97, 5100, True,
     20.0, 0, 0.0, 27.75, 226.125, 22.25),
    ("spawn_disabled", 5, 101, 103, 107, 109, 5200, False,
     18.5, 10, 0.0, 26.5, 222.25, 20.75),
    ("stronger_resistant", 113, 127, 131, 137, 139, 5300, False,
     17.0, 20, 2.0, 29.25, 228.5, 23.125),
    ("equal_resisted", 149, 151, 157, 163, 167, 5400, False,
     16.0, 20, 5.0, 25.625, 221.5, 30.25),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25699)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()
    request(args.port, "server_step_lock")
    try:
        for case in CASES:
            (name, pearl_seed, player_seed, math_seed, entity_seed,
             uuid_seed, next_id, mob_spawning, health, hurt_resistant,
             last_damage, x, y, z) = case
            action = {
                "pearl_seed48": pearl_seed,
                "player_seed48": player_seed,
                "math_seed48": math_seed,
                "entity_seed48": entity_seed,
                "server_uuid_seed48": uuid_seed,
                "next_entity_id": next_id,
                "do_mob_spawning": mob_spawning,
                "health": health,
                "hurt_resistant_time": hurt_resistant,
                "last_damage": last_damage,
                "x": x, "y": y, "z": z,
            }
            java = request(args.port, "ender_pearl_impact_locked", action)
            raw = subprocess.check_output([
                str(args.native.resolve()), str(pearl_seed),
                str(player_seed), str(math_seed), str(entity_seed),
                str(uuid_seed), str(next_id), str(int(mob_spawning)),
                repr(health), str(hurt_resistant), repr(last_damage),
                repr(x), repr(y), repr(z),
            ], text=True)
            native = json.loads(raw)
            if java != native:
                mismatch = {
                    key: [java.get(key), native.get(key)]
                    for key in sorted(set(java) | set(native))
                    if java.get(key) != native.get(key)
                }
                raise AssertionError(f"{name}: {mismatch}")
    finally:
        request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(CASES)} exact Ender Pearl impacts")


if __name__ == "__main__":
    main()
