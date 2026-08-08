#!/usr/bin/env python3
"""Compare leaf decay, drops, and break marking to live Minecraft 1.11.2."""

import argparse
import json
import subprocess
import time

from test_dragon_crystal_notification import request


MATH_SEED48 = 0x3456789ABCDE
BLOCK_SEED48 = 0x123456789ABC
NEXT_ENTITY_ID = 760000
WORLD_SEEDS = (
    0x23456789ABCD,
    0x23456789ABCD,
    0x23456789ABCD,
    0x23456789ABCD,
    18,
    18,
    0x0005DEECF39C,
    18,
    0,
    55,
    18,
    0x23456789ABCD,
    0x23456789ABCD,
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", required=True)
    args = parser.parse_args()
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
        for fixture, world_seed in enumerate(WORLD_SEEDS):
            action = {
                "case": fixture,
                "world_seed48": world_seed,
                "math_seed48": MATH_SEED48,
                "block_seed48": BLOCK_SEED48,
                "next_entity_id": NEXT_ENTITY_ID,
            }
            java = request(args.port, "leaf_decay_locked", action)
            native = json.loads(subprocess.check_output(
                [args.native, str(fixture), str(world_seed)], text=True))
            if java != native:
                java_blocks = java.pop("blocks", [])
                native_blocks = native.pop("blocks", [])
                diffs = [
                    (index, a, b)
                    for index, (a, b) in enumerate(
                        zip(java_blocks, native_blocks))
                    if a != b
                ]
                raise AssertionError(
                    f"case {fixture} seed {world_seed}:\n"
                    f"java={json.dumps(java, sort_keys=True)}\n"
                    f"native={json.dumps(native, sort_keys=True)}\n"
                    f"block_lengths={len(java_blocks)},{len(native_blocks)} "
                    f"diff_count={len(diffs)} first_diffs={diffs[:24]}")
        print("PASS real Java/native: 13 leaf decay and break rows")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
