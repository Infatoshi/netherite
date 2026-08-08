#!/usr/bin/env python3
"""Exact ItemBow release comparison against real Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]


def native(case):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_bow_release_oracle"),
        str(case["draw"]), str(case["bow_damage"]),
        str(case["arrows"]), str(case["power"]),
        str(case["punch"]), str(case["flame"]),
        str(case["infinity"]), str(case["unbreaking"]),
        str(case["player_seed48"]), str(case["arrow_seed48"]),
        str(int(case["arrow_have_gaussian"])),
        repr(case["arrow_next_gaussian"]),
        str(case["item_seed48"]), str(case["yaw"]),
        str(case["pitch"]),
    ], text=True)
    return json.loads(raw)


def cases():
    serial = 0
    for draw in (0, 1, 2, 3, 5, 20, 40):
        for mode in ("plain", "enchanted", "infinity", "empty"):
            yield {
                "name": f"draw_{draw}_{mode}",
                "draw": draw,
                "bow_damage": 0,
                "arrows": 0 if mode in ("infinity", "empty") else 2,
                "power": 5 if mode == "enchanted" else 0,
                "punch": 2 if mode == "enchanted" else 0,
                "flame": 1 if mode == "enchanted" else 0,
                "infinity": 1 if mode == "infinity" else 0,
                "unbreaking": 0,
                "player_seed48": (serial * 31337) & ((1 << 48) - 1),
                "arrow_seed48": (serial * 7919) & ((1 << 48) - 1),
                "arrow_have_gaussian": False,
                "arrow_next_gaussian": 0.0,
                "item_seed48": (serial * 104729) & ((1 << 48) - 1),
                "yaw": 0.0,
                "pitch": 0.0,
            }
            serial += 1
    for index, (yaw, pitch) in enumerate(
            ((90.0, 0.0), (-135.0, 30.0), (45.0, -45.0))):
        yield {
            "name": f"aim_{index}", "draw": 20,
            "bow_damage": 0, "arrows": 2,
            "power": 0, "punch": 0, "flame": 0,
            "infinity": 0, "unbreaking": 0,
            "player_seed48": 12345 + index,
            "arrow_seed48": 54321 + index,
            "arrow_have_gaussian": False,
            "arrow_next_gaussian": 0.0,
            "item_seed48": 999 + index,
            "yaw": yaw, "pitch": pitch,
        }
    for index, (damage, unbreaking, seed) in enumerate(
            ((383, 0, 0), (384, 0, 0), (384, 3, 0), (384, 3, 2))):
        yield {
            "name": f"wear_{index}", "draw": 20,
            "bow_damage": damage, "arrows": 2,
            "power": 0, "punch": 0, "flame": 0,
            "infinity": 0, "unbreaking": unbreaking,
            "player_seed48": seed, "arrow_seed48": 402,
            "arrow_have_gaussian": False,
            "arrow_next_gaussian": 0.0,
            "item_seed48": 95, "yaw": 0.0, "pitch": 0.0,
        }
    yield {
        "name": "cached_gaussian", "draw": 20,
        "bow_damage": 0, "arrows": 2,
        "power": 5, "punch": 2, "flame": 1,
        "infinity": 0, "unbreaking": 0,
        "player_seed48": 402, "arrow_seed48": 95,
        "arrow_have_gaussian": True,
        "arrow_next_gaussian": -0.125,
        "item_seed48": (1 << 48) - 1,
        "yaw": 15.0, "pitch": -30.0,
    }


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
        "make", "-C", str(MAGMA), "game/test_bow_release_oracle",
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
            java = request(args.port, "bow_release_locked", case)
            cpu = native(case)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"{case['name']}: {mismatch}: Java={java!r} CPU={cpu!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} exact bow releases")


if __name__ == "__main__":
    main()
