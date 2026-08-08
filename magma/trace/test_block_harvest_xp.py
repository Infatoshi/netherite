#!/usr/bin/env python3
"""Exact real-Java/native Forge block-break XP boundary."""

import argparse
import json
import pathlib
import subprocess

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
NATIVE = MAGMA / "game" / "test_block_harvest_xp_oracle"


def cases():
    values = []
    fixtures = (
        ("gold", 14, 257),
        ("iron", 15, 274),
        ("coal", 16, 270),
        ("lapis", 21, 274),
        ("diamond", 56, 257),
        ("redstone", 73, 257),
        ("lit_redstone", 74, 257),
        ("emerald", 129, 257),
        ("quartz", 153, 270),
    )
    seeds = (0, 1, 7, (1 << 48) - 1)
    for name, block, tool in fixtures:
        for fortune in (0, 1, 3):
            for world_seed in seeds:
                for block_seed in seeds:
                    values.append((f"{name}_f{fortune}_w{world_seed}_b{block_seed}", {
                        "block": block, "tool": tool, "silk": 0,
                        "fortune": fortune, "world_seed48": world_seed,
                        "block_seed48": block_seed,
                    }))
        for world_seed in seeds:
            values.append((f"{name}_silk_w{world_seed}", {
                "block": block, "tool": tool, "silk": 1,
                "fortune": 3, "world_seed48": world_seed,
                "block_seed48": 7,
            }))
    for name, block, tool in (
            ("coal_hand", 16, 0),
            ("diamond_wood", 56, 270),
            ("redstone_stone", 73, 274)):
        values.append((name, {
            "block": block, "tool": tool, "silk": 0,
            "fortune": 3, "world_seed48": 7, "block_seed48": 7,
        }))
    return values


def native(action):
    command = [str(NATIVE), *(str(action[key]) for key in (
        "block", "tool", "silk", "fortune",
        "world_seed48", "block_seed48"))]
    return json.loads(subprocess.check_output(command, text=True))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    request(args.port, "server_step_lock")
    try:
        for name, action in cases():
            java = request(args.port, "block_harvest_xp_locked", action)
            c_result = native(action)
            if java != c_result:
                raise AssertionError(
                    f"{name}\njava={json.dumps(java, sort_keys=True)}"
                    f"\nnative={json.dumps(c_result, sort_keys=True)}")
    finally:
        request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(cases())} block harvest XP cases")


if __name__ == "__main__":
    main()
