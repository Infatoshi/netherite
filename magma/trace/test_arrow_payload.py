#!/usr/bin/env python3
"""Tipped/spectral arrow payload comparison against real Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]


def cases():
    base = {
        "spectral_duration": 200,
        "custom_id": 0,
        "custom_amplifier": 0,
        "custom_duration": 0,
        "custom_flags": 0,
        "color": -1,
        "target_type": -1,
        "ground_ticks": -1,
    }
    variants = [
        ("tipped_poison_pig", {
            "kind": 1, "potion_type": 25, "target_type": 0,
        }),
        ("tipped_poison_zombie", {
            "kind": 1, "potion_type": 25, "target_type": 1,
        }),
        ("tipped_custom_flags", {
            "kind": 1, "potion_type": 25, "target_type": 0,
            "custom_id": 5, "custom_amplifier": 1,
            "custom_duration": 300, "custom_flags": 1,
            "color": 0x123456,
        }),
        ("tipped_instant_healing", {
            "kind": 1, "potion_type": 21, "target_type": 0,
        }),
        ("spectral_duration", {
            "kind": 2, "potion_type": 0, "target_type": 0,
            "spectral_duration": 321,
        }),
        ("tipped_empty_pickup", {
            "kind": 1, "potion_type": 0,
        }),
        ("tipped_washout_599", {
            "kind": 1, "potion_type": 0,
            "custom_id": 5, "custom_duration": 240,
            "custom_flags": 3, "ground_ticks": 599,
        }),
        ("tipped_washout_600", {
            "kind": 1, "potion_type": 0,
            "custom_id": 5, "custom_duration": 240,
            "custom_flags": 3, "ground_ticks": 600,
        }),
    ]
    for name, values in variants:
        yield {"name": name, **base, **values}


def native(case):
    fields = (
        "kind", "potion_type", "spectral_duration", "custom_id",
        "custom_amplifier", "custom_duration", "custom_flags", "color",
        "target_type", "ground_ticks",
    )
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_arrow_payload_oracle"),
        *(str(case[field]) for field in fields),
    ], text=True)
    return json.loads(raw)


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
        "make", "-C", str(MAGMA), "game/test_arrow_payload_oracle",
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
            action = {key: value for key, value in case.items()
                      if key != "name"}
            java = request(args.port, "arrow_payload_locked", action)
            cpu = native(case)
            if java != cpu:
                mismatch = [key for key in sorted(set(java) | set(cpu))
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"{case['name']}: {mismatch}: "
                    f"Java={java!r} CPU={cpu!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} arrow payload cases")


if __name__ == "__main__":
    main()
