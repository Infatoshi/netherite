#!/usr/bin/env python3
"""Compare complete real-Java and native block break/place/hit sound maps.

Exit codes follow the repo's tri-state gate convention:
  0  PASS  - every registered non-air block id matches Java exactly
  1  FAIL  - a real divergence, or a malformed/short table on either side
  3  BLOCKED - the Java oracle could not be reached (no JDK 8, no gradle,
     no deobfuscated classpath). A blocked gate is never a pass.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys

JAVA_HOME = "/usr/lib/jvm/java-8-openjdk-amd64"
# Block.getBlockById returns a non-air block for exactly this many legacy ids
# in 1.11.2 (1..234 plus 255/structure_block); both sides must agree on it or
# the comparison is measuring the wrong thing.
EXPECTED_IDS = 235
EXPECTED_FAMILIES = 12
ACTIONS = (("B", "break"), ("P", "place"), ("H", "hit"))


def blocked(message):
    print(f"BLOCKED: {message}")
    sys.exit(3)


def fail(message):
    print(f"FAIL: {message}")
    sys.exit(1)


def rows(output, prefix):
    return [line for line in output.splitlines()
            if line.startswith(prefix + " ")]


def run_java(java_dir):
    if not (java_dir / "gradlew").is_file():
        blocked(f"no gradle wrapper at {java_dir}/gradlew")
    if not Path(JAVA_HOME).is_dir():
        blocked(f"JDK 8 not found at {JAVA_HOME}")
    env = os.environ.copy()
    env["JAVA_HOME"] = JAVA_HOME
    try:
        result = subprocess.run(
            ["./gradlew", "-g", "run/gradle", "-q", "blockBreakSoundGolden"],
            cwd=java_dir, env=env, capture_output=True, text=True)
    except OSError as exc:
        blocked(f"cannot execute gradle: {exc}")
    if result.returncode != 0:
        tail = (result.stderr or result.stdout).strip().splitlines()[-6:]
        blocked("gradle blockBreakSoundGolden failed (oracle not built?): "
                + " | ".join(tail))
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]

    native_bin = args.native.resolve()
    if not native_bin.is_file():
        fail(f"native binary not built: {native_bin}")
    native = subprocess.run(
        [str(native_bin)], check=True, capture_output=True, text=True)
    java = run_java(root / "java/Minecraft")

    for prefix, action in ACTIONS:
        expected = rows(java, prefix)
        actual = rows(native.stdout, prefix)
        if len(expected) != EXPECTED_IDS or len(actual) != EXPECTED_IDS:
            fail(f"expected {EXPECTED_IDS} registered non-air {action} ids, "
                 f"got Java={len(expected)} native={len(actual)}")
        if expected != actual:
            for left, right in zip(expected, actual):
                if left != right:
                    fail(f"first {action} mismatch:\n"
                         f"Java   {left}\nnative {right}")
            fail(f"block-{action} maps differ in length")
        families = {line.split()[2] for line in expected}
        if len(families) != EXPECTED_FAMILIES:
            fail(f"expected {EXPECTED_FAMILIES} {action} sound families, "
                 f"got {sorted(families)}")
        # Negative control: a comparator that cannot see a swapped material
        # family would pass this file forever. Block 41 is gold (METAL).
        sabotaged = list(actual)
        index = next(i for i, line in enumerate(sabotaged)
                     if line.startswith(f"{prefix} 41 "))
        sabotaged[index] = sabotaged[index].replace(
            f"minecraft:block.metal.{action}",
            f"minecraft:block.stone.{action}")
        if expected == sabotaged:
            fail(f"{action} material-family sabotage escaped the comparator")

    print(f"PASS real Java/native: all {EXPECTED_IDS} registered non-air block "
          f"ids, {EXPECTED_FAMILIES} break/place/hit families, raw volume/pitch "
          "bits, metadata invariance, and per-action material-negative controls")


if __name__ == "__main__":
    main()
