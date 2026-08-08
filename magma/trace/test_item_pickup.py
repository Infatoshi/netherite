#!/usr/bin/env python3
"""Exact EntityItem player-pickup comparison against Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
KINDS = (
    "empty", "partial", "partial_full", "full", "bucket",
    "damaged", "damaged_full", "subtype", "nonsubtype",
    "offhand", "selected", "shulker",
    "arbitrary_tag",
)


def native(kind):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_item_pickup_oracle"), kind,
    ], text=True)
    return json.loads(raw)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case", choices=KINDS)
    args = parser.parse_args()
    selected = (args.case,) if args.case else KINDS
    subprocess.run([
        "make", "-C", str(MAGMA), "game/test_item_pickup_oracle",
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
        for kind in selected:
            java = request(args.port, "item_pickup_locked", {"kind": kind})
            cpu = native(kind)
            if java != cpu:
                raise AssertionError(
                    f"{kind}: Java={java!r} CPU={cpu!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} exact item pickup cases")


if __name__ == "__main__":
    main()
