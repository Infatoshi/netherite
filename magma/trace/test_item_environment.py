#!/usr/bin/env python3
"""Exact EntityItem environment-tick comparison against Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
KINDS = (
    "lava_scan", "lava_no_scan",
    "water_flow_first", "water_flow_entry", "water_still_entry",
    "fire_contact", "cactus_contact", "periodic_fire", "expire", "void",
    "pushout_west", "pushout_east", "pushout_north", "pushout_south",
    "pushout_up",
)
SEEDS = (0, 1, 0x123456789ABC, (1 << 48) - 1)
KEYS = (
    "ok", "kind", "alive", "health", "fire", "in_water",
    "first_update", "age", "pickup_delay", "ticks_existed",
    "on_ground", "no_clip", "entity_seed48", "position_bits", "motion_bits",
)


def normalized(value):
    return {key: value[key] for key in KEYS}


def native(kind, seed):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_item_environment_oracle"),
        kind, str(seed),
    ], text=True)
    return json.loads(raw)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case", choices=KINDS)
    args = parser.parse_args()
    selected = (args.case,) if args.case else KINDS
    subprocess.run([
        "make", "-C", str(MAGMA), "game/test_item_environment_oracle",
    ], check=True, stdout=subprocess.DEVNULL)
    locked = False
    count = 0
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
        for kind in selected:
            seeds = SEEDS if kind in {
                "lava_scan", "water_flow_entry", "water_still_entry",
                "pushout_west", "pushout_east", "pushout_north",
                "pushout_south", "pushout_up",
            } else SEEDS[:1]
            for seed in seeds:
                action = {"kind": kind, "entity_seed48": seed}
                java = normalized(request(
                    args.port, "item_environment_locked", action))
                cpu = native(kind, seed)
                if java != cpu:
                    raise AssertionError(
                        f"{kind} seed={seed}: Java={java!r} CPU={cpu!r}")
                count += 1
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {count} exact item environment ticks")


if __name__ == "__main__":
    main()
