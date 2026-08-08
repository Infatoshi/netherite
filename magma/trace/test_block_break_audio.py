#!/usr/bin/env python3
"""Compare complete real-Java and native block sound maps."""

import argparse
import os
from pathlib import Path
import subprocess


def rows(output, prefix):
    return [line for line in output.splitlines()
            if line.startswith(prefix + " ")]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    java_dir = root / "java/Minecraft"
    env = os.environ.copy()
    env["JAVA_HOME"] = "/usr/lib/jvm/java-8-openjdk-amd64"
    java = subprocess.run(
        ["./gradlew", "-g", "run/gradle", "-q", "blockBreakSoundGolden"],
        cwd=java_dir, env=env, check=True, capture_output=True, text=True)
    native = subprocess.run(
        [str(args.native.resolve())], check=True,
        capture_output=True, text=True)
    for prefix, action in (
            ("B", "break"), ("P", "place"), ("H", "hit"), ("F", "fall"),
            ("S", "step")):
        expected = rows(java.stdout, prefix)
        actual = rows(native.stdout, prefix)
        if len(expected) != 235 or len(actual) != 235:
            raise AssertionError(
                f"expected 235 registered non-air {action} ids, got "
                f"Java={len(expected)} native={len(actual)}")
        if expected != actual:
            for left, right in zip(expected, actual):
                if left != right:
                    raise AssertionError(
                        f"first {action} mismatch:\nJava   {left}\nnative {right}")
            raise AssertionError(f"block-{action} maps differ in length")
        families = {line.split()[2] for line in expected}
        if len(families) != 12:
            raise AssertionError(
                f"expected 12 {action} sound families, got {families}")
        sabotaged = list(actual)
        index = next(i for i, line in enumerate(sabotaged)
                     if line.startswith(f"{prefix} 41 "))
        sabotaged[index] = sabotaged[index].replace(
            f"minecraft:block.metal.{action}",
            f"minecraft:block.stone.{action}")
        if expected == sabotaged:
            raise AssertionError(
                f"{action} material-family sabotage escaped the comparator")
    print("PASS real Java/native: all 235 registered non-air block ids, "
          "12 break/place/hit/fall/step families, raw volume/pitch bits, metadata "
          "invariance, and per-action material-negative controls")


if __name__ == "__main__":
    main()
