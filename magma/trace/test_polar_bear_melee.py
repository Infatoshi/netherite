#!/usr/bin/env python3
"""Lock EntityPolarBear's private melee state machine to Java 1.11.2."""

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
        row = request(args.port, "polar_bear_melee_locked")
        expected = {
            "far_attack_tick": 20,
            "far_standing": False,
            "warning_attack_tick": 10,
            "warning_sound_ticks": 40,
            "warning_standing": True,
            "hit_attack_tick": 20,
            "hit_standing": False,
            "target_health": 4.0,
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
    print("PASS real 1.11.2 Polar Bear melee: reach, stand, warning, 6 damage")


if __name__ == "__main__":
    main()
