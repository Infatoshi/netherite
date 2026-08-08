#!/usr/bin/env python3
"""Compare the real 1.11.2 Witch loot path with shared CPU code."""
import argparse
import json
import subprocess
import time
from pathlib import Path

from test_dragon_crystal_notification import request


MAGMA = Path(__file__).resolve().parents[1]
CASES = (
    # Raw Random seeds cover every Witch loot entry, zero-count discard,
    # one/two/three emitted stacks, all supported Looting levels, and the
    # disabled gamerule path.  Math seeds exercise EntityItem construction.
    (0, 0, 100, 0, True),
    (1, 1, 101, 0, True),
    (2, 281474976710655, 102, 0, True),
    (3, 67890, 103, 0, True),
    (5, 123456789, 104, 0, True),
    (10, 424242, 105, 0, True),
    (11, 999999, 106, 0, True),
    (7, 7777777, 107, 1, True),
    (9, 987654321, 108, 1, True),
    (1, 13579, 109, 2, True),
    (22, 24680, 110, 3, True),
    (281474976710655, 281474976710655, 111, 3, True),
    (12345, 67890, 112, 3, False),
)


def native(entity_seed, math_seed, next_id, looting, enabled):
    proc = subprocess.run(
        [str(MAGMA / "game" / "test_witch_loot_oracle"),
         str(entity_seed), str(math_seed), str(next_id), str(looting),
         "1" if enabled else "0"],
        check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA), "game/test_witch_loot_oracle"],
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
        for entity_seed, math_seed, next_id, looting, enabled in CASES:
            java = request(args.port, "witch_loot_locked", {
                "entity_seed48": entity_seed,
                "math_seed48": math_seed,
                "next_entity_id": next_id,
                "looting": looting,
                "do_mob_loot": enabled,
            })
            cpu = native(
                entity_seed, math_seed, next_id, looting, enabled)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"entity={entity_seed} math={math_seed} id={next_id} "
                    f"looting={looting} enabled={enabled}: {mismatch}: "
                    f"Java={java} CPU={cpu}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print("PASS real Java/shared CPU: Witch loot selection, Looting bonus, "
          "zero-count discard, EntityItem fields, RNG, and gamerule gating")


if __name__ == "__main__":
    main()
