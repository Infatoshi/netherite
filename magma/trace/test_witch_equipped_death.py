#!/usr/bin/env python3
"""Compare equipped-Witch post-lethal drops, XP, and RNG with Java 1.11.2."""
import argparse
import json
import subprocess
import time
from pathlib import Path

from test_dragon_crystal_notification import request


MAGMA = Path(__file__).resolve().parents[1]
CASES = (
    # Raw Random seeds cover equipment drop/no-drop, XP totals 6/7/8,
    # zero through three loot-table stacks, Looting, and cursor extremes.
    (0, 0, 0, 100, 0, True),
    (95, 1, 2, 101, 0, True),
    (117, 281474976710655, 3, 102, 0, True),
    (130, 67890, 123456789, 103, 0, True),
    (274, 424242, 987654321, 104, 0, True),
    (402, 999999, 7777777, 105, 3, True),
    (33, 281474976710654, 281474976710655, 106, 1, True),
    (12345, 13579, 24680, 107, 3, False),
)


def native(entity_seed, math_seed, world_seed, next_id, looting, enabled):
    proc = subprocess.run(
        [str(MAGMA / "game" / "test_witch_equipped_death_oracle"),
         str(entity_seed), str(math_seed), str(world_seed), str(next_id),
         str(looting), "1" if enabled else "0"],
        check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA),
         "game/test_witch_equipped_death_oracle"],
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
            entity_seed, math_seed, world_seed, next_id, looting, enabled = case
            java = request(args.port, "witch_equipped_death_locked", {
                "entity_seed48": entity_seed,
                "math_seed48": math_seed,
                "world_seed48": world_seed,
                "next_entity_id": next_id,
                "looting": looting,
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
    print("PASS real Java/shared CPU: equipped Witch loot, potion drop, "
          "XP totals/splits, constructors, particles, and all RNG cursors")


if __name__ == "__main__":
    main()
