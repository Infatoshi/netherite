#!/usr/bin/env python3
"""Compare EntityFallingBlock timeout branches to real MC 1.11.2."""

import argparse
import json
import math
import pathlib
import struct
import subprocess
import time

from test_dragon_crystal_notification import request


MATH_SEED48 = 0x123456789ABC
NEXT_ENTITY_ID = 500000
HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent


def close(label, java, magma, tolerance=1e-15):
    if not math.isclose(float(java), float(magma), rel_tol=0.0,
                        abs_tol=tolerance):
        raise AssertionError(f"{label}: java={java!r} magma={magma!r}")


def c_result(mode, entity_drops):
    raw = subprocess.check_output(
        [str(MAGMA / "game" / "test_falling_timeout_oracle"), mode,
         str(int(entity_drops))], text=True)
    return json.loads(raw)


def validate_java(mode, entity_drops, result):
    height = mode == "height"
    low = mode == "low"
    age = mode == "age"
    if mode not in ("height", "low", "age"):
        raise AssertionError(f"unexpected mode: {mode!r}")
    if result["mode"] != mode:
        raise AssertionError(f"mode: {result['mode']!r}")
    if result["initial_fall_time"] != (600 if age else 100):
        raise AssertionError("initial fall time")
    if result["no_gravity"] != age:
        raise AssertionError("gravity mode")
    if result["blocks_before"] != result["blocks_after"]:
        raise AssertionError("timeout changed a world block")
    if not result["blocks_unchanged"]:
        raise AssertionError("oracle reported changed world blocks")
    rows = result["rows"]
    if len(rows) != 1:
        raise AssertionError(f"expected one update row, got {len(rows)}")
    row = rows[0]
    if row[0] != 1 or row[1] != (601 if age else 101) or not row[2]:
        raise AssertionError(f"timeout row: {row!r}")
    if height:
        if not (256.0 < float(row[4]) < 258.0):
            raise AssertionError(f"height timeout y: {row[4]!r}")
        if not float(row[7]) < 0.0:
            raise AssertionError(f"height timeout gravity: {row[7]!r}")
    elif low:
        if not (0.0 < float(row[4]) < 1.0) or not float(row[7]) < 0.0:
            raise AssertionError(f"low timeout row: {row!r}")
    elif (float(row[4]) != 128.00999999046326
          or float(row[7]) != 0.0):
        raise AssertionError(f"age timeout no-gravity row: {row!r}")
    item = result["constructor_item"]
    ticked = result["ticked_item"]
    if not entity_drops:
        if item is not None or ticked is not None:
            raise AssertionError("doEntityDrops=false produced an item")
        if result["math_seed48"] != MATH_SEED48:
            raise AssertionError("no-drop timeout advanced Math.random")
        if result["next_entity_id"] != NEXT_ENTITY_ID + 1:
            raise AssertionError("no-drop timeout entity id cursor")
        return
    if item is None or ticked is None:
        raise AssertionError("doEntityDrops=true omitted item")
    for key, value in (("eid", NEXT_ENTITY_ID + 1), ("item", 12),
                       ("count", 1), ("meta", 0), ("age", 0),
                       ("pickup_delay", 10)):
        if item[key] != value:
            raise AssertionError(f"constructor {key}: {item[key]!r}")
    if ticked["eid"] != NEXT_ENTITY_ID + 1 or ticked["age"] != 1:
        raise AssertionError(f"ticked item: {ticked!r}")
    if result["next_entity_id"] != NEXT_ENTITY_ID + 2:
        raise AssertionError("drop timeout entity id cursor")
    if not 0 <= result["math_seed48"] < (1 << 48):
        raise AssertionError("invalid Math.random cursor")


