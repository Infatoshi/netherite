#!/usr/bin/env python3
"""Exact two-EntityItem merge comparison against Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
KINDS = (
    "equal", "reverse_equal", "larger_first", "bucket_limit", "overflow",
    "x_inside", "x_edge", "y_inside", "y_edge",
    "different_item", "subtype_meta", "infinite_delay",
    "tag_equal", "tag_different", "repair_different", "enchant_equal",
    "arbitrary_tag_equal", "arbitrary_tag_different",
)
ITEM_KEYS = (
    "eid", "item", "count", "meta", "age", "pickup_delay",
    "ticks_existed", "health", "lifespan", "on_ground",
    "position_bits", "motion_bits",
)


def normalized(value):
    return {
        "ok": value["ok"],
        "kind": value["kind"],
        "tag_preserved": value["tag_preserved"],
        "items": [
            {key: item[key] for key in ITEM_KEYS}
            for item in value["items"]
        ],
    }


def native(kind):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_item_merge_oracle"), kind,
    ], text=True)
    return json.loads(raw)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case", choices=KINDS)
    args = parser.parse_args()
    selected = (args.case,) if args.case else KINDS
    subprocess.run([
        "make", "-C", str(MAGMA), "game/test_item_merge_oracle",
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
            java = normalized(request(
                args.port, "item_merge_locked", {"kind": kind}))
            cpu = native(kind)
            if java != cpu:
                raise AssertionError(
                    f"{kind}: Java={java!r} CPU={cpu!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} exact item merge cases")


if __name__ == "__main__":
    main()
