#!/usr/bin/env python3
"""Compare non-first-update Witch water entry with Java 1.11.2."""
import argparse
import json
import subprocess
import time
from pathlib import Path

from test_dragon_crystal_notification import request


MAGMA = Path(__file__).resolve().parents[1]
SEEDS = (0, 1, 4, 11, 402, 12345, 281474976710655)


def native(seed):
    proc = subprocess.run(
        [str(MAGMA / "game" / "test_witch_water_entry_oracle"),
         str(seed)], check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA),
         "game/test_witch_water_entry_oracle"], check=True)
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
        for seed in SEEDS:
            java = request(args.port, "witch_water_entry_locked", {
                "entity_seed48": seed,
            })
            cpu = native(seed)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"seed={seed}: {mismatch}: Java={java} CPU={cpu}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print("PASS real Java/shared CPU: Witch dry-to-water splash sound, "
          "26 particle calls, motion, wet/fire/fall state, and RNG cursor")


if __name__ == "__main__":
    main()
