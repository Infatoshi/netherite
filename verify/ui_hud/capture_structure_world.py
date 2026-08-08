#!/usr/bin/env python3
"""Capture exact Structure Block TESR frames from real Minecraft 1.11.2."""

from __future__ import print_function

import argparse
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "java"))
from qrl_client import NetheriteEnv  # noqa: E402


STATES = (
    ("save_air", {
        "mode": "SAVE", "mirror": "NONE", "rotation": "NONE",
        "show_air": True, "show_bounding_box": True,
    }),
    ("load_transform", {
        "mode": "LOAD", "mirror": "LEFT_RIGHT",
        "rotation": "CLOCKWISE_90", "show_air": False,
        "show_bounding_box": True,
    }),
    ("load_hidden", {
        "mode": "LOAD", "mirror": "LEFT_RIGHT",
        "rotation": "CLOCKWISE_90", "show_air": False,
        "show_bounding_box": False,
    }),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25699)
    parser.add_argument(
        "--out", default=os.path.join(ROOT, "verify", "ui_hud", "goldens"))
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    env = NetheriteEnv(port=args.port)
    try:
        reset = env.reset({
            "seed": 0, "mode": "creative", "type": "flat",
            "structures": False,
        })
        if not reset.get("ok"):
            raise RuntimeError("reset failed: %r" % reset)
        records = []
        for name, state in STATES:
            fixture = {
                # Keep the controller underground while its legal +32 Y
                # offset puts the entire measured volume in empty sky. This
                # removes ordinary block geometry from the TESR pixel mask.
                "x": 0, "y": 32, "z": 0,
                "pos_x": -2, "pos_y": 32, "pos_z": 1,
                "size_x": 5, "size_y": 3, "size_z": 4,
            }
            fixture.update(state)
            pin = env._cmd({
                "cmd": "hud_pin",
                "action": {
                    "x": 0.5, "y": 65.0, "z": -6.5,
                    "yaw": 0.0, "pitch": 20.0,
                    "close_screen": True,
                    "structure_world": fixture,
                },
            })
            if not pin.get("ok"):
                raise RuntimeError("hud_pin %s failed: %r" % (name, pin))
            prefix = "structure_world_" + name
            # Two active renders on one client-thread turn are the stable
            # oracle. The fixture projects entirely into empty sky; the gate
            # extracts TileEntityStructureRenderer's exact untextured RGB
            # palette inside that fixed ROI and requires both masks/colors to
            # agree. Toggling the tile hidden between full renderWorld calls
            # is intentionally avoided: Java mutates unrelated renderer state
            # between those calls and makes background subtraction noisy.
            path_a = os.path.abspath(os.path.join(
                args.out, prefix + "_on.png"))
            path_b = os.path.abspath(os.path.join(
                args.out, prefix + "_on_2.png"))
            pair = env._cmd({
                "cmd": "frame_pair",
                "action": {
                    "file_a": path_a, "file_b": path_b,
                    "rerender": True, "hide_gui": True,
                    "gmx": 0, "gmy": 0,
                },
            })
            if not pair.get("ok"):
                raise RuntimeError("frame_pair %s failed: %r" % (name, pair))
            records.append({
                "id": prefix, "fixture": fixture,
                "file_on": path_a, "file_on_2": path_b,
                "pin": pin, "active_frame_pair": pair,
            })
        meta = os.path.join(args.out, "meta", "structure_world.json")
        os.makedirs(os.path.dirname(meta), exist_ok=True)
        with open(meta, "w") as stream:
            json.dump({
                "source": "real Minecraft Java 1.11.2",
                "profile": "client-world TESR, atomic active frame_pair",
                "states": records,
            }, stream, indent=2, sort_keys=True)
            stream.write("\n")
        print("structure world oracle: captured %d states" % len(records))
    finally:
        env.close()


if __name__ == "__main__":
    main()
