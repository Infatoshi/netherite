#!/usr/bin/env python3
"""Compare real Java and native player swim/splash scalar and RNG math."""

import argparse
import os
from pathlib import Path
import subprocess


def rows(output):
    return [line for line in output.splitlines()
            if line.startswith(("A ", "B ", "C "))]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    java_dir = root / "java/Minecraft"
    env = os.environ.copy()
    env["JAVA_HOME"] = "/usr/lib/jvm/java-8-openjdk-amd64"
    java = subprocess.run(
        ["./gradlew", "-g", "run/gradle", "-q",
         "playerMovementSoundGolden"],
        cwd=java_dir, env=env, check=True, capture_output=True, text=True)
    native = subprocess.run(
        [str(args.native.resolve())], check=True,
        capture_output=True, text=True)
    expected = rows(java.stdout)
    actual = rows(native.stdout)
    if len(expected) != 3 or expected != actual:
        raise AssertionError(
            f"player movement audio mismatch\nJava:  {expected}\nnative: {actual}")
    sabotaged = list(actual)
    fields = sabotaged[1].split()
    fields[4] = f"{int(fields[4], 16) ^ 1:08x}"
    sabotaged[1] = " ".join(fields)
    if sabotaged == expected:
        raise AssertionError("post-splash RNG sabotage escaped comparator")
    print("PASS real Java/native: swim/splash volume and pitch bits, cap, "
          "67-draw splash cursor, chained next-swim pitch, and RNG-negative control")


if __name__ == "__main__":
    main()
