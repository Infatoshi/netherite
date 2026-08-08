#!/usr/bin/env python3
"""Bit-compare vanilla sheep grass eating to the native task boundary."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
NEXT_ENTITY_ID = 630000
MASK48 = (1 << 48) - 1

CASES = (
    ("grass", "grass", True, True, 0, 0),
    ("tallgrass", "tallgrass", True, True, 0, 0),
    ("tall_priority", "tall_over_grass", True, True, 0, 0),
    ("grass_no_grief", "grass", False, True, 0, 0),
    ("tall_no_grief", "tallgrass", False, True, 0, 0),
    ("child_growth", "grass", True, True, -2400, 0),
    ("child_clamp", "tallgrass", True, True, -100, 0),
    ("unsheared_child", "grass", True, False, -2400, 0),
    ("air", "air", True, True, 0, 0),
    ("fern", "fern", True, True, 0, 0),
    ("rng_miss", "grass", True, True, 0, 1),
)


def c_result(substrate, java, grief, sheared, age, seed):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_grazing_oracle"), substrate,
        repr(float(java["world_x"]) + 0.5),
        repr(float(java["world_y"])), repr(float(java["world_z"]) + 0.5),
        str(int(grief)), str(int(sheared)), str(age), "14", str(seed),
        str(NEXT_ENTITY_ID), "40",
    ], text=True)
    return json.loads(raw)


def java_lcg(seed):
    return (seed * 0x5DEECE66D + 0xB) & MASK48


def validate_expected(name, result, substrate, grief, sheared, age, seed):
    accepted = substrate not in ("air", "fern") and name != "rng_miss"
    if result["should_execute"] != accepted:
        raise AssertionError(
            f"should_execute={result['should_execute']} expected={accepted}")
    if result["entity_seed48"] != java_lcg(seed):
        raise AssertionError("eligibility did not consume exactly one RNG step")
    rows = result["rows"]
    if len(rows) != 41:
        raise AssertionError(f"row count={len(rows)}")
    if not accepted:
        if any(row["timer"] != 0 or row["status_count"] != 0
               or row["world_event_count"] != 0 for row in rows):
            raise AssertionError("rejected task changed timer/events")
        return
    if rows[0]["timer"] != 40 or rows[35]["timer"] != 5 \
            or rows[36]["timer"] != 4 or rows[40]["timer"] != 0:
        raise AssertionError("timer/update boundary mismatch")
    if any(row["status_count"] != 1 for row in rows):
        raise AssertionError("status 10 was not emitted exactly at start")
    if any(row["world_event_count"] != 0 for row in rows[:36]):
        raise AssertionError("world event preceded update 36")
    expected_event_count = int(grief)
    if any(row["world_event_count"] != expected_event_count
           for row in rows[36:]):
        raise AssertionError("world event count after effect mismatch")
    effect = rows[36]
    expected_age = 0 if age > -1200 else age + 1200
    if age >= 0:
        expected_age = age
    if effect["sheared"] or effect["growing_age"] != expected_age:
        raise AssertionError(
            f"bonus mismatch: sheared={effect['sheared']} "
            f"age={effect['growing_age']} expected_age={expected_age}")
    if substrate == "tall_over_grass" and effect["below_block"] != 2:
        raise AssertionError("tall grass did not win over grass below")
    if grief:
        if substrate in ("tallgrass", "tall_over_grass"):
            if effect["source_block"] != 0:
                raise AssertionError("tall grass was not destroyed")
            expected_data = 31 + (1 << 12)
        else:
            if effect["below_block"] != 3:
                raise AssertionError("grass did not become dirt")
            expected_data = 2
        if len(result["world_events"]) != 1 \
                or result["world_events"][0]["data"] != expected_data:
            raise AssertionError("event 2001 payload mismatch")
    else:
        if result["world_events"]:
            raise AssertionError("mobGriefing=false emitted a world event")
        if substrate == "grass" and effect["below_block"] != 2:
            raise AssertionError("mobGriefing=false changed grass")
        if substrate == "tallgrass" and effect["source_block"] != 31:
            raise AssertionError("mobGriefing=false changed tall grass")


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
            ["make", "-C", str(MAGMA), "game/test_grazing_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        for name, substrate, grief, sheared, age, seed in cases:
            java = request(args.port, "graze_sheep_locked", {
                "substrate": substrate,
                "mob_griefing": grief,
                "sheared": sheared,
                "growing_age": age,
                "fleece": 14,
                "updates": 40,
                "entity_seed48": seed,
                "next_entity_id": NEXT_ENTITY_ID,
            })
            magma = c_result(substrate, java, grief, sheared, age, seed)
            if java != magma:
                for field in java:
                    if java[field] != magma.get(field):
                        raise AssertionError(
                            f"{name} {field}: java={java[field]!r} "
                            f"magma={magma.get(field)!r}")
                raise AssertionError(f"{name}: extra native fields")
            try:
                validate_expected(
                    name, java, substrate, grief, sheared, age, seed)
            except AssertionError as exc:
                raise AssertionError(f"{name}: {exc}") from exc
        print(f"PASS java==magma: {len(cases)} sheep grazing cases, "
              "exact RNG/timer/blocks/regrowth/events")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
