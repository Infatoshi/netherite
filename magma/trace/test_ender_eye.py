#!/usr/bin/env python3
"""Exact EntityEnderEye trajectory and terminal comparison against 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]


def cases():
    fixtures = (
        ("far_first", 7, 19, 1, False, 32.5, 220.0, -18.25, 200, 32, 50),
        ("far_rise", 17, 29, 20, False, 32.5, 220.0, -18.25, 200, 32, 50),
        ("far_turn", 27, 39, 80, False, -91.75, 218.5, 73.125, -400, 32, -300),
        ("near_target", 37, 49, 24, False, 10.25, 210.0, -4.75, 11, 208, 2),
        ("water_first", 47, 53, 1, True, 4.5, 220.0, 4.5, 180, 32, -30),
        ("terminal_drop", 1, 59, 81, False, 32.5, 220.0, -18.25, 200, 32, 50),
        ("terminal_shatter", 0, 69, 81, False, -31.25, 215.0, 18.75, 80, 32, -90),
    )
    for serial, values in enumerate(fixtures):
        (name, seed48, math_seed48, ticks, water, x, y, z,
         target_x, target_y, target_z) = values
        yield {
            "name": name,
            "seed48": seed48,
            "math_seed48": math_seed48,
            "next_entity_id": 740000 + serial * 4,
            "ticks": ticks,
            "water": water,
            "x": x,
            "y": y,
            "z": z,
            "target_x": target_x,
            "target_y": target_y,
            "target_z": target_z,
        }


def native(case):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_ender_eye_oracle"),
        str(case["seed48"]),
        str(case["math_seed48"]),
        str(case["next_entity_id"]),
        str(case["ticks"]),
        "1" if case["water"] else "0",
        repr(case["x"]),
        repr(case["y"]),
        repr(case["z"]),
        str(case["target_x"]),
        str(case["target_y"]),
        str(case["target_z"]),
    ], text=True)
    return json.loads(raw)


def mismatch_detail(java, cpu):
    keys = sorted(set(java) | set(cpu))
    mismatch = [key for key in keys if java.get(key) != cpu.get(key)]
    details = []
    if "rows" in mismatch:
        for index, (java_row, cpu_row) in enumerate(
                zip(java["rows"], cpu["rows"])):
            row_keys = sorted(set(java_row) | set(cpu_row))
            row_mismatch = [key for key in row_keys
                            if java_row.get(key) != cpu_row.get(key)]
            if row_mismatch:
                fields = {
                    key: [java_row.get(key), cpu_row.get(key)]
                    for key in row_mismatch
                }
                details.append(f"rows[{index}] {fields!r}")
                break
        else:
            details.append(
                f"rows length Java={len(java['rows'])} "
                f"CPU={len(cpu['rows'])}")
        mismatch.remove("rows")
    for key in mismatch:
        details.append(f"{key} Java={java.get(key)!r} CPU={cpu.get(key)!r}")
    return "; ".join(details)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    selected = [case for case in cases()
                if not args.case or case["name"] == args.case]
    if not selected:
        parser.error(f"unknown case: {args.case}")
    subprocess.run([
        "make", "-C", str(MAGMA), "game/test_ender_eye_oracle",
    ], check=True, stdout=subprocess.DEVNULL)
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
        for case in selected:
            action = dict(case)
            action.pop("name")
            java = request(args.port, "ender_eye_tick_locked", action)
            cpu = native(case)
            if java != cpu:
                raise AssertionError(
                    f"{case['name']}: {mismatch_detail(java, cpu)}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} exact Ender Eye cases")


if __name__ == "__main__":
    main()
