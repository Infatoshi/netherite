#!/usr/bin/env python3
"""Exact EntityEnderPearl End Gateway comparison against MC 1.11.2."""

import argparse
import json
import pathlib
import subprocess

from test_dragon_crystal_notification import request


CASES = (
    ("exact_positive", 17, 120, 87, -45, 0),
    ("exact_negative", 31, -340, 64, 275, 0),
    ("cooling", 47, 900, 111, -700, 7),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25699)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()
    request(args.port, "server_step_lock")
    try:
        for name, seed, exit_x, exit_y, exit_z, cooldown in CASES:
            action = {
                "pearl_seed48": seed,
                "exit_x": exit_x, "exit_y": exit_y, "exit_z": exit_z,
                "cooldown": cooldown,
            }
            java = request(args.port, "ender_pearl_gateway_locked", action)
            native = json.loads(subprocess.check_output([
                str(args.native.resolve()), str(seed), str(exit_x),
                str(exit_y), str(exit_z), str(cooldown),
            ], text=True))
            if java != native:
                mismatch = {
                    key: [java.get(key), native.get(key)]
                    for key in sorted(set(java) | set(native))
                    if java.get(key) != native.get(key)
                }
                raise AssertionError(f"{name}: {mismatch}")
    finally:
        request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(CASES)} Ender Pearl gateway impacts")


if __name__ == "__main__":
    main()
