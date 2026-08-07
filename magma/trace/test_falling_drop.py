#!/usr/bin/env python3
"""Compare shaped-support falling-block drops to real MC 1.11.2."""

import argparse
import json
import math
import pathlib
import struct
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
MATH_SEED48 = 0x123456789ABC
NEXT_ENTITY_ID = 500000


def close(label, java, magma, tolerance):
    if not math.isclose(float(java), float(magma), rel_tol=0.0,
                        abs_tol=tolerance):
        raise AssertionError(f"{label}: java={java!r} magma={magma!r}")


def c_result(block, support, support_meta=0, entity_drops=True):
    subprocess.run(
        ["make", "-C", str(MAGMA), "game/test_falling_drop_oracle"],
        check=True, stdout=subprocess.DEVNULL)
    raw = subprocess.check_output(
        [str(MAGMA / "game" / "test_falling_drop_oracle"),
         str(block), str(support), str(support_meta),
         str(int(entity_drops))],
        text=True)
    return json.loads(raw)


def compare(block, support, support_meta, entity_drops, java, magma):
    drop_step = {44: 12, 60: 10, 88: 11, 92: 12, 116: 11, 171: 13,
                 208: 10, 70: 13, 72: 13, 147: 13, 148: 13}.get(
        support, {0: 13, 1: 12, 2: 12, 3: 12, 4: 12,
                  5: 11, 6: 11, 7: 11}[support_meta])
    support_top = {
        44: 0.5, 60: 0.9375, 88: 0.875, 92: 0.5, 116: 0.75,
        171: 0.0625, 208: 0.9375,
    }.get(support, support_meta / 8.0)
    if (len(java["rows"]) != drop_step
            or len(magma["rows"]) != drop_step):
        raise AssertionError(f"expected {drop_step} falling updates")
    replaces_snow = support == 78 and support_meta == 0
    no_item = replaces_snow or not entity_drops
    java_support_y = (
        float(java["rows"][-1][4]) - support_top
        if no_item else
        float(java["constructor_item"]["y"]) - support_top
    )
    for index, (jrow, crow) in enumerate(
            zip(java["rows"], magma["rows"]), 1):
        for field in (0, 1, 2):
            if jrow[field] != crow[field]:
                raise AssertionError(
                    f"row {index} field {field}: {jrow[field]} != {crow[field]}")
        close(f"row {index} x", float(jrow[3]) - math.floor(jrow[3]) - 0.5,
              float(crow[3]) - 26.5, 1e-15)
        close(f"row {index} y", float(jrow[4]) - java_support_y,
              float(crow[4]) - 77.0, 1e-13)
        close(f"row {index} z", float(jrow[5]) - math.floor(jrow[5]) - 0.5,
              float(crow[5]) - 8.5, 1e-15)
        for field, name in ((6, "vx"), (7, "vy"), (8, "vz")):
            close(f"row {index} {name}", jrow[field], crow[field], 1e-15)

    constructor = java["constructor_item"]
    if no_item:
        if constructor is not None or java["ticked_item"] is not None:
            raise AssertionError("no-item falling case unexpectedly dropped an item")
        if magma["ticked_item"] is not None:
            raise AssertionError("magma no-item falling case unexpectedly dropped an item")
        expected = (
            ("source_block", 0), ("source_meta", 0),
            ("support_block", block if replaces_snow else support),
            ("support_meta", 0 if replaces_snow else support_meta),
            ("math_seed48", MATH_SEED48),
            ("next_entity_id", NEXT_ENTITY_ID + 1),
        )
        for field, value in expected:
            if java[field] != value or magma[field] != value:
                raise AssertionError(
                    f"{field}: java={java[field]} magma={magma[field]}")
        return
    expected_constructor = {
        "eid": NEXT_ENTITY_ID + 1,
        "vx": 0.012723377905786037,
        "vy": 0.20000000298023224,
        "vz": 0.06541676074266434,
        "item": block,
        "count": 1,
        "meta": 0,
        "age": 0,
        "pickup_delay": 10,
    }
    for field in ("eid", "item", "count", "meta", "age", "pickup_delay"):
        if constructor[field] != expected_constructor[field]:
            raise AssertionError(f"constructor {field}: {constructor[field]}")
    for field in ("vx", "vy", "vz"):
        close(f"constructor {field}", constructor[field],
              expected_constructor[field], 1e-15)
    if struct.pack("!f", float(constructor["yaw"])) != struct.pack(
            "!f", 346.55627):
        raise AssertionError(f"constructor yaw: {constructor['yaw']}")

    jitem = java["ticked_item"]
    citem = magma["ticked_item"]
    for field in ("eid", "item", "count", "meta", "age", "pickup_delay"):
        if jitem[field] != citem[field]:
            raise AssertionError(
                f"ticked item {field}: java={jitem[field]} magma={citem[field]}")
    java_center_x = math.floor(float(constructor["x"])) + 0.5
    java_center_z = math.floor(float(constructor["z"])) + 0.5
    close("item x", float(jitem["x"]) - java_center_x,
          float(citem["x"]) - 26.5, 1e-13)
    close("item y", float(jitem["y"]) - java_support_y,
          float(citem["y"]) - 77.0, 1e-13)
    close("item z", float(jitem["z"]) - java_center_z,
          float(citem["z"]) - 8.5, 1e-13)
    for field in ("vx", "vy", "vz"):
        close(f"item {field}", jitem[field], citem[field], 1e-15)
    if struct.pack("!f", float(jitem["yaw"])) != struct.pack(
            "!f", float(citem["yaw"])):
        raise AssertionError(
            f"item yaw: java={jitem['yaw']} magma={citem['yaw']}")
    final_support_meta = 1 if support in (72, 147, 148) else support_meta
    for field, expected in (
            ("source_block", 0), ("source_meta", 0),
            ("support_block", support), ("support_meta", final_support_meta),
            ("math_seed48", 52327523130500),
            ("next_entity_id", NEXT_ENTITY_ID + 2)):
        if java[field] != expected or magma[field] != expected:
            raise AssertionError(
                f"{field}: java={java[field]} magma={magma[field]}")


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
        updates = 0
        cases = 0
        for support, support_meta in (
                *((support, 0) for support in (44, 88, 92, 116, 171, 208)),
                *((support, 0) for support in (70, 72, 147, 148)),
                *((60, meta) for meta in range(8)),
                *((78, meta) for meta in range(8))):
            for block in (12, 13):
                java = request(args.port, "falling_drop_locked", {
                    "block": block,
                    "support": support,
                    "support_meta": support_meta,
                    "math_seed48": MATH_SEED48,
                    "next_entity_id": NEXT_ENTITY_ID,
                })
                compare(block, support, support_meta, True, java,
                        c_result(block, support, support_meta))
                updates += len(java["rows"])
                cases += 1
        for block in (12, 13):
            java = request(args.port, "falling_drop_locked", {
                "block": block,
                "support": 44,
                "support_meta": 0,
                "entity_drops": False,
                "math_seed48": MATH_SEED48,
                "next_entity_id": NEXT_ENTITY_ID,
            })
            compare(block, 44, 0, False, java,
                    c_result(block, 44, 0, False))
            updates += len(java["rows"])
            cases += 1
        print(f"PASS java==magma: {updates} falling updates and "
              f"{cases} cases")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
