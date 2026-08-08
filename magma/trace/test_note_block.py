#!/usr/bin/env python3
"""Compare note-block redstone callbacks to live Minecraft 1.11.2."""

import argparse
import json
import subprocess
import time

from test_dragon_crystal_notification import request


WORLD_SEED48 = 0x123456789ABC
MATH_SEED48 = 0x0FEDCBA98765
CASES = 12


def native_row(path, fixture):
    return json.loads(subprocess.check_output(
        [path, str(fixture)], text=True))


def validate(row, fixture, label):
    if not row.get("ok") or row.get("case") != fixture:
        raise AssertionError(f"case {fixture} {label}: invalid result")
    expected_effect = fixture in (1, 4, 6, 7, 8, 9, 11)
    if len(row.get("sounds", [])) != int(expected_effect) or \
            len(row.get("particles", [])) != int(expected_effect):
        raise AssertionError(
            f"case {fixture} {label}: wrong effect cardinality")
    if row.get("world_seed48") != WORLD_SEED48 or \
            row.get("math_seed48") != MATH_SEED48:
        raise AssertionError(f"case {fixture} {label}: RNG advanced")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", required=True)
    parser.add_argument("--native-only", action="store_true")
    args = parser.parse_args()
    if args.native_only:
        for fixture in range(CASES):
            validate(native_row(args.native, fixture), fixture, "native")
        print(f"PASS native: {CASES} note-block callback rows")
        return
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
        # A freshly launched client can finish constructing replicated
        # entities after the server parks, advancing process-global
        # Math.random. Drain that startup-only work before controlled cursors.
        time.sleep(1.0)
        for fixture in range(CASES):
            action = {
                "case": fixture,
                "world_seed48": WORLD_SEED48,
                "math_seed48": MATH_SEED48,
            }
            java = request(args.port, "note_block_locked", action)
            native = native_row(args.native, fixture)
            validate(java, fixture, "Java")
            validate(native, fixture, "native")
            if java != native:
                raise AssertionError(
                    f"case {fixture}:\n"
                    f"java={json.dumps(java, sort_keys=True)}\n"
                    f"native={json.dumps(native, sort_keys=True)}")
        print(f"PASS real Java/native: {CASES} note-block callback rows")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
