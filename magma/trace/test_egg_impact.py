#!/usr/bin/env python3
"""Exact EntityEgg impact comparison against Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]


def cases():
    fixtures = (
        ("no_hatch_origin", 1396, 17, 29, 0.0, 220.0, 0.0, 0.0),
        ("one_origin", 1, 31, 43, 0.0, 220.0, 0.0, 0.0),
        ("four_origin", 0, 47, 59, 0.0, 220.0, 0.0, 0.0),
        ("no_hatch_offset", 1396, 32452850, 49979706,
         0.375, 219.875, -0.125, -37.25),
        ("one_offset", 1, 67867974, 86028178,
         2.125, 231.5, 3.875, 179.75),
        ("four_offset", 0, 104395322, 122949829,
         4.5, 192.25, 5.75, -180.0),
    )
    for serial, fixture in enumerate(fixtures):
        name, egg_seed, entity_seed, uuid_seed, x, y, z, yaw = fixture
        yield {
            "name": name,
            "egg_seed48": egg_seed,
            "entity_seed48": entity_seed,
            "server_uuid_seed48": uuid_seed,
            "next_entity_id": 750000 + serial * 8,
            "x": x,
            "y": y,
            "z": z,
            "yaw": yaw,
        }


def native(case):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_egg_impact_oracle"),
        str(case["egg_seed48"]),
        str(case["entity_seed48"]),
        str(case["server_uuid_seed48"]),
        str(case["next_entity_id"]),
        repr(case["x"]),
        repr(case["y"]),
        repr(case["z"]),
        repr(case["yaw"]),
    ], text=True)
    return json.loads(raw)


def mismatch_detail(java, cpu):
    mismatch = [key for key in sorted(set(java) | set(cpu))
                if java.get(key) != cpu.get(key)]
    details = []
    if "chickens" in mismatch:
        for index, (java_row, cpu_row) in enumerate(
                zip(java["chickens"], cpu["chickens"])):
            row_mismatch = [
                key for key in sorted(set(java_row) | set(cpu_row))
                if java_row.get(key) != cpu_row.get(key)
            ]
            if row_mismatch:
                details.append(
                    f"chickens[{index}] "
                    f"{dict((key, [java_row.get(key), cpu_row.get(key)]) for key in row_mismatch)!r}")
                break
        else:
            details.append(
                f"chickens length Java={len(java['chickens'])} "
                f"CPU={len(cpu['chickens'])}")
        mismatch.remove("chickens")
    for key in mismatch:
        details.append(
            f"{key} Java={java.get(key)!r} CPU={cpu.get(key)!r}")
    return "; ".join(details)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25699)
    parser.add_argument("--case")
    args = parser.parse_args()
    selected = [case for case in cases()
                if not args.case or case["name"] == args.case]
    if not selected:
        parser.error(f"unknown case: {args.case}")
    subprocess.run([
        "make", "-C", str(MAGMA), "game/test_egg_impact_oracle",
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
            java = request(args.port, "egg_impact_locked", action)
            cpu = native(case)
            if java != cpu:
                raise AssertionError(
                    f"{case['name']}: {mismatch_detail(java, cpu)}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} exact egg impacts")


if __name__ == "__main__":
    main()
