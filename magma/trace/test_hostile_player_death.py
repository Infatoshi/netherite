#!/usr/bin/env python3
"""Exact real-1.11.2 comparison of composed hostile player deaths."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
TARGETS = (
    ("sheep", 1), ("pig", 1), ("cow", 1), ("chicken", 1),
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
    ("villager", 1),
    ("slime", 1), ("slime", 2), ("slime", 4),
    ("magma_cube", 1), ("magma_cube", 2), ("magma_cube", 4),
)
SEEDS = (0, 402, (1 << 48) - 1)
MODES = ((0, True), (3, True), (3, False))
KEYS = (
    "ok", "target", "after_bits", "hurt_time", "held_count",
    "held_damage", "drops", "death_time", "is_dead",
    "entity_seed48", "math_seed48", "next_entity_id", "do_mob_loot",
)


def native(case):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_hostile_player_death_oracle"),
        case["target"], str(case["target_size"]),
        str(case["target_seed48"]),
        str(case["math_seed48"]), str(case["next_entity_id"]),
        str(case["enchant_level"]), str(int(case["do_mob_loot"])),
    ], text=True)
    return json.loads(raw)


def exact_subset(row):
    return {key: row[key] for key in KEYS}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    cases = []
    serial = 0
    for target, size in TARGETS:
        for seed in SEEDS:
            for looting, do_mob_loot in MODES:
                name = (f"{target}_{size}_{seed}_{looting}_"
                        f"{int(do_mob_loot)}")
                if not args.case or args.case == name:
                    cases.append((name, {
                        "target": target,
                        "target_size": size,
                        "target_seed48": seed,
                        "math_seed48": 67890 + serial * 101,
                        "next_entity_id": 1000 + serial * 8,
                        "held_item": 276,
                        "enchant_id": 21 if looting else 0,
                        "enchant_level": looting,
                        "cooldown_ticks": 5,
                        "on_ground": True,
                        "fall_distance": 0.0,
                        "target_health": 1.0,
                        "capture_death": True,
                        "do_mob_loot": do_mob_loot,
                    }))
                serial += 1
    if not cases:
        parser.error(f"unknown case: {args.case}")
    subprocess.run([
        "make", "-C", str(MAGMA),
        "game/test_hostile_player_death_oracle",
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
        mode = request(args.port, "runcmds", {
            "cmds": ["gamemode survival @p"],
        })
        if not mode.get("ok") or mode.get("failed"):
            raise AssertionError(f"could not force survival mode: {mode}")
        request(args.port, "server_step_lock")
        locked = True
        for name, case in cases:
            java = exact_subset(request(
                args.port, "player_critical_locked", case))
            cpu = native(case)
            if java != cpu:
                mismatch = [key for key in KEYS
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"{name}: {mismatch}: Java={java!r} CPU={cpu!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/shared CPU: {len(cases)} exact composed living deaths")


if __name__ == "__main__":
    main()
