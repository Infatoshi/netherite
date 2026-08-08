#!/usr/bin/env python3
"""Compare terminal Slime/Magma Cube splitting with real Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
CASES = tuple(
    (entity_type, size, seed, 67890 + index * 101,
     1000 + index * 8, enabled)
    for index, (entity_type, size, seed, enabled) in enumerate(
        (entity_type, size, seed, enabled)
        for entity_type in ("slime", "magma_cube")
        for size in (1, 2, 4)
        for seed in (0, 402, (1 << 48) - 1)
        for enabled in (True, False)
    )
)


def native(case):
    entity_type, size, seed, math_seed, next_id, enabled = case
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_slime_terminal_oracle"),
        entity_type, str(size), str(seed), str(math_seed), str(next_id),
        str(int(enabled)),
    ], text=True)
    return json.loads(raw)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    subprocess.run([
        "make", "-C", str(MAGMA), "game/test_slime_terminal_oracle",
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
        for case in CASES:
            entity_type, size, seed, math_seed, next_id, enabled = case
            java = request(args.port, "slime_terminal_locked", {
                "type": entity_type,
                "size": size,
                "entity_seed48": seed,
                "math_seed48": math_seed,
                "next_entity_id": next_id,
                "do_mob_loot": enabled,
            })
            cpu = native(case)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"{case}: {mismatch}: Java={java!r} CPU={cpu!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/shared CPU: {len(CASES)} exact Slime/Magma terminal rows")


if __name__ == "__main__":
    main()
