#!/usr/bin/env python3
"""Compare ItemFood subclass effects/cooldown to the real 1.11.2 oracle."""

from __future__ import annotations

import pathlib
import sys


def java_rows(path: pathlib.Path) -> list[str]:
    rows = []
    for line in path.read_text().splitlines():
        fields = line.split()
        if fields[:1] != ["F"]:
            continue
        if len(fields) != 22:
            raise ValueError(f"malformed Java food row: {line}")
        effects = fields[8]
        if effects != "-":
            effects = ",".join(sorted(
                effects.split(","), key=lambda value: int(value.split(":")[0])))
        rows.append("E " + " ".join((
            fields[1], fields[2], fields[3], effects, *fields[9:22])))
    return rows


def native_rows(path: pathlib.Path) -> list[str]:
    return [line for line in path.read_text().splitlines()
            if line.startswith("E ")]


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: food_registry_gate.py JAVA NATIVE")
    java = java_rows(pathlib.Path(sys.argv[1]))
    native = native_rows(pathlib.Path(sys.argv[2]))
    if len(java) != 38 or len(native) != 38:
        raise SystemExit(
            f"FAIL food row count: java={len(java)} native={len(native)}")
    if java != native:
        for index, (expected, actual) in enumerate(zip(java, native)):
            if expected != actual:
                raise SystemExit(
                    f"FAIL food row {index}:\nJ {expected}\nC {actual}")
        raise SystemExit("FAIL food rows differ in length")
    print("food registry: PASS (35 variants, 38 effects/RNG/cooldown rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
