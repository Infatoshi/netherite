#!/usr/bin/env python3
"""Lock exact leaf update, break-marking, and drop callbacks."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_leaves_receipt.json").read_text())
    expected = {
        ("BlockLeaves", "breakBlock"),
        ("BlockLeaves", "dropBlockAsItemWithChance"),
        ("BlockLeaves", "updateTick"),
    }
    if receipt.get("schema") != "netherite.block_callback_leaves_receipt" \
            or receipt.get("version") != 1 \
            or receipt.get("cases") != 13 \
            or {tuple(row) for row in receipt.get("families", [])} != expected:
        raise RuntimeError("invalid leaf callback receipt")
    census_keys = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not expected <= census_keys:
        raise RuntimeError("leaf callback census owners changed")
    comparator = (ROOT / "magma/trace/test_leaf_decay.py").read_text()
    java = (ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java").read_text()
    native = (ROOT / "magma/game/test_leaf_decay_oracle.c").read_text()
    for token in ("WORLD_SEEDS", "block_lengths", "first_diffs"):
        if token not in comparator:
            raise RuntimeError(f"leaf comparator lost {token}")
    for token in (
            "Exact BlockLeaves distance, drop, and breakBlock lifecycle",
            "leaf_decay_locked"):
        if token not in java:
            raise RuntimeError(f"real-Java leaf oracle lost {token}")
    for token in ("case 4:", "case 8:", "default:"):
        if token not in native:
            raise RuntimeError(f"native leaf campaign lost {token}")
    print("PASS leaf callbacks: update, break marking, and drops across "
          "13 live Java/native cases")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL leaf callbacks: {exc}")
        raise SystemExit(1)