def compare(mode, entity_drops, java, magma):
    validate_java(mode, entity_drops, java)
    height = mode == "height"
    low = mode == "low"
    for field in ("mode", "initial_fall_time", "no_gravity",
                  "blocks_unchanged", "math_seed48", "next_entity_id"):
        if java[field] != magma[field]:
            raise AssertionError(
                f"{field}: java={java[field]!r} magma={magma[field]!r}")
    jrow = java["rows"][0]
    crow = magma["rows"][0]
    for field in (0, 1, 2):
        if jrow[field] != crow[field]:
            raise AssertionError(
                f"row field {field}: java={jrow[field]!r} "
                f"magma={crow[field]!r}")
    close("row x", float(jrow[3]) - math.floor(jrow[3]) - 0.5,
          float(crow[3]) - 26.5)
    close("row y", jrow[4], crow[4])
    close("row z", float(jrow[5]) - math.floor(jrow[5]) - 0.5,
          float(crow[5]) - 8.5)
    for field, name in ((6, "vx"), (7, "vy"), (8, "vz")):
        close(f"row {name}", jrow[field], crow[field])
    expected_y = (257.96999999135733 if height else
                  0.9699999913573265 if low else
                  128.00999999046326)
    close("timeout row expected y", jrow[4], expected_y)

    constructor = java["constructor_item"]
    ticked = java["ticked_item"]
    cticked = magma["ticked_item"]
    if not entity_drops:
        if constructor is not None or ticked is not None or cticked is not None:
            raise AssertionError("doEntityDrops=false produced an item")
        return
    expected_constructor = {
        "eid": NEXT_ENTITY_ID + 1,
        "vx": 0.012723377905786037,
        "vy": 0.20000000298023224,
        "vz": 0.06541676074266434,
        "item": 12,
        "count": 1,
        "meta": 0,
        "age": 0,
        "pickup_delay": 10,
    }
    for field in ("eid", "item", "count", "meta", "age", "pickup_delay"):
        if constructor[field] != expected_constructor[field]:
            raise AssertionError(
                f"constructor {field}: {constructor[field]!r}")
    for field in ("vx", "vy", "vz"):
        close(f"constructor {field}", constructor[field],
              expected_constructor[field])
    if struct.pack("!f", float(constructor["yaw"])) != struct.pack(
            "!f", 346.55627):
        raise AssertionError(f"constructor yaw: {constructor['yaw']!r}")
    close("constructor x", float(constructor["x"])
          - math.floor(constructor["x"]) - 0.5, 0.0)
    close("constructor y", constructor["y"], jrow[4])
    close("constructor z", float(constructor["z"])
          - math.floor(constructor["z"]) - 0.5, 0.0)

    for field in ("eid", "item", "count", "meta", "age", "pickup_delay"):
        if ticked[field] != cticked[field]:
            raise AssertionError(
                f"ticked item {field}: java={ticked[field]!r} "
                f"magma={cticked[field]!r}")
    java_center_x = math.floor(float(constructor["x"])) + 0.5
    java_center_z = math.floor(float(constructor["z"])) + 0.5
    close("ticked item x", float(ticked["x"]) - java_center_x,
          float(cticked["x"]) - 26.5, 1e-13)
    close("ticked item y", ticked["y"], cticked["y"], 1e-13)
    close("ticked item z", float(ticked["z"]) - java_center_z,
          float(cticked["z"]) - 8.5, 1e-13)
    for field in ("vx", "vy", "vz"):
        close(f"ticked item {field}", ticked[field], cticked[field])
    if struct.pack("!f", float(ticked["yaw"])) != struct.pack(
            "!f", float(cticked["yaw"])):
        raise AssertionError(
            f"ticked item yaw: java={ticked['yaw']!r} "
            f"magma={cticked['yaw']!r}")

    expected_item_y = (258.12999999523163 if height else
                       1.1299999952316284 if low else
                       128.16999999433756)
    close("timeout item expected y", ticked["y"], expected_item_y,
          1e-13)


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
            ["make", "-C", str(MAGMA), "game/test_falling_timeout_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        cases = 0
        for mode in ("height", "low", "age"):
            for entity_drops in (True, False):
                java = request(args.port, "falling_timeout_locked", {
                    "mode": mode,
                    "entity_drops": entity_drops,
                    "math_seed48": MATH_SEED48,
                    "next_entity_id": NEXT_ENTITY_ID,
                })
                compare(mode, entity_drops, java,
                        c_result(mode, entity_drops))
                cases += 1
        print(f"PASS java==magma: {cases} falling timeout cases")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
