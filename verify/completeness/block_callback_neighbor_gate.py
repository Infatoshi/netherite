#!/usr/bin/env python3
"""Lock paired support/retention neighbor callback families."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_neighbor_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_neighbor_receipt" \
            or receipt.get("version") != 1 \
            or receipt.get("campaign_rows") != 99 \
            or receipt.get("vine_campaign_rows") != 8:
        raise RuntimeError("invalid neighbor callback receipt")
    rows = receipt.get("paired_families", [])
    keys = [(row[0], row[1]) for row in rows]
    if len(rows) != 17 or len(keys) != len(set(keys)):
        raise RuntimeError("neighbor callback receipt cardinality changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    for owner, callback, fixtures in rows:
        if (owner, callback) not in families:
            raise RuntimeError(f"neighbor census owner changed: {owner}")
        if len(fixtures) < 2 or min(fixtures) < 0 or max(fixtures) >= 99:
            raise RuntimeError(f"neighbor family lacks paired cases: {owner}")
    comparator = (ROOT / "magma/trace/test_plant_support_neighbor.py").read_text()
    java = (ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java").read_text()
    native = (ROOT / "magma/game/test_plant_support_neighbor_oracle.c").read_text()
    vine_comparator = (ROOT / "magma/trace/test_vine_neighbor.py").read_text()
    vine_native = (ROOT / "magma/game/test_vine_neighbor_oracle.c").read_text()
    for token in (
            "vacuous callback fixture", "before_blocks", "after_blocks",
            "EID cursor contaminated"):
        if token not in comparator:
            raise RuntimeError(f"neighbor comparator lost {token}")
    for token in (
            "portal-X notifier placement failed",
            "snow-retention fixture placement failed",
            "wall-banner notifier placement failed",
            "flower-pot notifier placement failed"):
        if token not in java:
            raise RuntimeError(f"real-Java neighbor campaign lost {token}")
    for token in (
            "fixture == 76", "fixture == 83", "fixture < 91",
            "fixture == 98"):
        if token not in native:
            raise RuntimeError(f"native neighbor campaign lost {token}")
    for token in ("range(8)", "first_diffs"):
        if token not in vine_comparator:
            raise RuntimeError(f"vine comparator lost {token}")
    for token in ("value > 7", "case 6:", "gm_runtime_set_block"):
        if token not in vine_native:
            raise RuntimeError(f"native vine campaign lost {token}")
    print("PASS neighbor callbacks: 17 paired implementation families from "
          "107 live Java/native mutation rows")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL neighbor callbacks: {exc}")
        raise SystemExit(1)
