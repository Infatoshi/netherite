#!/usr/bin/env python3
"""Real-Java/native oracle for returning potion player collision."""

import argparse
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
    locked = env._cmd({"cmd": "server_step_lock"})
    if not locked.get("ok"):
        raise RuntimeError(f"server lock failed: {locked}")
    try:
        result = env._cmd({"cmd": "potion_player_return_locked"})
        if not result.get("ok"):
            raise AssertionError(result)
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")
    expected = [
        f"R {age} {int(dead)} {int(ignored)} {ignore_time} {health}"
        for age, dead, ignored, ignore_time, health in zip(
            result["ages"], result["dead"], result["ignored"],
            result["ignore_times"], result["health_bits"])
    ]
    expected.append(
        f"H {result['hurt_time']} {result['hurt_resistant_time']}")
    expected.extend(
        f"P {effect['id']} {effect['amp']} {effect['dur']} "
        f"{int(bool(effect['flags'] & 1))} "
        f"{int(bool(effect['flags'] & 2))}"
        for effect in result["effects"])
    expected.append(
        f"D {result['drink_item']} {result['drink_count']} "
        f"{result['drink_meta']}")
    expected.extend(
        f"Q {effect['id']} {effect['amp']} {effect['dur']} "
        f"{int(bool(effect['flags'] & 1))} "
        f"{int(bool(effect['flags'] & 2))}"
        for effect in sorted(
            result["drink_effects"], key=lambda effect: effect["id"]))
    expected.append(
        f"S {result['effect_food']} {result['effect_saturation_bits']} "
        f"{result['effect_luck_bits']} "
        f"{int(result['saturation_alive'])}")
    native = subprocess.run(
        [str(args.native.resolve())], check=True,
        capture_output=True, text=True)
    actual = [line for line in native.stdout.splitlines()
              if line.startswith(("R ", "H ", "P ", "D ", "Q ", "S "))]
    if actual != expected:
        raise AssertionError(
            "potion player-return mismatch\n"
            f"Java:  {expected}\nNative: {actual}")
    print("potion player-return oracle: PASS "
          "(custom splash/drink plus Saturation and Luck/Unluck effects)")


if __name__ == "__main__":
    main()
