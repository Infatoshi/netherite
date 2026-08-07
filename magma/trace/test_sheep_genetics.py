#!/usr/bin/env python3
"""Compare the complete vanilla sheep child-color matrix with magma."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
SEEDS = (0, 1 << 47)


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
        time.sleep(2.0)
        subprocess.run(
            ["make", "-C", str(MAGMA), "game/test_sheep_genetics_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        java = request(args.port, "sheep_child_color_locked", {
            "world_seed48s": list(SEEDS),
        })
        raw = subprocess.check_output([
            str(MAGMA / "game" / "test_sheep_genetics_oracle"),
            str(SEEDS[0]), str(SEEDS[1]),
        ], text=True)
        magma = json.loads(raw)
        if java != magma:
            java_rows = java.get("rows", [])
            magma_rows = magma.get("rows", [])
            for index, pair in enumerate(zip(java_rows, magma_rows)):
                if pair[0] != pair[1]:
                    raise AssertionError(
                        f"row {index}: java={pair[0]!r} magma={pair[1]!r}")
            raise AssertionError(
                f"matrix metadata differs: java={len(java_rows)} rows, "
                f"magma={len(magma_rows)} rows")
        recipe_rows = sum(
            row["world_seed48"] == row["seed48"] for row in java["rows"])
        print("PASS java==magma: 512 sheep-genetics rows, "
              f"{recipe_rows} recipe rows and exact fallback RNG cursors")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
