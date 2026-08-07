#!/usr/bin/env python3
"""Compare the scheduled 1.11.2 dragon-egg fall path to real MC.

Both producers deliberately use this JSON contract:

  mode: "supported" or "fall"
  origin_x, origin_z, base_y: absolute fixture anchor
  on_added_scheduled: callbacks immediately after placing the egg
  after_support_loss_scheduled: callbacks after removing its support
  rows: EntityFallingBlock rows [step, fallTime, dead, x, y, z,
         vx, vy, vz, onGround, collidedHorizontally, collidedVertically,
         fallDistance]
  source_block, source_meta, final_blocks, scheduled, fixture_entities,
  math_seed48, world_seed48, next_entity_id

Schedule rows are [x, y, z, block, delay, priority, rank], with delay
relative to the fixture's logical start. The fall fixture must stage an egg
on support, remove that support before the original +5 callback is due, then
run the normal scheduled callback and the complete short fall.  No click is
performed: click teleport has unrelated World.rand behavior.
"""

import argparse
import json
import math
import pathlib
import struct
import subprocess
import time

from test_dragon_crystal_notification import request


MATH_SEED48 = 0x123456789ABC
WORLD_SEED48 = 0x23456789ABCD
NEXT_ENTITY_ID = 520000
HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent


def close(label, java, magma, tolerance=1e-15):
    if not math.isclose(float(java), float(magma), rel_tol=0.0,
                        abs_tol=tolerance):
        raise AssertionError(f"{label}: java={java!r} magma={magma!r}")


def normalized_blocks(result):
    ox, oz, by = (int(result[key]) for key in
                  ("origin_x", "origin_z", "base_y"))
    return sorted((int(x) - ox, int(y) - by, int(z) - oz,
                   int(block), int(meta))
                  for x, y, z, block, meta in result["final_blocks"])


def normalized_schedule(result, field="scheduled"):
    ox, oz, by = (int(result[key]) for key in
                  ("origin_x", "origin_z", "base_y"))
    return sorted((int(x) - ox, int(y) - by, int(z) - oz,
                   int(block), int(delay), int(priority), int(order))
                  for x, y, z, block, delay, priority, order
                  in result[field])


def c_result(fixture, origin_x, origin_z):
    raw = subprocess.check_output(
        [str(MAGMA / "game" / "test_falling_dragon_egg_oracle"), fixture,
         str(origin_x), str(origin_z)], text=True)
    return json.loads(raw)


def validate_common(fixture, result):
    if result["mode"] != fixture:
        raise AssertionError(f"mode: {result['mode']!r}")
    if result["math_seed48"] != MATH_SEED48:
        raise AssertionError("dragon-egg fall advanced Math.random")
    if result["world_seed48"] != WORLD_SEED48:
        raise AssertionError("dragon-egg fall advanced World.rand")
    initial = normalized_schedule(result, "on_added_scheduled")
    expected_initial = [(0, 0, 0, 122, 5, 0, 0)]
    if initial != expected_initial:
        raise AssertionError(f"on-added +5 callback: {initial!r}")
    after_support = normalized_schedule(result, "after_support_loss_scheduled")
    expected_after_support = [] if fixture == "supported" else expected_initial
    if after_support != expected_after_support:
        raise AssertionError(
            "neighbor schedule duplicated or postponed the original +5: "
            f"{after_support!r}")


