#!/usr/bin/env python3
"""Exact EntitySnowball living-impact comparison against MC 1.11.2."""

import argparse
import json
import pathlib
import subprocess

from test_dragon_crystal_notification import request


CASES = (
    ("pig_fresh_ground", "pig", 10.0, 0, 0.0, True, 17, 29),
    ("pig_fresh_air", "pig", 7.5, 10, 0.0, False, 31, 43),
    ("pig_resisted", "pig", 10.0, 20, 0.0, True, 47, 59),
    ("blaze_fresh_ground", "blaze", 20.0, 0, 0.0, True, 61, 71),
    ("blaze_fresh_air", "blaze", 12.5, 10, 0.0, False, 73, 83),
    ("blaze_resistant_accept", "blaze", 20.0, 20, 0.0,
     True, 89, 97),
    ("blaze_resisted_equal", "blaze", 20.0, 20, 3.0,
     True, 101, 103),
    ("player_self_fresh_ground", "player", 20.0, 0, 0.0,
     True, 107, 109),
    ("player_self_fresh_air", "player", 15.5, 10, 0.0,
     False, 113, 127),
    ("player_self_resisted", "player", 20.0, 20, 0.0,
     True, 131, 137),
)

SNOWMAN_OWNER_CASES = (
    ("snowman_zombie_fresh_ground", "zombie", 20.0, 0, 0.0,
     True, 149, 151),
    ("snowman_zombie_fresh_air", "zombie", 17.5, 10, 0.0,
     False, 157, 163),
    ("snowman_zombie_resisted", "zombie", 20.0, 20, 0.0,
     True, 167, 173),
    ("snowman_blaze_fresh", "blaze", 20.0, 0, 0.0,
     True, 179, 181),
    ("snowman_blaze_resisted_equal", "blaze", 20.0, 20, 3.0,
     True, 191, 193),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25699)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()
    request(args.port, "server_step_lock")
    try:
        for (name, target, health, hurt_resistant, last_damage,
             on_ground, target_seed, math_seed) in CASES:
            action = {
                "target": target,
                "health": health,
                "hurt_resistant_time": hurt_resistant,
                "last_damage": last_damage,
                "on_ground": on_ground,
                "target_seed48": target_seed,
                "math_seed48": math_seed,
            }
            java = request(args.port, "snowball_impact_locked", action)
            raw = subprocess.check_output([
                str(args.native.resolve()), target, repr(health),
                str(hurt_resistant), repr(last_damage),
                str(int(on_ground)), str(target_seed), str(math_seed),
            ], text=True)
            native = json.loads(raw)
            if java != native:
                mismatch = {
                    key: [java.get(key), native.get(key)]
                    for key in sorted(set(java) | set(native))
                    if java.get(key) != native.get(key)
                }
                raise AssertionError(f"{name}: {mismatch}")
        for (name, target, health, hurt_resistant, last_damage,
             on_ground, target_seed, math_seed) in SNOWMAN_OWNER_CASES:
            action = {
                "target": target,
                "thrower": "snowman",
                "health": health,
                "hurt_resistant_time": hurt_resistant,
                "last_damage": last_damage,
                "on_ground": on_ground,
                "target_seed48": target_seed,
                "math_seed48": math_seed,
            }
            java = request(args.port, "snowball_impact_locked", action)
            raw = subprocess.check_output([
                str(args.native.resolve()), f"snowman_{target}",
                repr(health), str(hurt_resistant), repr(last_damage),
                str(int(on_ground)), str(target_seed), str(math_seed),
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
    print(f"PASS real Java/native: {len(CASES) + len(SNOWMAN_OWNER_CASES)} "
          "exact player and Snow Golem-owned snowball impacts")


if __name__ == "__main__":
    main()
