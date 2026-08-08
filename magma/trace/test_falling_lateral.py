#!/usr/bin/env python3
"""Parked Java invariants for EntityFallingBlock lateral movement/collision."""

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
NEXT_ENTITY_ID = 510000
HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent


def close(label, java, magma, tolerance=1e-15):
    if not math.isclose(float(java), float(magma), rel_tol=0.0,
                        abs_tol=tolerance):
        raise AssertionError(f"{label}: java={java!r} magma={magma!r}")


def normalized_blocks(result):
    ox = int(result["origin_x"])
    oz = int(result["origin_z"])
    by = int(result["base_y"])
    return sorted((int(x) - ox, int(y) - by, int(z) - oz,
                   int(block), int(meta))
                  for x, y, z, block, meta in result["final_blocks"])


def normalized_schedule(result):
    ox = int(result["origin_x"])
    oz = int(result["origin_z"])
    by = int(result["base_y"])
    return sorted((int(x) - ox, int(y) - by, int(z) - oz,
                   int(block), int(delay), int(priority), int(order))
                  for x, y, z, block, delay, priority, order
                  in result["scheduled"])


def c_result(fixture, origin_x, origin_z):
    raw = subprocess.check_output(
        [str(MAGMA / "game" / "test_falling_lateral_oracle"), fixture,
         str(origin_x), str(origin_z)],
        text=True)
    return json.loads(raw)


def validate_java(fixture, result):
    if result["fixture"] != fixture or result["source_block"] != 0:
        raise AssertionError(f"fixture/source: {result!r}")
    rows = result["rows"]
    if not rows or any(len(row) != 13 for row in rows):
        raise AssertionError("missing complete falling rows")
    for number, row in enumerate(rows, 1):
        if row[0] != number or row[1] != number:
            raise AssertionError(f"fallTime row {number}: {row!r}")
    if result["math_seed48"] != MATH_SEED48:
        raise AssertionError("lateral fixture advanced Math.random")
    if result["world_seed48"] != WORLD_SEED48:
        raise AssertionError("lateral fixture advanced World.rand")
    if result["next_entity_id"] != NEXT_ENTITY_ID + 1:
        raise AssertionError("falling constructor EID cursor")
    if result["fixture_entities"]:
        raise AssertionError("direct fixture leaked a world entity")
    expected_vx = 0.75 if fixture == "wall" else 0.35
    expected_vz = 0.0 if fixture == "wall" else 0.15
    if result["initial_vx"] != expected_vx or result["initial_vz"] != expected_vz:
        raise AssertionError("initial lateral velocity")
    if (len(rows) != 13 or not rows[-1][2] or not rows[-1][9]
            or not rows[-1][11]):
        raise AssertionError(f"landing lifecycle: {rows!r}")
    if fixture == "wall":
        row = rows[0]
        if not row[10] or row[11] or row[9]:
            raise AssertionError(f"wall collision flags: {row!r}")
        expected_x = float(result["origin_x"]) + 0.5099999904632568
        if abs(float(row[3]) - expected_x) > 1e-15:
            raise AssertionError(f"wall-clipped x: {row[3]!r}")
        expected_cell = (0, -3, 0, 12, 0)
    elif rows[0][10] or rows[0][11]:
        raise AssertionError(f"free first tick unexpectedly collided: {rows[0]!r}")
    else:
        expected_cell = (4, -3, 2, 12, 0)
    sand = [cell for cell in normalized_blocks(result) if cell[3] == 12]
    if sand != [expected_cell]:
        raise AssertionError(f"landing cell: {sand!r}")
    expected_tick = expected_cell[:3] + (12, 2, 0, 1)
    predecessor = (9, -4, 2, 1, 2, 0, 0)
    if normalized_schedule(result) != sorted([predecessor, expected_tick]):
        raise AssertionError(f"landing schedule: {result['scheduled']!r}")


def compare(fixture, java, magma):
    validate_java(fixture, java)
    for field in ("fixture", "origin_x", "origin_z", "base_y",
                  "initial_vx", "initial_vz",
                  "source_block", "math_seed48", "world_seed48",
                  "next_entity_id"):
        if java[field] != magma[field]:
            raise AssertionError(
                f"{field}: java={java[field]!r} magma={magma[field]!r}")
    if len(java["rows"]) != len(magma["rows"]):
        raise AssertionError(
            f"row count: java={len(java['rows'])} magma={len(magma['rows'])}")
    for index, (jrow, crow) in enumerate(
            zip(java["rows"], magma["rows"]), 1):
        for field in (0, 1, 2, 9, 10, 11):
            if jrow[field] != crow[field]:
                raise AssertionError(
                    f"row {index} field {field}: "
                    f"java={jrow[field]!r} magma={crow[field]!r}")
        close(f"row {index} x",
              float(jrow[3]) - float(java["origin_x"]),
              float(crow[3]) - float(magma["origin_x"]), 1e-14)
        close(f"row {index} y", jrow[4], crow[4], 1e-13)
        close(f"row {index} z",
              float(jrow[5]) - float(java["origin_z"]),
              float(crow[5]) - float(magma["origin_z"]), 1e-14)
        for field, name in ((6, "vx"), (7, "vy"), (8, "vz")):
            close(f"row {index} {name}", jrow[field], crow[field])
        if struct.pack("!f", float(jrow[12])) != struct.pack(
                "!f", float(crow[12])):
            raise AssertionError(
                f"row {index} fallDistance: "
                f"java={jrow[12]!r} magma={crow[12]!r}")
    if normalized_blocks(java) != normalized_blocks(magma):
        raise AssertionError("final raw blocks differ")
    if normalized_schedule(java) != normalized_schedule(magma):
        raise AssertionError("scheduled callback differs")
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
            ["make", "-C", str(MAGMA), "game/test_falling_lateral_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        for fixture, command in (("free", "falling_lateral_free_locked"),
                                 ("wall", "falling_lateral_wall_locked")):
            result = request(args.port, command, {
                "math_seed48": MATH_SEED48,
                "world_seed48": WORLD_SEED48,
                "next_entity_id": NEXT_ENTITY_ID,
            })
            compare(fixture, result, c_result(
                fixture, result["origin_x"], result["origin_z"]))
        print("PASS java==magma: 26 lateral falling updates and 2 cases")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
