#!/usr/bin/env python3
"""Bit-compare exact cow-milk inventory and EntityItem toss state."""

import argparse
import json
import pathlib
import struct
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
CASES = (
    ("insert", {}),
    ("replace_full", {}),
    ("drop_axial", {"mode": "drop"}),
    ("drop_angled", {
        "mode": "drop",
        "math_seed48": 0x123456789ABC,
        "player_seed48": 0x0FEDCBA98765,
        "yaw": 37.25,
        "pitch": -21.5,
    }),
)


def normalized(overrides):
    case = {
        "mode": "insert",
        "math_seed48": 0x456789ABCDEF,
        "player_seed48": 0x23456789ABCD,
        "next_entity_id": 673000,
        "yaw": 0.0,
        "pitch": 0.0,
    }
    case.update(overrides)
    return case


def native(case, java):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_cow_milking_oracle"),
        case["mode"], str(case["math_seed48"]),
        str(case["player_seed48"]), str(case["next_entity_id"]),
        str(case["yaw"]), str(case["pitch"]),
        str(java["player_x"]), str(java["player_z"]),
    ], text=True)
    return json.loads(raw)


def normalize_float_fields(result):
    for drop in result.get("drops", ()):
        for key in ("yaw", "hover_start"):
            drop[key] = struct.unpack(
                ">I", struct.pack(">f", drop[key]))[0]
    return result


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
            ["make", "-C", str(MAGMA), "game/test_cow_milking_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        for name, overrides in cases:
            case = normalized(overrides)
            java = normalize_float_fields(
                request(args.port, "milk_cow_locked", case))
            magma = normalize_float_fields(native(case, java))
            if java != magma:
                raise AssertionError(
                    f"{name}: java={java!r} magma={magma!r}")
        print(f"PASS java==magma: {len(cases)} exact cow-milk "
              "inventory/sound/RNG/EID/item cases")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
