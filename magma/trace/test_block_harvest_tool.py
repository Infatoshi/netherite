#!/usr/bin/env python3
"""Exact real-Java/native ItemStack.onBlockDestroyed durability boundary."""

import argparse
import json
import pathlib
import subprocess

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
NATIVE = MAGMA / "game" / "test_block_harvest_tool_oracle"


def cases():
    values = []
    fixtures = (
        ("pick_stone", 1, 278, 0),
        ("pick_zero_hardness", 50, 278, 0),
        ("sword_stone", 1, 276, 0),
        ("sword_zero_hardness", 50, 276, 0),
        ("shovel_stone", 1, 277, 0),
        ("axe_stone", 1, 279, 0),
        ("hoe_stone", 1, 293, 0),
        ("shears_stone", 1, 359, 0),
        ("shears_zero_hardness", 50, 359, 0),
        ("pick_break", 1, 278, 1561),
        ("sword_break", 1, 276, 1560),
    )
    for name, block, tool, damage in fixtures:
        for unbreaking in (0, 1, 3):
            for seed in (0, 1, 7, (1 << 48) - 1):
                values.append((f"{name}_u{unbreaking}_s{seed}", {
                    "block": block, "tool": tool, "damage": damage,
                    "unbreaking": unbreaking, "player_seed48": seed,
                }))
    return values


def native(action):
    command = [str(NATIVE), *(str(action[key]) for key in (
        "block", "tool", "damage", "unbreaking", "player_seed48"))]
    return json.loads(subprocess.check_output(command, text=True))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    request(args.port, "server_step_lock")
    try:
        for name, action in cases():
            java = request(args.port, "block_harvest_tool_locked", action)
            c_result = native(action)
            if java != c_result:
                raise AssertionError(
                    f"{name}\njava={json.dumps(java, sort_keys=True)}"
                    f"\nnative={json.dumps(c_result, sort_keys=True)}")
    finally:
        request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(cases())} block tool durability cases")


if __name__ == "__main__":
    main()
