#!/usr/bin/env python3
"""Exact real-Java/native Structure Block behavior gate."""

import argparse
import json
import pathlib
import subprocess
import sys


JAVA = pathlib.Path(__file__).resolve().parents[2] / "java"
sys.path.insert(0, str(JAVA))
from qrl_client import NetheriteEnv  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()
    env = NetheriteEnv(args.host, args.port)
    initial = env.reset({
        "seed": 708, "mode": "creative", "type": "flat",
        "structures": False,
    })
    if not initial.get("ok"):
        raise RuntimeError(initial)
    locked = env._cmd({"cmd": "server_step_lock"})
    if not locked.get("ok"):
        raise RuntimeError(locked)
    try:
        expected = env._cmd({"cmd": "structure_block_locked", "action": {}})
        actual = json.loads(subprocess.run(
            [str(args.native.resolve())], check=True,
            capture_output=True, text=True,
        ).stdout)
        if actual != expected:
            keys = sorted(
                key for key in set(actual) | set(expected)
                if actual.get(key) != expected.get(key)
            )
            raise AssertionError(
                f"Structure Block mismatch in {keys}\n"
                f"Java:  {expected}\nNative: {actual}"
            )
        sabotaged = dict(actual)
        sabotaged["integrity_states"] = list(actual["integrity_states"])
        sabotaged["integrity_states"][0] ^= 1
        if sabotaged == expected:
            raise AssertionError("Structure Block sabotage escaped")
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(unlocked)
    print("PASS real Java/native: Structure Block defaults, name sanitation, "
          "corner detection, save/load, tile payload, all-state transforms, "
          "saved living/XP/item/boat/TNT/falling-block/End-crystal/minecart "
          "entity NBT, all seven minecart subtype payloads, "
          "constructor RNG, and transforms, "
          "seeded integrity, and "
          "redstone edges plus negative control")


if __name__ == "__main__":
    main()
