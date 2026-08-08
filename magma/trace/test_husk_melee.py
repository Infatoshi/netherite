#!/usr/bin/env python3
"""Verify Husk empty-hand Hunger against the parked real 1.11.2 server."""

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
        cases = ((13000, 140), (1512000, 280))
        for world_time, duration in cases:
            row = request(args.port, "husk_melee_locked", {
                "world_time": world_time,
                "inhabited_time": 0,
            })
            expected = {
                "hit": True,
                "difficulty": 2,
                "hunger_duration": duration,
                "hunger_amplifier": 0,
                "player_health": 17.0,
            }
            mismatch = {
                key: (row.get(key), value)
                for key, value in expected.items()
                if row.get(key) != value
            }
            if mismatch:
                raise AssertionError(
                    f"world_time={world_time}: {mismatch}; row={row!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print("PASS real 1.11.2 Husk melee: daylight-age local difficulty 140/280 Hunger")


if __name__ == "__main__":
    main()
