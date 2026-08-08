#!/usr/bin/env python3
"""Compare Teleporter.makePortal placement and RNG with real 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import sys


JAVA = pathlib.Path(__file__).resolve().parents[2] / "java"
sys.path.insert(0, str(JAVA))
from qrl_client import NetheriteEnv  # noqa: E402


CASES = (
    (0.25, 5.0, 0.75, 0x5DEECE66D),
    (0.75, 5.0, 0.25, 0x123456789ABC),
    (0.125, 5.0, 0.875, 0x23456789ABCD),
    (0.875, 5.0, 0.125, 0x0FEDCBA98765),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()
    env = NetheriteEnv(args.host, args.port)
    initial = env.reset({
        "seed": 708,
        "mode": "survival",
        "type": "flat",
        "structures": False,
    })
    if not initial.get("ok"):
        raise RuntimeError(f"oracle reset failed: {initial}")
    locked = env._cmd({"cmd": "server_step_lock"})
    if not locked.get("ok"):
        raise RuntimeError(f"server lock failed: {locked}")
    negative_checked = False
    try:
        if int(locked["authoritative"]["dim"]) != 0:
            moved = env._cmd({
                "cmd": "transfer_dimension_locked", "action": {"id": 0},
            })
            if not moved.get("ok"):
                raise AssertionError(moved)
        for index, (frac_x, y, frac_z, seed48) in enumerate(CASES):
            expected = env._cmd({
                "cmd": "teleporter_make_locked",
                "action": {
                    "case": index,
                    "frac_x": frac_x,
                    "y": y,
                    "frac_z": frac_z,
                    "random_seed48": seed48,
                },
            })
            if not expected.get("ok"):
                raise AssertionError(expected)
            actual = json.loads(subprocess.run([
                str(args.native.resolve()), "708",
                repr(expected["entity_x"]), repr(expected["entity_y"]),
                repr(expected["entity_z"]), str(seed48),
            ], check=True, capture_output=True, text=True).stdout)
            comparable = {
                "ok": True,
                "random_seed48": expected["random_seed48"],
                "blocks": expected["blocks"],
            }
            if actual != comparable:
                raise AssertionError(
                    f"case {index} mismatch\nJava:  {comparable}\n"
                    f"Native: {actual}")
            if not negative_checked:
                sabotaged = json.loads(json.dumps(actual))
                sabotaged["blocks"][0][3] ^= 1
                if sabotaged == comparable:
                    raise AssertionError("portal-block sabotage escaped")
                negative_checked = True
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")
    print("PASS real Java/native: 4 Teleporter construction/RNG cases plus "
          "block-negative control")


if __name__ == "__main__":
    main()
