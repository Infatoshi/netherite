#!/usr/bin/env python3
"""Bit-compare Forge Mooshroom shearing and cow conversion."""

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
    ("mooshroom", False, 0, 2),
    ("mooshroom_child", True, 0, 1),
    ("mooshroom_unbreaking", False, 3, 2),
)


def native(mode, java, entity_seed):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_shearing_oracle"), mode,
        repr(float(java["x"])), repr(float(java["y"])),
        repr(float(java["z"])), str(entity_seed), str(MATH_SEED48),
        str(SHEAR_SEED48), str(NEXT_ENTITY_ID),
    ], text=True)
    return json.loads(raw)


def same_float(label, left, right, precision):
    fmt = "!f" if precision == 32 else "!d"
    if struct.pack(fmt, float(left)) != struct.pack(fmt, float(right)):
        raise AssertionError(f"{label}: java={left!r} magma={right!r}")


def compare(name, java, magma, expected_code):
    if java["result"] != "success" or magma["result_code"] != expected_code:
        raise AssertionError(
            f"result: java={java['result']!r} magma={magma['result_code']!r}")
    fields = (
        "eid", "source_dead", "cow_present", "growing_age",
        "math_seed48", "shear_random_constructed", "shear_seed48",
        "next_entity_id", "tool_item", "tool_count", "tool_meta",
        "tool_unbreaking",
    )
    for field in fields:
        if java[field] != magma[field]:
            raise AssertionError(
                f"{field}: java={java[field]!r} magma={magma[field]!r}")
    for field in ("x", "y", "z"):
        same_float(field, java[field], magma[field], 64)
    if java["cow_present"]:
        for field in ("cow_eid", "cow_growing_age"):
            if java[field] != magma[field]:
                raise AssertionError(
                    f"{field}: java={java[field]!r} magma={magma[field]!r}")
        for field in ("cow_x", "cow_y", "cow_z"):
            same_float(field, java[field], magma[field], 64)
        for field in (
                "cow_yaw", "cow_pitch", "cow_render_yaw", "cow_health"):
            same_float(field, java[field], magma[field], 32)
    if len(java["drops"]) != len(magma["drops"]):
        raise AssertionError(
            f"drop count: java={len(java['drops'])} magma={len(magma['drops'])}")
    integers = (
        "eid", "item", "count", "meta", "age", "pickup_delay",
        "health", "lifespan", "on_ground", "dead",
    )
    for index, (jitem, citem) in enumerate(zip(java["drops"], magma["drops"])):
        for field in integers:
            if jitem[field] != citem[field]:
                raise AssertionError(
                    f"drop {index} {field}: java={jitem[field]!r} "
                    f"magma={citem[field]!r}")
        for field in ("x", "y", "z", "vx", "vy", "vz"):
            same_float(f"drop {index} {field}", jitem[field], citem[field], 64)
        for field in ("yaw", "hover_start"):
            same_float(f"drop {index} {field}", jitem[field], citem[field], 32)
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
            same_float(
                f"event {index} {field}", jevent[field], cevent[field], 64)
        for field in ("volume", "pitch"):
            same_float(
                f"event {index} {field}", jevent[field], cevent[field], 32)


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
        for name, child, unbreaking, expected_code in cases:
            entity_seed = 1
            java = request(args.port, "mooshroom_shear_locked", {
                "hand": "main",
                "fleece": 14,
                "sheared": False,
                "child": child,
                "held_item": 359,
                "tool_meta": 0,
                "unbreaking": unbreaking,
                "entity_seed48": entity_seed,
                "math_seed48": MATH_SEED48,
                "shear_seed48": SHEAR_SEED48,
                "next_entity_id": NEXT_ENTITY_ID,
            })
            try:
                compare(name, java, native(name, java, entity_seed), expected_code)
            except AssertionError as exc:
                raise AssertionError(f"{name}: {exc}") from exc
            total_drops += len(java["drops"])
        print(f"PASS java==magma: {len(cases)} Mooshroom shear cases, "
              f"{total_drops} exact mushroom entities and cow transitions")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
