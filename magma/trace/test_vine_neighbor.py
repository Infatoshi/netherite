#!/usr/bin/env python3
"""Compare BlockVine neighbor support rechecks to live Minecraft 1.11.2."""

import argparse
import json
import subprocess
import time

from test_dragon_crystal_notification import request


WORLD_SEED48 = 0x23456789ABCD
MATH_SEED48 = 0x3456789ABCDE
NEXT_ENTITY_ID = 760000


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
        for fixture in range(8):
            action = {
                "case": fixture,
                "world_seed48": WORLD_SEED48,
                "math_seed48": MATH_SEED48,
                "next_entity_id": NEXT_ENTITY_ID,
            }
            java = request(args.port, "vine_neighbor_locked", action)
            native = json.loads(subprocess.check_output(
                [args.native, str(fixture)], text=True))
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
                    f"case {fixture}:\n"
                    f"java={json.dumps(java, sort_keys=True)}\n"
                    f"native={json.dumps(native, sort_keys=True)}\n"
                    f"block_lengths={len(java_blocks)},{len(native_blocks)} "
                    f"diff_count={len(diffs)} first_diffs={diffs[:24]}")
        print("PASS real Java/native: 8 vine neighbor-support rows")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
