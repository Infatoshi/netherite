#!/usr/bin/env python3
"""Bit-compare Forge Snow Golem shearing and pumpkin state."""

import argparse
import json
import pathlib
import struct
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
MATH_SEED48 = 0x123456789ABC
SHEAR_SEED48 = 0x3456789ABCDE
NEXT_ENTITY_ID = 684000
CASES = (
    ("snowman", True, 0, 2),
    ("snowman_removed", False, 0, 1),
    ("snowman_unbreaking", True, 3, 2),
)


def native(mode, java, entity_seed):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_shearing_oracle"), mode,
        repr(float(java["x"])), repr(float(java["y"])),
        repr(float(java["z"])), str(entity_seed), str(MATH_SEED48),
        str(SHEAR_SEED48), str(NEXT_ENTITY_ID),
    ], text=True)
    return json.loads(raw)


def same_double(label, left, right):
    if struct.pack("!d", float(left)) != struct.pack("!d", float(right)):
        raise AssertionError(f"{label}: java={left!r} magma={right!r}")


def compare(java, magma, expected_code):
    if java["result"] != "success" or magma["result_code"] != expected_code:
        raise AssertionError(
            f"result: java={java['result']!r} magma={magma['result_code']!r}")
    for field in (
            "eid", "pumpkin", "entity_seed48", "math_seed48",
            "shear_random_constructed", "shear_seed48", "next_entity_id",
            "tool_item", "tool_count", "tool_meta", "tool_unbreaking",
            "drops", "events"):
        if java[field] != magma[field]:
            raise AssertionError(
                f"{field}: java={java[field]!r} magma={magma[field]!r}")
    for field in ("x", "y", "z"):
        same_double(field, java[field], magma[field])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    cases = [case for case in CASES if not args.case or case[0] == args.case]
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
        time.sleep(2.0)
        subprocess.run(
            ["make", "-C", str(MAGMA), "game/test_shearing_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        for name, pumpkin, unbreaking, expected_code in cases:
            entity_seed = 1
            java = request(args.port, "snowman_shear_locked", {
                "hand": "main",
                "pumpkin": pumpkin,
                "held_item": 359,
                "tool_meta": 0,
                "unbreaking": unbreaking,
                "entity_seed48": entity_seed,
                "math_seed48": MATH_SEED48,
                "shear_seed48": SHEAR_SEED48,
                "next_entity_id": NEXT_ENTITY_ID,
            })
            try:
                compare(java, native(name, java, entity_seed), expected_code)
            except AssertionError as exc:
                raise AssertionError(f"{name}: {exc}") from exc
        print(f"PASS java==magma: {len(cases)} Snow Golem shear cases, "
              "exact pumpkin, tool, RNG, EID, drop and event state")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
