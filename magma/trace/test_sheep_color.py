#!/usr/bin/env python3
"""Bit-compare vanilla sheep fleece selection to the native spawn boundary."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
MASK48 = (1 << 48) - 1
CASES = (
    ("black", 0, 15),
    ("gray", 31, 7),
    ("silver", 19, 8),
    ("brown", 15, 12),
    ("white", 1, 0),
    ("pink", 55, 6),
)


def lcg(seed):
    return (seed * 0x5DEECE66D + 0xB) & MASK48


def next_int(seed, bound):
    while True:
        seed = lcg(seed)
        bits = seed >> 17
        value = bits % bound
        if ((bits - value + bound - 1) & 0xFFFFFFFF) < 0x80000000:
            return seed, value


def expected(seed):
    seed, roll = next_int(seed, 100)
    if roll < 5:
        return 15, seed
    if roll < 10:
        return 7, seed
    if roll < 15:
        return 8, seed
    if roll < 18:
        return 12, seed
    seed, pink = next_int(seed, 500)
    return (6 if pink == 0 else 0), seed


def native(mode, seed, sheared):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_sheep_color_oracle"),
        mode, str(seed), str(int(sheared)),
    ], text=True)
    return json.loads(raw)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    cases = [case for case in CASES if not args.case or case[0] == args.case]
    if not cases:
        parser.error(f"unknown case: {args.case}")
    locked = False
    comparisons = 0
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
            ["make", "-C", str(MAGMA), "game/test_sheep_color_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        for name, seed, wanted_color in cases:
            wanted, wanted_seed = expected(seed)
            if wanted != wanted_color:
                raise AssertionError(f"bad fixture seed for {name}")
            for mode in ("direct", "initial_spawn"):
                java = request(args.port, "sheep_color_locked", {
                    "mode": mode,
                    "world_seed48": seed,
                    "sheared": False,
                })
                magma = native(mode, seed, False)
                if java != magma:
                    raise AssertionError(
                        f"{name}/{mode}: java={java!r} magma={magma!r}")
                if java["fleece"] != wanted_color \
                        or java["world_seed48"] != wanted_seed:
                    raise AssertionError(f"{name}/{mode}: expected mismatch")
                comparisons += 1

        java = request(args.port, "sheep_color_locked", {
            "mode": "initial_spawn", "world_seed48": 55, "sheared": True,
        })
        magma = native("initial_spawn", 55, True)
        if java != magma or not java["sheared"] or java["fleece"] != 6:
            raise AssertionError(
                f"sheared-bit preservation: java={java!r} magma={magma!r}")
        comparisons += 1
        print(f"PASS java==magma: {comparisons} sheep-color selections, "
              "six outcomes and exact World.rand cursor")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
