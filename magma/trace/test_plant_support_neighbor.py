#!/usr/bin/env python3
"""Compare plant/attachment callbacks to live Minecraft 1.11.2."""

import argparse
import json
import subprocess
import time

from test_dragon_crystal_notification import request


WORLD_SEED48 = 0x123456789ABC
MATH_SEED48 = 0x0FEDCBA98765
BLOCK_SEED48 = 0
NEXT_ENTITY_ID = 780000
CASES = 99


def native_row(path, fixture):
    return json.loads(subprocess.check_output(
        [path, str(fixture)], text=True))


def validate(row, fixture, label):
    if not row.get("ok") or row.get("case") != fixture:
        raise AssertionError(f"case {fixture} {label}: invalid result")
    before = row.get("before_blocks", [])
    after = row.get("after_blocks", [])
    expected = 125 if fixture >= 76 else (100 if fixture == 75 else 36)
    if len(before) != expected or len(after) != expected:
        raise AssertionError(
            f"case {fixture} {label}: incomplete raw block volume")
    if before == after:
        raise AssertionError(
            f"case {fixture} {label}: vacuous callback fixture")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", required=True)
    parser.add_argument("--native-only", action="store_true")
    args = parser.parse_args()
    if args.native_only:
        for fixture in range(CASES):
            validate(native_row(args.native, fixture), fixture, "native")
        print(f"PASS native: {CASES} plant/attachment callback rows")
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
        # A freshly started client, or one replicating the preceding server
        # items, can still construct client entities after the server parks.
        # Drain that client-only work before injecting process-global cursors.
        client_drain = 0.25
        for fixture in range(CASES):
            time.sleep(client_drain)
            action = {
                "case": fixture,
                "world_seed48": WORLD_SEED48,
                "math_seed48": MATH_SEED48,
                "block_seed48": BLOCK_SEED48,
                "next_entity_id": NEXT_ENTITY_ID,
            }
            for attempt in range(5):
                try:
                    java = request(
                        args.port, "plant_support_neighbor_locked", action)
                    break
                except RuntimeError as error:
                    result = error.args[0] if error.args else {}
                    if not isinstance(result, dict) or \
                            "EID cursor contaminated" not in \
                            result.get("error", "") or attempt == 4:
                        raise
                    time.sleep(1.0)
            client_drain = 1.0 if java.get("items") else 0.05
            native = native_row(args.native, fixture)
            validate(java, fixture, "Java")
            validate(native, fixture, "native")
            if java != native:
                java_blocks = java.pop("before_blocks", []), \
                    java.pop("after_blocks", [])
                native_blocks = native.pop("before_blocks", []), \
                    native.pop("after_blocks", [])
                diffs = []
                for phase, (left, right) in enumerate(zip(
                        java_blocks, native_blocks)):
                    diffs.append([
                        (index, a, b)
                        for index, (a, b) in enumerate(zip(left, right))
                        if a != b
                    ])
                raise AssertionError(
                    f"case {fixture}:\n"
                    f"java={json.dumps(java, sort_keys=True)}\n"
                    f"native={json.dumps(native, sort_keys=True)}\n"
                    f"block_diffs={[d[:24] for d in diffs]}")
        print(f"PASS real Java/native: {CASES} plant/attachment callback rows")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
