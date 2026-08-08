#!/usr/bin/env python3
"""Capture zero-tick A/B GuiEditStructure frames from the real 1.11.2 client."""

from __future__ import print_function

import argparse
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "java"))
from qrl_client import NetheriteEnv  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25699)
    parser.add_argument(
        "--out",
        default=os.path.join(ROOT, "verify", "ui_hud", "goldens"),
    )
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    env = NetheriteEnv(port=args.port)
    try:
        reset = env.reset({
            "seed": 0,
            "mode": "creative",
            "type": "flat",
            "structures": False,
        })
        if not reset.get("ok"):
            raise RuntimeError("reset failed: %r" % reset)
        records = []
        for mode in ("SAVE", "LOAD", "CORNER", "DATA"):
            state_id = "gui_structure_%s" % mode.lower()
            pin = env._cmd({
                "cmd": "hud_pin",
                "action": {
                    "structure_gui": {
                        "mode": mode,
                        "name": "screen_fixture",
                        "pos_x": 1,
                        "pos_y": 2,
                        "pos_z": 3,
                        "size_x": 4,
                        "size_y": 5,
                        "size_z": 6,
                        "mirror": "LEFT_RIGHT",
                        "rotation": "CLOCKWISE_90",
                        "metadata": "custom_data",
                        "ignore_entities": True,
                        "show_air": True,
                        "show_bounding_box": True,
                        "integrity": 0.75,
                        "seed": 12345,
                    }
                },
            })
            if not pin.get("ok"):
                raise RuntimeError("hud_pin %s failed: %r" % (mode, pin))
            diag = env._cmd({"cmd": "focusdiag", "action": {}})
            if diag.get("screen") != "GuiEditStructure":
                raise RuntimeError("wrong Java screen for %s: %r" % (mode, diag))
            path_a = os.path.abspath(os.path.join(args.out, state_id + "_a.png"))
            path_b = os.path.abspath(os.path.join(args.out, state_id + "_b.png"))
            pair = env._cmd({
                "cmd": "frame_pair",
                "action": {
                    "file_a": path_a,
                    "file_b": path_b,
                    "rerender": True,
                    "gmx": 0,
                    "gmy": 0,
                },
            })
            if not pair.get("ok"):
                raise RuntimeError("frame_pair %s failed: %r" % (mode, pair))
            for path in (path_a, path_b):
                if not os.path.isfile(path) or os.path.getsize(path) < 100:
                    raise RuntimeError("missing Java frame: " + path)
            records.append({
                "id": state_id,
                "mode": mode,
                "screen": diag.get("screen"),
                "file_a": path_a,
                "file_b": path_b,
                "pin": pin,
                "frame_pair": pair,
            })
        meta = os.path.join(args.out, "meta", "gui_structure.json")
        os.makedirs(os.path.dirname(meta), exist_ok=True)
        with open(meta, "w") as stream:
            json.dump({
                "source": "real Minecraft Java 1.11.2",
                "profile": "oracle-pool menus enabled, atomic frame_pair",
                "states": records,
            }, stream, indent=2, sort_keys=True)
            stream.write("\n")
        print("structure GUI oracle: captured %d stable modes" % len(records))
    finally:
        env.close()


if __name__ == "__main__":
    main()
