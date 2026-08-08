#!/usr/bin/env python3
"""Lock Rabbit combat, jump thresholds, and persistence to Java 1.11.2."""

import argparse
import time

from test_dragon_crystal_notification import request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
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
        row = request(args.port, "rabbit_locked")
        expected = {
            "ordinary_hit": True,
            "ordinary_health": 7.0,
            "killer_hit": True,
            "killer_health": 2.0,
            "killer_armor": 8.0,
            "slow_jump": 0.2,
            "fast_jump": 0.3,
            "obstacle_jump": 0.5,
            "jump_ticks": 0,
            "jump_duration": 10,
            "nbt_type": 99,
            "nbt_carrot_ticks": 40,
        }
        mismatch = {
            key: (row.get(key), value)
            for key, value in expected.items()
            if row.get(key) != value
        }
        if mismatch:
            raise AssertionError(f"{mismatch}; row={row!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print("PASS real 1.11.2 Rabbit: combat, jump thresholds, and NBT")


if __name__ == "__main__":
    main()
