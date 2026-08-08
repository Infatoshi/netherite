#!/usr/bin/env python3
"""Compare a bounded flaming-explosion transition to real 1.11.2."""

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
WORLD_SEED48 = 135120319782334
EXPLOSION_SEED48 = 0x5DEECE66D


def c_rows():
    root = pathlib.Path(os.environ.get("TMPDIR", str(MAGMA / ".tmp")))
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=root) as tmp:
        binary = pathlib.Path(tmp) / "explosion_fire_oracle"
        subprocess.run([
            os.environ.get("CC", "cc"), "-O2", "-ffp-contract=off",
            "-I", str(BLAZE / "core"),
            str(MAGMA / "game" / "test_explosion_fire_oracle.c"),
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
        for flaming in (True, False):
            row = request(args.port, "explosion_fire_locked", {
                "world_seed48": WORLD_SEED48,
                "explosion_seed48": EXPLOSION_SEED48,
                "flaming": flaming,
            })
            java.append("{} {} {} {} {}".format(
                1 if row["affected_center"] else 0,
                row["center_block"], row["fire_count"],
                row["world_seed48"], row["explosion_seed48"]))
        c = c_rows()
        if java != c:
            raise SystemExit(f"FAIL java={java!r} c={c!r}")
        print("PASS java==c: flaming positive and non-flaming control")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
