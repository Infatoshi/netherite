#!/usr/bin/env python3
"""Compare the bounded healer selection/cadence transition to real 1.11.2."""

import argparse
import os
import pathlib
import subprocess
import tempfile
import time

from test_dragon_crystal_notification import request

HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
BLAZE = MAGMA.parent / "blaze"

CASES = [
    ("persist_heal", dict(seed=1, ticks_existed=8, healing=0, steps=4,
                          crystals=[[0, 0, 10, True]])),
    ("select_gate", dict(seed=3, ticks_existed=0, healing=-1, steps=2,
                         crystals=[[20, 0, 0, True], [5, 0, 0, True]])),
    ("dead_clear", dict(seed=1, ticks_existed=0, healing=0, steps=1,
                        crystals=[[0, 0, 10, False]])),
    ("heal_then_select", dict(seed=0, ticks_existed=9, healing=0, steps=1,
                              crystals=[[50, 0, 0, True], [5, 0, 0, True]])),
    ("expanded_box", dict(seed=0, ticks_existed=0, healing=-1, steps=1,
                          crystals=[[40, 0, 0, True]])),
]


def c_rows():
    root = pathlib.Path(os.environ.get("TMPDIR", str(MAGMA / ".tmp")))
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=root) as tmp:
        binary = pathlib.Path(tmp) / "dragon_healer_oracle"
        subprocess.run([
            os.environ.get("CC", "cc"), "-O2", "-ffp-contract=off",
            "-I", str(BLAZE / "core"),
            str(MAGMA / "game" / "test_dragon_healer_oracle.c"),
            "-lm", "-o", str(binary),
        ], check=True)
        return subprocess.check_output([str(binary)], text=True).splitlines()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
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
        java = []
        for name, action in CASES:
            action["health"] = 100.0
            result = request(args.port, "dragon_crystal_tick_locked", action)
            for step, row in enumerate(result["rows"]):
                java.append(f"{name} {step} {row[0]} {float(row[1]):.1f} {row[2]}")
        c = c_rows()
        if java != c:
            raise SystemExit(f"FAIL java={java!r} c={c!r}")
        print(f"PASS java==c: {len(java)} healer transitions")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
