#!/usr/bin/env python3
"""Verify EntityStray's arrow payload against the parked real 1.11.2 server."""

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
        row = request(args.port, "stray_arrow_locked")
        expected = {
            "class": "net.minecraft.entity.projectile.EntityTippedArrow",
            "potion": "",
            "effect_count": 1,
            "id": 2,
            "amplifier": 0,
            "duration": 600,
            "ambient": False,
            "show_particles": True,
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
    print("PASS real 1.11.2 Stray arrow: tipped Slowness-I 600 payload")


if __name__ == "__main__":
    main()
