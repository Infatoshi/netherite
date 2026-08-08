#!/usr/bin/env python3
"""Exact EntityArrow block-impact comparison against Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]


def native(case):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_arrow_block_impact_oracle"),
        str(case["block"]), str(case["meta"]),
        str(case["arrow_seed48"]),
        repr(case["x"]), repr(case["y"]), repr(case["z"]),
        repr(case["vx"]), repr(case["vy"]), repr(case["vz"]),
        repr(case["hit_x"]), repr(case["hit_y"]), repr(case["hit_z"]),
        str(int(case["critical"])),
        str(case["ticks"]), str(int(case["remove_block"])),
    ], text=True)
    return json.loads(raw)


def cases():
    rows = (
        ("stone_west", 1, 0, 24.5, 220.5, 24.5,
         2.75, 0.0, 0.0, 25.0, 220.5, 24.5),
        ("wool_diagonal", 35, 14, 24.25, 220.25, 24.25,
         1.5, 0.25, 0.75, 25.0, 220.375, 24.625),
        ("log_oblique", 17, 9, 25.75, 220.8, 24.8,
         -1.2, -0.4, -0.6, 25.0, 220.55, 24.425),
        ("slab_top", 44, 5, 25.5, 221.5, 24.5,
         0.0, -2.0, 0.0, 25.5, 221.0, 24.5),
    )
    serial = 0
    for name, block, meta, x, y, z, vx, vy, vz, hx, hy, hz in rows:
        for critical in (False, True):
            yield {
                "name": name + ("_critical" if critical else ""),
                "block": block, "meta": meta,
                "arrow_seed48": (serial * 104729 + 19)
                    & ((1 << 48) - 1),
                "x": x, "y": y, "z": z,
                "vx": vx, "vy": vy, "vz": vz,
                "hit_x": hx, "hit_y": hy, "hit_z": hz,
                "critical": critical,
                "ticks": 0, "remove_block": False,
            }
            serial += 1
    for ticks, remove_block in ((1, False), (7, False), (1199, False),
                                (1200, False), (1, True)):
        yield {
            "name": f"stone_ticks_{ticks}"
                + ("_removed" if remove_block else ""),
            "block": 1, "meta": 0,
            "arrow_seed48": (serial * 104729 + 19)
                & ((1 << 48) - 1),
            "x": 24.5, "y": 220.5, "z": 24.5,
            "vx": 2.75, "vy": 0.0, "vz": 0.0,
            "hit_x": 25.0, "hit_y": 220.5, "hit_z": 24.5,
            "critical": True, "ticks": ticks,
            "remove_block": remove_block,
        }
        serial += 1


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
        "make", "-C", str(MAGMA),
        "game/test_arrow_block_impact_oracle",
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
            java = request(args.port, "arrow_block_impact_locked", case)
            cpu = native(case)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"{case['name']}: {mismatch}: "
                    f"Java={java!r} CPU={cpu!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} exact arrow block impacts")


if __name__ == "__main__":
    main()
