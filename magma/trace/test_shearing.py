#!/usr/bin/env python3
"""Bit-compare Forge sheep shearing to the parked 1.11.2 game."""

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
NEXT_ENTITY_ID = 620000

CASES = (
    ("count1", 0, "main", False, False, 359, 0, 0, 2),
    ("count3", 1, "main", False, False, 359, 0, 0, 2),
    ("adult_offhand", 1, "offhand", False, False, 359, 0, 0, 2),
    ("already_sheared", 1, "main", True, False, 359, 0, 0, 1),
    ("child", 1, "main", False, True, 359, 0, 0, 1),
    ("non_shears", 1, "main", False, False, 280, 0, 0, 0),
    ("unbreaking", 1, "main", False, False, 359, 0, 3, 2),
)


def c_result(mode, java, entity_seed):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_shearing_oracle"), mode,
        repr(float(java["x"])), repr(float(java["y"])), repr(float(java["z"])),
        str(entity_seed), str(MATH_SEED48), str(SHEAR_SEED48),
        str(NEXT_ENTITY_ID),
    ], text=True)
    return json.loads(raw)


def same_double(label, left, right):
    if struct.pack("!d", float(left)) != struct.pack("!d", float(right)):
        raise AssertionError(f"{label}: java={left!r} magma={right!r}")


def same_float(label, left, right):
    if struct.pack("!f", float(left)) != struct.pack("!f", float(right)):
        raise AssertionError(f"{label}: java={left!r} magma={right!r}")


def compare(mode, java, magma, expected_code):
    result_code = {"pass": 0, "success": 2}[java["result"]]
    if expected_code == 1:
        result_code = 1 if java["result"] == "success" else result_code
    if result_code != expected_code or magma["result_code"] != expected_code:
        raise AssertionError(
            f"result: java={java['result']!r} magma={magma['result_code']!r} "
            f"expected={expected_code}")
    scalars = (
        "eid", "fleece", "sheared", "growing_age", "entity_seed48",
        "math_seed48", "shear_random_constructed", "shear_seed48",
        "next_entity_id", "tool_item", "tool_count", "tool_meta",
        "tool_unbreaking",
    )
    for field in scalars:
        if java[field] != magma[field]:
            raise AssertionError(
                f"{field}: java={java[field]!r} magma={magma[field]!r}")
    if len(java["drops"]) != len(magma["drops"]):
        raise AssertionError(
            f"drop count: java={len(java['drops'])} magma={len(magma['drops'])}")
    integer_fields = (
        "eid", "item", "count", "meta", "age", "pickup_delay", "health",
        "lifespan", "on_ground", "dead",
    )
    for index, (jitem, citem) in enumerate(zip(java["drops"], magma["drops"])):
        for field in integer_fields:
            if jitem[field] != citem[field]:
                raise AssertionError(
                    f"drop {index} {field}: java={jitem[field]!r} "
                    f"magma={citem[field]!r}")
        for field in ("x", "y", "z", "vx", "vy", "vz"):
            same_double(f"drop {index} {field}", jitem[field], citem[field])
        for field in ("yaw", "hover_start"):
            same_float(f"drop {index} {field}", jitem[field], citem[field])
    if len(java["events"]) != len(magma["events"]):
        raise AssertionError(
            f"event count: java={java['events']!r} magma={magma['events']!r}")
    for index, (jevent, cevent) in enumerate(zip(
            java["events"], magma["events"])):
        for field in ("kind", "eid", "sound", "category"):
            if jevent[field] != cevent[field]:
                raise AssertionError(
                    f"event {index} {field}: java={jevent[field]!r} "
                    f"magma={cevent[field]!r}")
        for field in ("x", "y", "z"):
            same_double(f"event {index} {field}", jevent[field], cevent[field])
        for field in ("volume", "pitch"):
            same_float(f"event {index} {field}", jevent[field], cevent[field])


def validate_capacity():
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_shearing_oracle"), "capacity",
        "10", "64", "10", "1", str(MATH_SEED48), str(SHEAR_SEED48),
        str(NEXT_ENTITY_ID),
    ], text=True)
    result = json.loads(raw)
    expected = {
        "result_code": -1,
        "sheared": False,
        "entity_seed48": 1,
        "math_seed48": MATH_SEED48,
        "shear_seed48": SHEAR_SEED48,
        "next_entity_id": NEXT_ENTITY_ID + 1,
        "tool_item": 359,
        "tool_count": 1,
        "tool_meta": 0,
        "drops": [],
        "events": [],
    }
    for field, value in expected.items():
        if result[field] != value:
            raise AssertionError(
                f"capacity {field}: got={result[field]!r} expected={value!r}")


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
        total_drops = 0
        for (mode, entity_seed, hand, sheared, child, held_item,
             tool_meta, unbreaking, expected_code) in cases:
            java = request(args.port, "shear_sheep_locked", {
                "hand": hand,
                "fleece": 14,
                "sheared": sheared,
                "child": child,
                "held_item": held_item,
                "tool_meta": tool_meta,
                "unbreaking": unbreaking,
                "entity_seed48": entity_seed,
                "math_seed48": MATH_SEED48,
                "shear_seed48": SHEAR_SEED48,
                "next_entity_id": NEXT_ENTITY_ID,
            })
            try:
                compare(mode, java, c_result(mode, java, entity_seed), expected_code)
            except AssertionError as exc:
                raise AssertionError(f"{mode}: {exc}") from exc
            total_drops += len(java["drops"])
        validate_capacity()
        print(f"PASS java==magma: {len(cases)} shearing cases, "
              f"{total_drops} exact wool entities, capacity atomic")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
