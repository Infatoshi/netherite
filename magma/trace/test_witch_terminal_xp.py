#!/usr/bin/env python3
"""Compare real 1.11.2 Witch terminal XP with shared CPU code."""
import argparse
import json
import subprocess
import time
from pathlib import Path

from test_dragon_crystal_notification import request


MAGMA = Path(__file__).resolve().parents[1]
CASES = (
    (0, 0, 0, 100, True),
    (1, 1, 1, 103, True),
    (2, 281474976710655, 67890, 106, True),
    (3, 123456789, 281474976710655, 109, True),
    (12345, 424242, 987654321, 112, True),
    (281474976710655, 999999, 24680, 115, True),
    (67890, 13579, 7777777, 118, False),
)


def native(entity_seed, math_seed, world_seed, next_id, enabled):
    proc = subprocess.run(
        [str(MAGMA / "game" / "test_witch_terminal_xp_oracle"),
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
         "game/test_witch_terminal_xp_oracle"],
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
        for entity_seed, math_seed, world_seed, next_id, enabled in CASES:
            java = request(args.port, "witch_terminal_xp_locked", {
                "entity_seed48": entity_seed,
                "math_seed48": math_seed,
                "world_seed48": world_seed,
                "next_entity_id": next_id,
                "do_mob_loot": enabled,
            })
            cpu = native(
                entity_seed, math_seed, world_seed, next_id, enabled)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"entity={entity_seed} math={math_seed} "
                    f"world={world_seed} id={next_id} enabled={enabled}: "
                    f"{mismatch}: Java={java} CPU={cpu}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print("PASS real Java/shared CPU: ordinary Witch terminal XP split, "
          "EntityXPOrb fields, terminal RNG ordering, and gamerule gating")


if __name__ == "__main__":
    main()
