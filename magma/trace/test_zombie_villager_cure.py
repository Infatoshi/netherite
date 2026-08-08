#!/usr/bin/env python3
"""Bit-compare the zombie-villager cure lifecycle with real 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
EID = 673000
INTERACTIONS = [
    ("survival_seed0", {}),
    ("survival_seed2", {"entity_seed48": 2}),
    ("survival_seed_max", {"entity_seed48": (1 << 48) - 1}),
    ("creative", {"entity_seed48": 2, "creative": True}),
    ("no_weakness", {"entity_seed48": 3, "weakness": False}),
    ("enchanted_apple", {"entity_seed48": 4, "meta": 1}),
    ("wrong_item", {"entity_seed48": 5, "item": 260}),
    ("empty", {"entity_seed48": 6, "count": 0}),
    ("already_converting", {
        "entity_seed48": 7, "conversion_time": 50}),
]
PROGRESS = [
    (f"bars_{seed}", {
        "mode": "progress", "entity_seed48": seed,
        "accelerators": 14, "block": "iron_bars"})
    for seed in (0, 1, 2, 3, 4, 95, 402, (1 << 48) - 1)
] + [
    (f"beds_{seed}", {
        "mode": "progress", "entity_seed48": seed,
        "accelerators": 14, "block": "bed"})
    for seed in (0, 2, 95, (1 << 48) - 1)
] + [
    ("no_accelerators", {
        "mode": "progress", "entity_seed48": 0,
        "accelerators": 0}),
    ("partial_accelerators", {
        "mode": "progress", "entity_seed48": 0,
        "accelerators": 5}),
]
FINISH = [
    ("adult_noai", {
        "mode": "finish", "entity_seed48": 0,
        "profession": 3, "no_ai": True, "conversion_time": 1}),
    ("child", {
        "mode": "finish", "entity_seed48": 2,
        "profession": 5, "child": True, "conversion_time": 1}),
]
AUDIO = [
    (f"audio_{seed}", {"mode": "audio", "entity_seed48": seed})
    for seed in (0, 2, 95, 402, (1 << 48) - 1)
]


def normalized(overrides):
    case = {
        "mode": "interact",
        "entity_seed48": 0,
        "next_entity_id": EID,
        "profession": 3,
        "child": False,
        "no_ai": False,
        "weakness": True,
        "item": 322,
        "meta": 0,
        "count": 2,
        "creative": False,
        "conversion_time": -1,
        "accelerators": 0,
        "block": "iron_bars",
    }
    case.update(overrides)
    return case


def native(case, java):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_zombie_villager_cure_oracle"),
        case["mode"], str(java["world_x"]), str(java["world_z"]),
        str(case["entity_seed48"]), str(case["next_entity_id"]),
        str(case["profession"]), str(int(case["child"])),
        str(int(case["no_ai"])), str(int(case["weakness"])),
        str(case["item"]), str(case["meta"]), str(case["count"]),
        str(int(case["creative"])), str(case["conversion_time"]),
        str(case["accelerators"]), case["block"],
    ], text=True)
    return json.loads(raw)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    cases = [(name, normalized(overrides))
             for name, overrides in INTERACTIONS + PROGRESS + FINISH + AUDIO
             if not args.case or name == args.case]
    if not cases:
        parser.error(f"unknown case: {args.case}")
    subprocess.run([
        "make", "-C", str(MAGMA),
        "game/test_zombie_villager_cure_oracle",
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
        for name, case in cases:
            java = request(args.port, "zombie_villager_cure_locked", case)
            cpu = native(case, java)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"{name}: {mismatch}: Java={java!r} CPU={cpu!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/shared CPU: {len(cases)} exact zombie-villager "
          "cure interaction, accelerator, RNG, replacement, and event rows")


if __name__ == "__main__":
    main()
