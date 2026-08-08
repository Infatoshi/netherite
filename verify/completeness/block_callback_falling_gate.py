#!/usr/bin/env python3
"""Lock direct Java/native falling callback continuations for WORLD-02."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    report = census()
    census_keys = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in report["families"]
    }
    receipt = json.loads(
        (HERE / "block_callback_falling_receipt.json").read_text())
    require(receipt.get("schema") == "netherite.block_callback_falling_receipt"
            and receipt.get("version") == 1,
            "invalid falling callback receipt header")
    rows = receipt.get("cases", [])
    expected = {
        ("BlockDragonEgg", "neighborChanged", 2, 13),
        ("BlockDragonEgg", "updateTick", 2, 13),
        ("BlockFalling", "neighborChanged", 54, 614),
        ("BlockFalling", "updateTick", 54, 614),
    }
    require({tuple(row) for row in rows} == expected,
            "falling callback live receipt changed")
    for owner, callback, cases, updates in rows:
        require((owner, callback) in census_keys,
                f"falling receipt lost census owner {owner}.{callback}")
        require(cases >= 2 and updates >= cases,
                f"falling callback lacks causal controls: {owner}.{callback}")

    dragon = (ROOT / "magma/trace/test_falling_dragon_egg.py").read_text()
    falling = (ROOT / "magma/trace/test_falling_drop.py").read_text()
    java = (ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java").read_text()
    native = (ROOT / "magma/game/test_runtime.c").read_text()
    for token in (
            "on_added_scheduled", "after_support_loss_scheduled",
            "normalized_schedule"):
        require(token in dragon,
                f"dragon-egg continuation comparator lost {token}")
    for token in ("entity_drops", "source_block", "support_block",
                  "next_entity_id"):
        require(token in falling,
                f"falling continuation comparator lost {token}")
    for token in (
            "dragon egg placement schedule mismatch",
            "dragon egg duplicate schedule changed due entry",
            "BlockFalling.fallInstantly"):
        require(token in java, f"real-Java falling oracle lost {token}")
    for token in (
            "supported dragon egg drains without entity or cursors",
            "unsupported dragon egg still waits for due callback",
            "falling sand follows Java's exact nine-tick trajectory",
            "falling gravel follows Java's exact nine-tick trajectory"):
        require(token in native, f"native falling regression lost {token}")

    print("PASS block falling callbacks: 4 implementation families, "
          "56 live Java/native cases and 627 exact continuation rows")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL block falling callbacks: {exc}")
        raise SystemExit(1)
