#!/usr/bin/env python3
"""Complete vanilla item stack-property census against Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    subprocess.run([
        "make", "-C", str(MAGMA), "game/test_item_registry_oracle",
    ], check=True, stdout=subprocess.DEVNULL)
    native = json.loads(subprocess.check_output([
        str(MAGMA / "game" / "test_item_registry_oracle"),
    ], text=True))
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
        java = request(args.port, "item_registry_locked")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    if len(java["items"]) != 392:
        raise AssertionError(
            f"unexpected Java registry size: {len(java['items'])}")
    mismatches = []
    for row in java["items"]:
        item = row["id"]
        actual = native["items"][item]
        expected = [
            row["limit"], int(row["subtypes"]),
            int(row["max_damage"] > 0),
            int(row.get("nbt_roundtrip") is True),
            int(row.get("nbt_split") is True),
        ]
        if actual != expected:
            mismatches.append((item, expected, actual))
        if row.get("nbt_roundtrip") is not True \
                or row.get("nbt_split") is not True:
            mismatches.append((item, "arbitrary NBT round-trip/split", row))
    if mismatches:
        raise AssertionError(f"item registry mismatches: {mismatches!r}")
    print("PASS real Java/native: 392 exact item registry property and "
          "arbitrary-NBT round-trip/split rows")


if __name__ == "__main__":
    main()
