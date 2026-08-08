#!/usr/bin/env python3
"""Exact FLY_INTO_WALL damage routing against Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]


def cases():
    rows = (
        (4.0, 20.0, 0.0, 0, 0.0, -1,
         (0, 0, 0, 0)),
        (8.5, 20.0, 0.0, 0, 0.0, -1,
         (1, 2, 3, 4)),
        (8.5, 20.0, 0.0, 0, 0.0, 0,
         (1, 2, 3, 4)),
        (8.5, 20.0, 2.0, 0, 0.0, 1,
         (4, 4, 4, 4)),
        (5.0, 20.0, 10.0, 0, 0.0, -1,
         (0, 0, 0, 0)),
        (5.0, 20.0, 0.0, 15, 5.0, -1,
         (0, 0, 0, 0)),
        (8.0, 20.0, 0.0, 15, 5.0, -1,
         (0, 0, 0, 0)),
        (8.0, 20.0, 1.0, 15, 5.0, 0,
         (2, 0, 1, 0)),
    )
    damage_sets = ((0, 0, 0, 0), (3, 7, 11, 5))
    serial = 0
    for row in rows:
        for damage in damage_sets:
            amount, health, absorption, hurt, last, resistance, prot = row
            yield {
                "name": f"case_{serial}", "amount": amount,
                "health": health, "absorption": absorption,
                "hurt_resistant_time": hurt, "last_damage": last,
                "resistance": resistance, "protection": prot,
                "damage": damage,
            }
            serial += 1


def native(case):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_elytra_wall_damage_oracle"),
        str(case["amount"]), str(case["health"]),
        str(case["absorption"]), str(case["hurt_resistant_time"]),
        str(case["last_damage"]), str(case["resistance"]),
        *(str(value) for value in case["protection"]),
        *(str(value) for value in case["damage"]),
    ], text=True)
    return json.loads(raw)


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
        "game/test_elytra_wall_damage_oracle",
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
            java = request(args.port, "elytra_wall_damage_locked", case)
            cpu = native(case)
            if java != cpu:
                mismatch = [key for key in sorted(set(java) | set(cpu))
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"{case['name']}: {mismatch}: "
                    f"Java={java!r} CPU={cpu!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} Elytra wall damage cases")


if __name__ == "__main__":
    main()
