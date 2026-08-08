#!/usr/bin/env python3
"""Bit-compare AbstractHorse explicit dismount geometry with Java 1.11.2."""

import argparse
import json
import pathlib
import struct
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
NATIVE = MAGMA / "game" / "test_horse_dismount_oracle"
YAWS = (-90.0, -45.0, 0.0, 30.0, 45.0, 89.0, 90.0, 135.0, 180.0)
CASES = tuple(
    (f"{layout}_yaw_{yaw:g}_{'left' if left else 'right'}",
     layout, yaw, left)
    for left in (False, True)
    for yaw in YAWS
    for layout in ("open", "first_blocked", "twice_blocked")
)


def bits_double(text):
    return struct.unpack(">d", bytes.fromhex(text))[0]


def native(case, java):
    horse_x, horse_y, horse_z = map(
        bits_double, java["horse_position_bits"])
    raw = subprocess.check_output([
        str(NATIVE), case[1], repr(case[2]), "1" if case[3] else "0",
        repr(horse_x), repr(horse_y), repr(horse_z),
        str(java["player_eid"]), str(java["horse_eid"]),
    ], text=True)
    return json.loads(raw)


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
        subprocess.run([
            "make", "-C", str(MAGMA), "game/test_horse_dismount_oracle",
        ], check=True, stdout=subprocess.DEVNULL)
        for index, case in enumerate(cases):
            name, layout, yaw, left_handed = case
            java = request(args.port, "horse_dismount_locked", {
                "layout": layout,
                "yaw": yaw,
                "left_handed": left_handed,
                "next_entity_id": 695000 + index,
            })
            magma = native(case, java)
            if java != magma:
                differing = {
                    key: {"java": java.get(key), "magma": magma.get(key)}
                    for key in sorted(set(java) | set(magma))
                    if java.get(key) != magma.get(key)
                }
                raise AssertionError(
                    f"{name}: {json.dumps(differing, sort_keys=True)}")
        print(
            f"PASS java==magma: {len(cases)} horse dismount hand/yaw/layouts, "
            "exact pose, AABB, passenger graph and RNG invariance")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