def validate_java(fixture, result):
    validate_common(fixture, result)
    if fixture == "supported":
        if result["rows"]:
            raise AssertionError("supported egg spawned a falling entity")
        if result["source_block"] != 122 or result["source_meta"] != 0:
            raise AssertionError("supported callback changed the egg")
        if result["next_entity_id"] != NEXT_ENTITY_ID:
            raise AssertionError("supported callback consumed an entity id")
        if normalized_schedule(result):
            raise AssertionError("supported callback did not drain")
        return

    if fixture != "fall":
        raise AssertionError(f"unexpected fixture: {fixture!r}")
    rows = result["rows"]
    if len(rows) != 13:
        raise AssertionError(f"expected 13 falling rows, got {len(rows)}")
    for number, row in enumerate(rows, 1):
        if row[0] != number or row[1] != number:
            raise AssertionError(f"fallTime row {number}: {row!r}")
    if result["source_block"] != 0 or result["source_meta"] != 0:
        raise AssertionError("fall did not remove the source egg")
    if result["next_entity_id"] != NEXT_ENTITY_ID + 1:
        raise AssertionError("falling egg entity-id cursor")
    entities = result["fixture_entities"]
    expected_entity = [[NEXT_ENTITY_ID, True,
                        result["origin_x"] + 0.5, result["base_y"] - 3.0,
                        result["origin_z"] + 0.5]]
    if entities != expected_entity:
        raise AssertionError(f"retired falling entity state: {entities!r}")
    last = rows[-1]
    if not last[2] or not last[9] or not last[11]:
        raise AssertionError(f"landing lifecycle: {last!r}")
    egg = [cell for cell in normalized_blocks(result) if cell[3] == 122]
    if egg != [(0, -3, 0, 122, 0)]:
        raise AssertionError(f"final egg cell: {egg!r}")
    expected_landed = [(0, -3, 0, 122, 5, 0, 0)]
    if normalized_schedule(result) != expected_landed:
        raise AssertionError(
            f"landed egg must schedule its own +5 tick: {result['scheduled']!r}")


def compare(fixture, java, magma):
    validate_java(fixture, java)
    validate_common(fixture, magma)
    for field in ("mode", "origin_x", "origin_z", "base_y",
                  "source_block", "source_meta", "math_seed48",
                  "world_seed48", "next_entity_id"):
        if java[field] != magma[field]:
            raise AssertionError(
                f"{field}: java={java[field]!r} magma={magma[field]!r}")
    for field in ("on_added_scheduled", "after_support_loss_scheduled"):
        if normalized_schedule(java, field) != normalized_schedule(magma, field):
            raise AssertionError(f"{field}: java and magma differ")
    if len(java["rows"]) != len(magma["rows"]):
        raise AssertionError("falling row count differs")
    for index, (jrow, crow) in enumerate(zip(java["rows"], magma["rows"]), 1):
        for field in (0, 1, 2, 9, 10, 11):
            if jrow[field] != crow[field]:
                raise AssertionError(
                    f"row {index} field {field}: java={jrow[field]!r} "
                    f"magma={crow[field]!r}")
        close(f"row {index} x", float(jrow[3]) - float(java["origin_x"]),
              float(crow[3]) - float(magma["origin_x"]), 1e-14)
        close(f"row {index} y", jrow[4], crow[4], 1e-13)
        close(f"row {index} z", float(jrow[5]) - float(java["origin_z"]),
              float(crow[5]) - float(magma["origin_z"]), 1e-14)
        for field, name in ((6, "vx"), (7, "vy"), (8, "vz")):
            close(f"row {index} {name}", jrow[field], crow[field])
        if struct.pack("!f", float(jrow[12])) != struct.pack(
                "!f", float(crow[12])):
            raise AssertionError(f"row {index} fallDistance differs")
    if normalized_blocks(java) != normalized_blocks(magma):
        raise AssertionError("final raw blocks differ")
    if normalized_schedule(java) != normalized_schedule(magma):
        raise AssertionError("landed schedule differs")
    if java["fixture_entities"] != magma["fixture_entities"]:
        raise AssertionError("fixture entity set differs")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
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
        subprocess.run(
            ["make", "-C", str(MAGMA), "game/test_falling_dragon_egg_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        updates = 0
        for fixture in ("supported", "fall"):
            java = request(args.port, "falling_dragon_egg_locked", {
                "mode": fixture,
                "math_seed48": MATH_SEED48,
                "world_seed48": WORLD_SEED48,
                "next_entity_id": NEXT_ENTITY_ID,
            })
            compare(fixture, java, c_result(
                fixture, java["origin_x"], java["origin_z"]))
            updates += len(java["rows"])
        print(f"PASS java==magma: {updates} dragon-egg falling updates and 2 cases")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
