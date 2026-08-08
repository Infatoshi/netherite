#!/usr/bin/env python3
"""Fail-closed progress counter for WORLD-02 strict callback cases."""

from __future__ import annotations

import collections
import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
STRICT_CALLBACKS = {
    "breakBlock", "dropBlockAsItemWithChance", "neighborChanged",
    "onBlockActivated", "onBlockPlacedBy", "randomTick", "updateTick",
}


def main() -> int:
    report = census()
    expected = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in report["families"]
        if family["callback"] in STRICT_CALLBACKS
    }
    manifest = json.loads(
        (HERE / "block_callback_strict_coverage.json").read_text())
    if manifest.get("schema") != "netherite.block_callback_strict_coverage" \
            or manifest.get("version") != 1:
        raise RuntimeError("invalid strict callback coverage header")
    rows = manifest.get("families", [])
    keys = [(row[0], row[1]) for row in rows]
    if len(keys) != len(set(keys)):
        raise RuntimeError("duplicate strict callback coverage family")
    covered = set(keys)
    extra = sorted(covered - expected)
    if extra:
        raise RuntimeError(f"coverage references unknown families: {extra}")
    for evidence in manifest.get("evidence", []):
        path = ROOT / evidence
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"strict callback evidence missing: {evidence}")
    remaining = expected - covered
    by_callback = collections.Counter(callback for _, callback in remaining)
    print(
        f"PASS strict callback progress: {len(covered)}/{len(expected)} "
        f"families proven; {len(remaining)} remain; "
        f"remaining_by_callback={dict(sorted(by_callback.items()))}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (IndexError, KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL strict callback progress: {exc}")
        raise SystemExit(1)
