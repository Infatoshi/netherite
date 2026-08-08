#!/usr/bin/env python3
"""Exact EntityArrow player-pickup comparison against Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]


def native(case):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_arrow_pickup_oracle"),
        str(case["pickup"]), str(case["shake"]),
        str(case["inventory_mode"]), str(int(case["creative"])),
    ], text=True)
    return json.loads(raw)


def cases():
    for pickup in range(3):
        for creative in (False, True):
            for inventory_mode in range(3):
                yield {
                    "name": (
                        f"pickup{pickup}_creative{int(creative)}_"
                        f"inventory{inventory_mode}"
                    ),
                    "pickup": pickup,
                    "shake": 0,
                    "inventory_mode": inventory_mode,
                    "creative": creative,
                }
    for shake in (1, 7):
        yield {
            "name": f"allowed_shake{shake}",
            "pickup": 1,
            "shake": shake,
            "inventory_mode": 0,
            "creative": False,
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    selected = [case for case in cases()
                if not args.case or case["name"] == args.case]
    if not selected:
        parser.error(f"unknown case: {args.case}")
    subprocess.run([
        "make", "-C", str(MAGMA), "game/test_arrow_pickup_oracle",
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
        for case in selected:
            java = request(args.port, "arrow_pickup_locked", case)
            cpu = native(case)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"{case['name']}: {mismatch}: "
                    f"Java={java!r} CPU={cpu!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} arrow pickup cases")


if __name__ == "__main__":
    main()
