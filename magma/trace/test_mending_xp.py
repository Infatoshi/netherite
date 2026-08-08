#!/usr/bin/env python3
"""Compare EntityXPOrb Mending pickup to real Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
NAMES = ("mainhand", "offhand", "feet", "legs", "chest", "head")


def stack(item=0, meta=0, mending=False):
    value = {"item": item, "count": 1 if item else 0, "meta": meta}
    if mending:
        value["enchants"] = [{"id": 70, "level": 1}]
    return value


BASE_CASES = [
    ("no_mending", 5, 0, 0, 0x123456789ABC, {}),
    ("main_even_repair", 5, 0, 0, 0, {
        "mainhand": stack(276, 8, True),
    }),
    ("main_odd_repair", 1, 0, 0, 1, {
        "mainhand": stack(276, 1, True),
    }),
    ("main_partial_repair", 5, 0, 0, 2, {
        "mainhand": stack(276, 15, True),
    }),
    ("undamaged_candidate", 7, 0, 0, 3, {
        "mainhand": stack(276, 0, True),
    }),
    ("cooldown_blocks", 5, 1, 0, 4, {
        "mainhand": stack(276, 10, True),
    }),
    ("delay_blocks", 5, 0, 1, 5, {
        "mainhand": stack(276, 10, True),
    }),
    ("offhand_only", 4, 0, 0, 6, {
        "offhand": stack(278, 7, True),
    }),
    ("all_equipment", 9, 0, 0, 7, {
        "mainhand": stack(276, 30, True),
        "offhand": stack(278, 31, True),
        "feet": stack(313, 32, True),
        "legs": stack(312, 33, True),
        "chest": stack(443, 34, True),
        "head": stack(310, 35, True),
    }),
]


def cases():
    out = list(BASE_CASES)
    equipment = {
        "mainhand": stack(276, 8, True),
        "chest": stack(443, 11, True),
    }
    for seed in range(16):
        out.append((f"two_candidates_seed_{seed}", 5, 0, 0,
                    seed, equipment))
    return out


def native(value, cooldown, delay, seed, equipment):
    args = [
        str(MAGMA / "game" / "test_mending_oracle"),
        str(seed), str(value), str(cooldown), str(delay),
    ]
    for name in NAMES:
        value_stack = equipment.get(name, stack())
        args.extend((
            str(value_stack["item"]), str(value_stack["meta"]),
            str(int(bool(value_stack.get("enchants")))),
        ))
    return json.loads(subprocess.check_output(args, text=True))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    selected = [case for case in cases()
                if not args.case or case[0] == args.case]
    if not selected:
        raise SystemExit("unknown case")
    request(args.port, "server_step_lock")
    try:
        for name, value, cooldown, delay, seed, equipment in selected:
            action = {
                "value": value, "cooldown": cooldown, "delay": delay,
                "player_seed48": seed,
            }
            action.update(equipment)
            java = request(args.port, "mending_xp_locked", action)
            c_result = native(value, cooldown, delay, seed, equipment)
            if java != c_result:
                raise AssertionError(
                    f"{name}\njava={json.dumps(java, sort_keys=True)}"
                    f"\nnative={json.dumps(c_result, sort_keys=True)}")
    finally:
        request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} exact Mending XP pickups")


if __name__ == "__main__":
    main()
