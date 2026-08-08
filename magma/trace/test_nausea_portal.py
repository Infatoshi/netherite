#!/usr/bin/env python3
"""Exact EntityPlayerSP portal/Nausea ramp comparison against MC 1.11.2."""

import argparse
import json
import pathlib
import struct
import subprocess

from test_dragon_crystal_notification import request


def float_bits(value):
    return f"{struct.unpack('>I', struct.pack('>f', value))[0]:08x}"


CASES = (
    ("physical_first", 0.0, 0.0, True, 0, "step"),
    ("physical_cap", 0.999, 0.8, True, 0, "step"),
    ("nausea_first", 0.0, 0.0, False, 61, "step"),
    ("nausea_mid", 0.5, 0.4, False, 61, "step"),
    ("nausea_cap", 1.0, 0.99, False, 600, "step"),
    ("threshold_decay", 0.2, 0.25, False, 60, "step"),
    ("decay_clamp", 0.02, 0.07, False, 0, "step"),
    ("portal_precedes_nausea", 0.3, 0.2, True, 100, "step"),
    ("explicit_remove", 0.7, 0.6, False, 100, "remove"),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25699)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()
    request(args.port, "server_step_lock")
    try:
        for name, current, previous, in_portal, duration, mode in CASES:
            current_hex = float_bits(current)
            previous_hex = float_bits(previous)
            action = {
                "current_bits": current_hex,
                "prev_bits": previous_hex,
                "in_portal": in_portal,
                "nausea_duration": duration,
                "mode": mode,
            }
            java = request(args.port, "nausea_portal_locked", action)
            raw = subprocess.check_output([
                str(args.native.resolve()), current_hex, previous_hex,
                str(int(in_portal)), str(duration), mode,
            ], text=True)
            native = json.loads(raw)
            if java != native:
                mismatch = {
                    key: [java.get(key), native.get(key)]
                    for key in sorted(set(java) | set(native))
                    if java.get(key) != native.get(key)
                }
                raise AssertionError(f"{name}: {mismatch}")
    finally:
        request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(CASES)} exact portal/Nausea cases")


if __name__ == "__main__":
    main()
