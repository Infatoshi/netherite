#!/usr/bin/env python3
"""Exact real-Java/native 1.11.2 block harvest drop boundary."""

import argparse
import json
import pathlib
import subprocess

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
NATIVE = MAGMA / "game" / "test_block_harvest_oracle"


def cases():
    fixtures = (
        ("stone", 1, 0, 278),
        ("granite", 1, 1, 278),
        ("smooth_andesite", 1, 6, 278),
        ("grass", 2, 0, 0),
        ("dirt", 3, 0, 0),
        ("coarse_dirt", 3, 1, 0),
        ("podzol", 3, 2, 0),
        ("oak_planks", 5, 0, 0),
        ("acacia_planks", 5, 4, 0),
        ("gravel", 13, 0, 0),
        ("gold", 14, 0, 257),
        ("iron", 15, 0, 274),
        ("coal", 16, 0, 270),
        ("lapis", 21, 0, 274),
        ("diamond", 56, 0, 257),
        ("crafting_table", 58, 0, 270),
        ("redstone", 73, 0, 257),
        ("lit_redstone", 74, 0, 257),
        ("emerald", 129, 0, 257),
        ("quartz", 153, 0, 270),
        ("glass", 20, 0, 0),
        ("ice", 79, 0, 278),
        ("snow", 80, 0, 277),
        ("clay", 82, 0, 0),
        ("glowstone", 89, 0, 0),
        ("melon", 103, 0, 0),
        ("ender_chest", 130, 2, 270),
    )
    values = []
    seeds = (0, 1, 7, (1 << 48) - 1)
    for name, block, meta, tool in fixtures:
        for fortune in (0, 1, 3):
            for seed in seeds:
                values.append((f"{name}_f{fortune}_s{seed}", {
                    "block": block, "meta": meta, "tool": tool,
                    "silk": 0, "fortune": fortune,
                    "world_seed48": seed,
                }))
        for seed in seeds:
            values.append((f"{name}_silk_s{seed}", {
                "block": block, "meta": meta, "tool": tool,
                "silk": 1, "fortune": 3,
                "world_seed48": seed,
            }))
    for name, block, tool in (
            ("stone_hand", 1, 0),
            ("diamond_wood", 56, 270),
            ("obsidian_iron", 49, 257),
            ("emerald_stone", 129, 274),
            ("ender_chest_hand", 130, 0),
            ("ender_chest_shovel", 130, 277)):
        values.append((name, {
            "block": block, "meta": 0, "tool": tool,
            "silk": 0, "fortune": 3, "world_seed48": 7,
        }))
    for fortune in (0, 1, 3):
        for seed in seeds:
            values.append((f"dead_bush_f{fortune}_s{seed}", {
                "block": 32, "meta": 0, "tool": 0,
                "silk": 0, "fortune": fortune,
                "world_seed48": seed,
            }))
    return values


def native(action):
    command = [str(NATIVE), *(str(action[key]) for key in (
        "block", "meta", "tool", "silk", "fortune", "world_seed48"))]
    return json.loads(subprocess.check_output(command, text=True))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    selected = [entry for entry in cases()
                if not args.case or entry[0] == args.case]
    if not selected:
        raise SystemExit("unknown case")
    request(args.port, "server_step_lock")
    try:
        for name, action in selected:
            java = request(args.port, "block_harvest_drops_locked", action)
            c_result = native(action)
            if java != c_result:
                raise AssertionError(
                    f"{name}\njava={json.dumps(java, sort_keys=True)}"
                    f"\nnative={json.dumps(c_result, sort_keys=True)}")
    finally:
        request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} block harvest drops")


if __name__ == "__main__":
    main()
