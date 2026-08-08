#!/usr/bin/env python3
"""Compare real 1.11.2 firework client audio with the native sound seam."""

import argparse
import json
from pathlib import Path
import subprocess
import time

from test_dragon_crystal_notification import request


CASES = [
    ([0], False, 0.0),
    ([1], False, 10.0),
    ([2], False, 10.0001),
    ([4], False, 15.999),
    ([0], False, 16.0),
    ([0, 2, 4], False, 20.0),
    ([3], True, 8.0),
    ([2, 4], True, 24.0),
]
BLAST_SEED = 0x123456789ABC
TWINKLE_SEED = 0x0FEDCBA98765


def native(binary, types, flicker, distance):
    large = len(types) >= 3 or 1 in types
    result = subprocess.run(
        [str(binary), str(len(types)), str(int(large)), str(int(flicker)),
         repr(distance), str(BLAST_SEED), str(TWINKLE_SEED)],
        check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


def comparable(result):
    keys = ("tick", "sound", "category", "volume", "pitch_bits",
            "distance_delay", "delay_ticks")
    return {
        "explosion_count": result["explosion_count"],
        "large": result["large"],
        "flicker": result["flicker"],
        "max_age": result["max_age"],
        "events": [{key: event[key] for key in keys}
                   for event in result["events"]],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", type=Path, default=(
        Path(__file__).resolve().parents[1] / "game/test_firework_audio"))
    args = parser.parse_args()
    deadline = time.monotonic() + 120.0
    while True:
        try:
            request(args.port, "obs")
            break
        except (OSError, RuntimeError, ValueError):
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.5)
    locked = False
    negative_checked = False
    try:
        request(args.port, "server_step_lock")
        locked = True
        for types, flicker, distance in CASES:
            java = request(args.port, "firework_audio_locked", {
                "types": types,
                "flicker": flicker,
                "distance": distance,
                "blast_seed48": BLAST_SEED,
                "twinkle_seed48": TWINKLE_SEED,
            })
            actual_distance = java["events"][0]["distance_sq"] ** 0.5
            actual = native(args.native, types, flicker, actual_distance)
            if comparable(java) != comparable(actual):
                raise AssertionError(
                    f"types={types} flicker={flicker} distance={distance}\n"
                    f"java={comparable(java)}\nnative={comparable(actual)}")
            if not negative_checked:
                sabotaged = json.loads(json.dumps(actual))
                sabotaged["events"][0]["delay_ticks"] += 1
                if comparable(java) == comparable(sabotaged):
                    raise AssertionError("delay sabotage escaped the comparator")
                negative_checked = True
        print("PASS real Java/native: 8 firework blast, far, delay, and "
              "twinkle cases plus delay-negative control")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
