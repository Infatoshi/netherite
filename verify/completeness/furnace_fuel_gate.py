#!/usr/bin/env python3
"""Lock native furnace fuel and output XP to every Java item row."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "furnace_fuel_manifest.json"
CANDIDATE = HERE / "furnace_fuel_candidate.c"
sys.path.insert(0, str(HERE))
import save_fork  # noqa: E402


class FurnaceFuelError(RuntimeError):
    pass


def _capture(port: int) -> dict[str, Any]:
    result = save_fork.request(port, "furnace_registry")
    if not result.get("ok") or result.get("schema") != "qrl.furnace_registry.v2":
        raise FurnaceFuelError(f"invalid live Java response: {result}")
    rows = result.get("items")
    if not isinstance(rows, list):
        raise FurnaceFuelError("live Java response has no item rows")
    return {
        "schema": "netherite.furnace_fuel_registry",
        "version": 2,
        "source_version": "Minecraft Java 1.11.2",
        "items": rows,
    }


def _validate(manifest: dict[str, Any]) -> None:
    if set(manifest) != {"schema", "version", "source_version", "items"} \
            or manifest["schema"] != "netherite.furnace_fuel_registry" \
            or manifest["version"] != 2:
        raise FurnaceFuelError("invalid manifest header")
    rows = manifest["items"]
    if not isinstance(rows, list) or len(rows) != 392:
        raise FurnaceFuelError(f"expected 392 Java item rows, got {len(rows)}")
    ids = [row["id"] for row in rows]
    names = [row["name"] for row in rows]
    if ids != sorted(ids) or len(ids) != len(set(ids)):
        raise FurnaceFuelError("Java item ids are duplicate or unsorted")
    if len(names) != len(set(names)):
        raise FurnaceFuelError("Java item names are duplicate")
    for row in rows:
        if set(row) != {
                "id", "name", "class", "burn_time", "experience_bits"}:
            raise FurnaceFuelError(f"invalid Java item row: {row}")
        if not isinstance(row["burn_time"], int) or row["burn_time"] < 0:
            raise FurnaceFuelError(f"invalid burn time: {row}")
        bits = row["experience_bits"]
        if not isinstance(bits, list) or len(bits) != 16 \
                or not all(isinstance(value, int) for value in bits):
            raise FurnaceFuelError(f"invalid experience values: {row}")


def _native() -> dict[int, tuple[int, ...]]:
    compiler = shlex.split(os.environ.get("CC", "cc"))
    if not compiler:
        raise FurnaceFuelError("CC selects no compiler")
    with tempfile.TemporaryDirectory(prefix="netherite-fuel-") as directory:
        binary = pathlib.Path(directory) / "candidate"
        subprocess.run([
            *compiler, "-O2", "-ffp-contract=off", "-Wall", "-Wextra",
            "-I", str(ROOT / "blaze" / "core"), str(CANDIDATE),
            "-o", str(binary),
        ], cwd=ROOT, check=True)
        output = subprocess.run(
            [str(binary)], cwd=ROOT, check=True, capture_output=True,
            text=True).stdout.splitlines()
    result: dict[int, tuple[int, ...]] = {}
    for line in output:
        fields = tuple(int(value) for value in line.split())
        if len(fields) != 20 or fields[0] in result:
            raise FurnaceFuelError(f"malformed native row: {line!r}")
        result[fields[0]] = fields[1:]
    if len(result) != 2301:
        raise FurnaceFuelError(f"native emitted {len(result)} rows, expected 2301")
    return result


def _difference(manifest: dict[str, Any],
                native: dict[int, tuple[int, ...]]) -> str | None:
    for row in manifest["items"]:
        actual = native.get(row["id"])
        expected = (row["burn_time"],) * 3 \
            + tuple(row["experience_bits"])
        if actual != expected:
            return (f"{row['name']} id={row['id']}: native fuel/XP "
                    f"{actual} != Java {expected}")
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--live", action="store_true")
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--port", type=int, default=25699)
    args = parser.parse_args()
    captured = _capture(args.port) if args.live or args.update else None
    if args.update:
        _validate(captured)
        MANIFEST.write_text(json.dumps(captured, indent=2, sort_keys=True) + "\n")
    if not MANIFEST.is_file():
        raise FurnaceFuelError(f"missing {MANIFEST}; run --update --live")
    manifest = json.loads(MANIFEST.read_text())
    _validate(manifest)
    if args.live and captured != manifest:
        raise FurnaceFuelError("checked manifest differs from live Java")
    native = _native()
    difference = _difference(manifest, native)
    if difference is not None:
        raise FurnaceFuelError(difference)
    mutation = dict(native)
    first = manifest["items"][0]["id"]
    mutation[first] = (1,) + mutation[first][1:]
    if _difference(manifest, mutation) is None:
        raise FurnaceFuelError("native mutation escaped comparator")
    xp_row = next(row for row in manifest["items"]
                  if any(row["experience_bits"]))
    mutation = dict(native)
    original = mutation[xp_row["id"]]
    mutation[xp_row["id"]] = original[:3] + (0,) + original[4:]
    if _difference(manifest, mutation) is None:
        raise FurnaceFuelError("experience mutation escaped comparator")
    fuels = sum(row["burn_time"] > 0 for row in manifest["items"])
    print(
        f"PASS furnace fuels: Java/native={len(manifest['items'])} "
        f"fuel_items={fuels} fuel_metas=3 xp_metas=16 mutations=2")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError,
            subprocess.SubprocessError, save_fork.SaveForkError,
            FurnaceFuelError) as exc:
        print(f"FAIL furnace fuels: {exc}")
        raise SystemExit(1)
