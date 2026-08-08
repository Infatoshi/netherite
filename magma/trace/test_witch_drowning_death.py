#!/usr/bin/env python3
"""Compare ordinary drowning-killed Witch death with Java 1.11.2."""
import argparse
import json
import subprocess
import time
from pathlib import Path

from test_dragon_crystal_notification import request


MAGMA = Path(__file__).resolve().parents[1]
CASES = (
    # Cover zero through three Looting-0 stacks and cursor extremes.
    (0, 0, 0, 100, True),
    (3, 1, 2, 101, True),
    (33, 281474976710655, 3, 102, True),
    (82, 67890, 123456789, 103, True),
    (117, 424242, 987654321, 104, True),
    (402, 999999, 7777777, 105, True),
    (12345, 281474976710654, 281474976710655, 106, False),
)


def native(entity_seed, math_seed, world_seed, next_id, enabled):
    proc = subprocess.run(
        [str(MAGMA / "game" / "test_witch_drowning_death_oracle"),
         str(entity_seed), str(math_seed), str(world_seed), str(next_id),
         "1" if enabled else "0"],
        check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA),
         "game/test_witch_drowning_death_oracle"],
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
        for case in CASES:
            entity_seed, math_seed, world_seed, next_id, enabled = case
            java = request(args.port, "witch_drowning_death_locked", {
                "entity_seed48": entity_seed,
                "math_seed48": math_seed,
                "world_seed48": world_seed,
                "next_entity_id": next_id,
                "do_mob_loot": enabled,
            })
            cpu = native(*case)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"case={case}: {mismatch}: Java={java} CPU={cpu}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print("PASS real Java/shared CPU: drowning Witch Looting-0 drops, "
          "no equipment/XP, death feedback, particles, and all RNG cursors")


if __name__ == "__main__":
    main()
