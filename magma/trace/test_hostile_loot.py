#!/usr/bin/env python3
"""Bit-compare represented hostile loot tables with real 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
TARGETS = [
    ("zombie", 1), ("zombie_villager", 1), ("pigman", 1),
    ("skeleton", 1), ("wither_skeleton", 1), ("creeper", 1),
    ("spider", 1), ("cave_spider", 1), ("enderman", 1),
    ("blaze", 1), ("ghast", 1), ("silverfish", 1),
    ("endermite", 1),
    ("giant", 1),
    ("husk", 1),
    ("stray", 1),
    ("polar_bear", 1),
    ("rabbit", 1),
    ("slime", 1), ("slime", 4),
    ("magma_cube", 1), ("magma_cube", 4),
    ("vindicator", 1), ("evoker", 1),
    ("snowman", 1),
]
SEEDS = [0, 2, 95, 402, (1 << 48) - 1]


def native(case):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_hostile_loot_oracle"),
        case["target"], str(case["size"]), str(case["looting"]),
        str(int(case["killed_by_player"])), str(case["entity_seed48"]),
    ], text=True)
    return json.loads(raw)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    cases = []
    for target, size in TARGETS:
        for seed in SEEDS:
            for looting, killed in ((0, True), (3, True), (0, False)):
                name = f"{target}_{size}_{seed}_{looting}_{int(killed)}"
                if not args.case or args.case == name:
                    cases.append((name, {
                        "target": target, "size": size,
                        "entity_seed48": seed, "looting": looting,
                        "killed_by_player": killed,
                    }))
    if not cases:
        parser.error(f"unknown case: {args.case}")
    subprocess.run([
        "make", "-C", str(MAGMA), "game/test_hostile_loot_oracle",
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
        for name, case in cases:
            java = request(args.port, "hostile_loot_locked", case)
            cpu = native(case)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"{name}: {mismatch}: Java={java!r} CPU={cpu!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/shared CPU: {len(cases)} exact hostile loot rows")


if __name__ == "__main__":
    main()
