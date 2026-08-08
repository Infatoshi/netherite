#!/usr/bin/env python3
"""Compare the 1.11.2 hut-witch pre-first-tick boundary to shared CPU."""
import argparse
import json
import struct
import subprocess
import time
from pathlib import Path

from test_dragon_crystal_notification import request


MAGMA = Path(__file__).resolve().parents[1]
SEEDS = (0, 1, 23, 42, 0x123456789ABC, (1 << 48) - 1)


def native(seed48):
    proc = subprocess.run(
        [str(MAGMA / "game" / "test_swamp_witch_oracle"), str(seed48)],
        check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)


def f32(value):
    return struct.pack("=f", float(value))


def compare(java, cpu, label):
    float32 = ("health", "max_health", "yaw", "pitch", "width", "height",
               "eye_height")
    exact = ("ok", "x", "y", "z", "vx", "vy", "vz", "movement_speed",
             "follow_range", "fire", "air", "persistence", "on_ground",
             "left_handed", "drinking", "mainhand_empty", "entity_seed48",
             "entity_have_gaussian", "entity_next_gaussian")
    for field in float32:
        if f32(java[field]) != f32(cpu[field]):
            raise AssertionError(
                f"{label}: {field}: Java={java[field]} CPU={cpu[field]}")
    for field in exact:
        if java[field] != cpu[field]:
            raise AssertionError(
                f"{label}: {field}: Java={java[field]} CPU={cpu[field]}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA), "game/test_swamp_witch_oracle"],
        check=True)
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
        handed = set()
        for seed48 in SEEDS:
            java = request(args.port, "swamp_witch_locked", {
                "entity_seed48": seed48,
            })
            cpu = native(seed48)
            compare(java, cpu, f"seed48 {seed48}")
            handed.add(java["left_handed"])
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    if handed != {False, True}:
        raise AssertionError(f"left-hand branch not covered: {handed}")
    print("PASS real Java/shared CPU: hut witch position/base state, "
          "attributes, persistence, initial-spawn RNG, and both hand branches")


if __name__ == "__main__":
    main()
