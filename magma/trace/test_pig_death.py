#!/usr/bin/env python3
"""Bit-compare ordered 1.11.2 pig pork and saddle death drops."""

import argparse
import json
import pathlib
import struct
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
NEXT_ID = 680000
ENTITY_SEED = 0x3456789ABCDE
MATH_SEED = 0x123456789ABC

CASES = [
    ("saddled_no_mob_loot", {
        "saddled": True, "do_mob_loot": False}),
    ("unsaddled_no_mob_loot", {
        "saddled": False, "do_mob_loot": False}),
    ("saddled_raw_loot", {
        "saddled": True, "do_mob_loot": True}),
    ("saddled_cooked_loot", {
        "saddled": True, "do_mob_loot": True, "burning": True}),
    ("unsaddled_raw_loot_second_seed", {
        "saddled": False, "do_mob_loot": True,
        "entity_seed48": 1, "math_seed48": 2}),
]


def fixture(overrides):
    case = {
        "saddled": False,
        "do_mob_loot": True,
        "burning": False,
        "entity_seed48": ENTITY_SEED,
        "math_seed48": MATH_SEED,
        "next_entity_id": NEXT_ID,
    }
    case.update(overrides)
    return case


def native(case, java):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_pig_death_oracle"),
        str(int(case["saddled"])),
        str(int(case["do_mob_loot"])),
        str(int(case["burning"])),
        str(case["entity_seed48"]),
        str(case["math_seed48"]),
        str(case["next_entity_id"]),
        repr(java["pig_x"]),
        repr(java["pig_z"]),
    ], text=True)
    return json.loads(raw)


def normalize(result):
    for drop in result["drops"]:
        drop["yaw_bits"] = struct.pack("!f", drop.pop("yaw")).hex()
        drop["hover_start_bits"] = struct.pack(
            "!f", drop.pop("hover_start")).hex()
    return result


def mismatch(name, java, magma):
    keys = sorted(set(java) | set(magma))
    diff = {key: {"java": java.get(key), "magma": magma.get(key)}
            for key in keys if java.get(key) != magma.get(key)}
    raise AssertionError(f"{name}: {json.dumps(diff, sort_keys=True)}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    cases = [(name, fixture(values)) for name, values in CASES
             if not args.case or name == args.case]
    if not cases:
        parser.error(f"unknown case: {args.case}")

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
        subprocess.run([
            "make", "-C", str(MAGMA), "game/test_pig_death_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        for name, case in cases:
            java = normalize(request(args.port, "pig_death_locked", case))
            magma = normalize(native(case, java))
            if java != magma:
                mismatch(name, java, magma)
        print(f"PASS java==magma: {len(cases)} ordered pig death-drop "
              "transitions, doMobLoot boundary, constructor state, EIDs, "
              "and RNG")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
