#!/usr/bin/env python3
"""Exact client EntityEgg status-3 particle comparison."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
CASES = (
    (0, 760000, 0.0, 220.0, 0.0),
    (1, 760001, 0.125, 219.875, -0.25),
    (1396, 760002, -31.5, 96.25, 47.75),
    (0x123456789ABC, 760003, 100.5, 64.0, -100.5),
    ((1 << 48) - 1, 760004, -0.0, 255.875, 0.0),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25699)
    parser.add_argument(
        "--native", type=pathlib.Path,
        default=MAGMA / "game" / "test_egg_client_status_oracle")
    args = parser.parse_args()
    subprocess.run([
        "make", "-C", str(MAGMA), "game/test_egg_client_status_oracle",
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
        for seed48, eid, x, y, z in CASES:
            java = request(args.port, "egg_client_status_locked", {
                "client_seed48": seed48, "eid": eid,
                "x": x, "y": y, "z": z,
            })
            raw = subprocess.check_output([
                str(args.native.resolve()), str(seed48), str(eid),
                repr(x), repr(y), repr(z),
            ], text=True)
            native = json.loads(raw)
            if native != java:
                keys = sorted(key for key in set(native) | set(java)
                              if native.get(key) != java.get(key))
                raise AssertionError(
                    f"seed {seed48} mismatch in {keys}\n"
                    f"Java:  {java}\nNative: {native}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(CASES)} exact egg client statuses")


if __name__ == "__main__":
    main()
