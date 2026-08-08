#!/usr/bin/env python3
"""Exact indirect-magic potion damage comparison against MC 1.11.2."""

import argparse
import json
import pathlib
import subprocess

from test_dragon_crystal_notification import request


CASES = (
    ("pig_player_fresh_ground", "pig", "player", 10.0, 0, 0.0,
     True, 17, 29, 0, 0.5),
    ("pig_player_fresh_air", "pig", "player", 10.0, 0, 0.0,
     False, 31, 43, 0, 0.5),
    ("pig_player_resisted_equal", "pig", "player", 10.0, 20, 3.0,
     True, 47, 59, 0, 0.5),
    ("pig_player_resistant_delta", "pig", "player", 10.0, 20, 2.0,
     True, 61, 71, 0, 0.5),
    ("pig_ownerless_math_yaw", "pig", "none", 10.0, 0, 0.0,
     True, 73, 5582, 0, 0.5),
    ("pig_cow_owner", "pig", "cow", 10.0, 0, 0.0,
     True, 83, 97, 0, 0.5),
    ("pig_strong_scaled", "pig", "player", 10.0, 0, 0.0,
     True, 101, 103, 1, 0.75),
    ("zombie_reversed_heal", "zombie", "player", 10.0, 0, 0.0,
     True, 107, 109, 0, 0.5),
    ("witch_player_magic_resistance", "witch", "player", 26.0,
     0, 0.0, True, 113, 127, 0, 0.5),
    ("witch_ownerless_magic_resistance", "witch", "none", 26.0,
     0, 0.0, True, 131, 5582, 0, 0.5),
    ("player_cow_fresh_ground", "player", "cow", 20.0,
     0, 0.0, True, 137, 149, 0, 0.5),
    ("player_ownerless_math_yaw", "player", "none", 20.0,
     0, 0.0, True, 151, 5582, 0, 0.5),
    ("player_cow_resisted_equal", "player", "cow", 20.0,
     20, 3.0, True, 157, 163, 0, 0.5),
    ("player_cow_resistant_delta", "player", "cow", 20.0,
     20, 2.0, False, 167, 173, 0, 0.5),
    ("player_self_fresh_ground", "player", "player", 20.0,
     0, 0.0, True, 179, 181, 0, 0.5),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25699)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()
    request(args.port, "server_step_lock")
    try:
        for (name, target, owner, health, hurt_resistant, last_damage,
             on_ground, target_seed, math_seed, amplifier, factor) in CASES:
            action = {
                "target": target,
                "owner": owner,
                "health": health,
                "hurt_resistant_time": hurt_resistant,
                "last_damage": last_damage,
                "on_ground": on_ground,
                "target_seed48": target_seed,
                "math_seed48": math_seed,
                "amplifier": amplifier,
                "factor": factor,
            }
            java = request(args.port, "potion_owner_damage_locked", action)
            raw = subprocess.check_output([
                str(args.native.resolve()), target, owner, repr(health),
                str(hurt_resistant), repr(last_damage), str(int(on_ground)),
                str(target_seed), str(math_seed), str(amplifier), repr(factor),
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
    print(f"PASS real Java/native: {len(CASES)} exact potion owner hits")


if __name__ == "__main__":
    main()
