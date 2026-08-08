#!/usr/bin/env python3
"""Compare the native furnace registry and hot lookup with the live-Java census."""

from __future__ import annotations

import json
import os
import pathlib
import shlex
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "verify" / "completeness" / "surface_registry_manifest.json"
CANDIDATE = ROOT / "verify" / "completeness" / "smelting_registry_candidate.c"
SENTINEL = 0xFFFFFFFF


class SmeltingRegistryError(RuntimeError):
    pass


def _expected() -> list[tuple[int, ...]]:
    manifest = json.loads(MANIFEST.read_text())
    return [(
        row["input_item"], 1, row["input_meta"], row["output_item"],
        row["output_count"], row["output_meta"],
    ) for row in manifest["smelting_recipes"]]


def _candidate() -> tuple[list[tuple[int, ...]], list[tuple[int, ...]]]:
    compiler = shlex.split(os.environ.get("CC", "cc"))
    if not compiler:
        raise SmeltingRegistryError("CC selects no compiler")
    with tempfile.TemporaryDirectory(prefix="netherite-smelting-") as directory:
        binary = pathlib.Path(directory) / "candidate"
        subprocess.run([
            *compiler, "-O2", "-ffp-contract=off", "-Wall", "-Wextra",
            "-I", str(ROOT / "blaze" / "core"), str(CANDIDATE),
            "-o", str(binary),
        ], cwd=ROOT, check=True)
        output = subprocess.run(
            [str(binary)], cwd=ROOT, check=True, capture_output=True,
            text=True).stdout.splitlines()
    if not output or not output[0].startswith("COUNT "):
        raise SmeltingRegistryError("candidate emitted no count")
    declared = int(output[0].split()[1])
    rows: list[tuple[int, ...]] = []
    negatives: list[tuple[int, ...]] = []
    for line in output[1:]:
        fields = line.split()
        if fields[0] == "R" and len(fields) == 10:
            values = tuple(int(value) for value in fields[1:])
            rows.append(values)
        elif fields[0] == "N" and len(fields) == 6:
            negatives.append(tuple(int(value) for value in fields[1:]))
        else:
            raise SmeltingRegistryError(f"malformed candidate row: {line!r}")
    if declared != len(rows):
        raise SmeltingRegistryError(
            f"candidate count mismatch: declared {declared}, emitted {len(rows)}")
    return rows, negatives


def _difference(expected: list[tuple[int, ...]],
                actual: list[tuple[int, ...]]) -> str | None:
    if len(expected) != len(actual):
        return f"row count {len(actual)} != {len(expected)}"
    for index, (oracle, native) in enumerate(zip(expected, actual)):
        if native[:6] != oracle:
            return f"row {index}: native table {native[:6]} != Java {oracle}"
        if native[6:] != oracle[3:]:
            return f"row {index}: hot lookup {native[6:]} != Java {oracle[3:]}"
    return None


def main() -> int:
    expected = _expected()
    rows, negatives = _candidate()
    difference = _difference(expected, rows)
    if difference is not None:
        raise SmeltingRegistryError(difference)
    if not negatives:
        raise SmeltingRegistryError("negative-control corpus is empty")
    for item, meta, out_item, out_count, out_meta in negatives:
        if (out_item & 0xFFFFFFFF, out_count, out_meta) != (SENTINEL, 0, 0):
            raise SmeltingRegistryError(
                f"negative {item}:{meta} unexpectedly maps to "
                f"{out_item}:{out_meta}x{out_count}")

    mutated = list(rows)
    row = list(mutated[0])
    row[3] += 1
    mutated[0] = tuple(row)
    if _difference(expected, mutated) is None:
        raise SmeltingRegistryError("table mutation escaped the comparator")
    row = list(rows[0])
    row[6] += 1
    mutated = list(rows)
    mutated[0] = tuple(row)
    if _difference(expected, mutated) is None:
        raise SmeltingRegistryError("hot-lookup mutation escaped the comparator")

    print(
        f"PASS smelting registry: Java={len(expected)} native={len(rows)} "
        f"hot={len(rows)} negatives={len(negatives)} mutations=2")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, subprocess.SubprocessError,
            json.JSONDecodeError, SmeltingRegistryError) as exc:
        print(f"FAIL smelting registry: {exc}")
        raise SystemExit(1)
