#!/usr/bin/env python3
"""Exact EntityExpBottle impact comparison against Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]


def native(case):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_xp_bottle_impact_oracle"),
        str(case["world_seed48"]), str(case["math_seed48"]),
        str(case["next_entity_id"]), repr(case["x"]),
        repr(case["y"]), repr(case["z"]),
    ], text=True)
    return json.loads(raw)


def cases():
    positions = (
        (0.0, 1.0, 0.0),
        (0.375, 1.875, -0.375),
        (-0.125, 0.5, 0.125),
    )
    for serial in range(24):
        x, y, z = positions[serial % len(positions)]
        yield {
            "name": f"seed_{serial}",
            "world_seed48": (serial * 32452843 + 7) & ((1 << 48) - 1),
            "math_seed48": (serial * 49979687 + 19) & ((1 << 48) - 1),
            "next_entity_id": 730000 + serial * 8,
            "x": x, "y": y, "z": z,
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    selected = [case for case in cases()
                if not args.case or case["name"] == args.case]
    if not selected:
        parser.error(f"unknown case: {args.case}")
    subprocess.run([
        "make", "-C", str(MAGMA),
        "game/test_xp_bottle_impact_oracle",
    ], check=True, stdout=subprocess.DEVNULL)
    locked = False
    try:
        deadline = time.monotonic() + 120.0
        while True:
            try:
                request(args.port, "obs")
                break
            except (OSError, RuntimeError, ValueError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.5)
        request(args.port, "server_step_lock")
        locked = True
        for case in selected:
            java = request(args.port, "xp_bottle_impact_locked", case)
            cpu = native(case)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"{case['name']}: {mismatch}: "
                    f"Java={java!r} CPU={cpu!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} exact XP bottle impacts")


if __name__ == "__main__":
    main()
