#!/usr/bin/env python3
"""Exact EntityArrow.onHit comparison against real Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]


def native(case):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_arrow_impact_oracle"),
        str(case["target_type"]), str(case["health"]),
        str(case["hurt_resistant_time"]), str(case["last_damage"]),
        str(case["target_fire_ticks"]), str(int(case["on_ground"])),
        str(case["target_seed48"]), str(case["arrow_seed48"]),
        repr(case["vx"]), repr(case["vy"]), repr(case["vz"]),
        repr(case["arrow_damage"]), str(case["knockback"]),
        str(int(case["critical"])), str(case["arrow_fire_ticks"]),
        str(int(case["player_owned"])),
    ], text=True)
    return json.loads(raw)


def cases():
    variants = [
        ("plain", 3.0, 0.0, 0.0, 2.0, 0, False, -1, 0, 0.0, -1, True),
        ("angled", 1.25, 0.5, -0.75, 2.0, 0, False, -1, 0, 0.0, 40, True),
        ("punch_ground", 2.2, 0.1, 0.7, 3.5, 2, False, -1, 0, 0.0, -1, True),
        ("punch_air", 2.2, -0.4, 0.7, 3.5, 2, False, -1, 0, 0.0, -1, True),
        ("flame", 1.0, 0.0, 0.0, 2.0, 0, False, 100, 0, 0.0, -1, True),
        ("critical_0", 1.0, 0.0, 0.0, 2.0, 0, True, -1, 0, 0.0, -1, True),
        ("critical_95", 1.0, 0.0, 0.0, 2.0, 0, True, -1, 0, 0.0, -1, True),
        ("reject_flame", 3.0, 0.0, 0.0, 2.0, 1, False, 100, 15, 10.0, -1, True),
        ("differential", 3.0, 0.0, 0.0, 2.0, 1, False, -1, 15, 4.0, -1, True),
        ("source_plain", 1.1, 0.1, 0.0, 2.0, 0, False, -1, 0, 0.0, -1, False),
        ("source_reject", 1.1, 0.1, 0.0, 2.0, 0, False, -1, 15, 10.0, -1, False),
        ("source_differential", 1.1, 0.1, 0.0, 2.0, 0, False, -1, 15, 2.0, -1, False),
    ]
    serial = 0
    for target_type in (0, 1):
        for (name, vx, vy, vz, damage, knockback, critical,
             arrow_fire, hurt_resistant, last_damage,
             target_fire, player_owned) in variants:
            if name == "critical_95":
                arrow_seed = 95
            else:
                arrow_seed = serial * 7919 & ((1 << 48) - 1)
            yield {
                "name": ("pig" if target_type == 0 else "zombie")
                    + "_" + name,
                "target_type": target_type,
                "health": 10.0 if target_type == 0 else 20.0,
                "hurt_resistant_time": hurt_resistant,
                "last_damage": last_damage,
                "target_fire_ticks": target_fire,
                "on_ground": name != "punch_air",
                "target_seed48": (serial * 31337 + 17)
                    & ((1 << 48) - 1),
                "arrow_seed48": arrow_seed,
                "vx": vx, "vy": vy, "vz": vz,
                "arrow_damage": damage,
                "knockback": knockback,
                "critical": critical,
                "arrow_fire_ticks": arrow_fire,
                "player_owned": player_owned,
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
        "make", "-C", str(MAGMA), "game/test_arrow_impact_oracle",
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
            java = request(args.port, "arrow_impact_locked", case)
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
    print(f"PASS real Java/native: {len(selected)} exact arrow impacts")


if __name__ == "__main__":
    main()
