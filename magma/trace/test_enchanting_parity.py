#!/usr/bin/env python3
"""Compare live enchanting offers and mutation with real MC 1.11.2."""
import argparse
import json
import socket
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / ".tmp"
DRIVER = TMP / "enchanting_apply"


def request(stream, command):
    stream.write((json.dumps(command) + "\n").encode())
    reply = json.loads(stream.readline())
    if not reply.get("ok", False):
        raise RuntimeError(str(reply))
    return reply


def build_driver():
    TMP.mkdir(exist_ok=True)
    subprocess.run([
        "cc", "-O2", "-ffp-contract=off",
        str(ROOT / "blaze/cpu/enchanting_apply.c"),
        "-lm", "-o", str(DRIVER),
    ], check=True)


def native(case):
    values = [case[key] for key in (
        "item", "xp_seed", "power", "button",
        "level", "lapis", "player_seed")]
    return json.loads(subprocess.run(
        [str(DRIVER), *map(str, values)], text=True,
        capture_output=True, check=True).stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25629)
    args = parser.parse_args()
    build_driver()
    cases = [
        dict(item=276, xp_seed=0, power=15, button=2,
             level=30, lapis=12, player_seed=12345),
        dict(item=257, xp_seed=42, power=5, button=1,
             level=30, lapis=2, player_seed=-7),
        dict(item=340, xp_seed=1, power=15, button=0,
             level=30, lapis=3, player_seed=999999),
        dict(item=261, xp_seed=-1, power=15, button=2,
             level=30, lapis=3, player_seed=42),
        dict(item=346, xp_seed=12345, power=15, button=1,
             level=30, lapis=2, player_seed=123456789),
        dict(item=298, xp_seed=0x12345678, power=15, button=2,
             level=30, lapis=3, player_seed=0),
        dict(item=317, xp_seed=999999, power=0, button=0,
             level=30, lapis=1, player_seed=1),
        dict(item=276, xp_seed=42, power=15, button=2,
             level=2, lapis=2, player_seed=7),
    ]
    with socket.create_connection(("127.0.0.1", args.port), timeout=30) as sock:
        stream = sock.makefile("rwb", buffering=0)
        request(stream, {"cmd": "server_step_lock"})
        try:
            for index, case in enumerate(cases):
                java = request(stream, {
                    "cmd": "enchanting_locked", "action": case})
                c_value = native(case)
                if java != {"ok": True, **c_value}:
                    raise AssertionError(
                        "enchanting mismatch case %d\njava=%r\nnative=%r" %
                        (index, java, c_value))
        finally:
            request(stream, {"cmd": "server_step_unlock"})
    print("PASS enchanting parity: %d actual ContainerEnchantment cases" %
          len(cases))


if __name__ == "__main__":
    main()
