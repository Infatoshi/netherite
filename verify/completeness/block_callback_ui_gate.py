#!/usr/bin/env python3
"""Lock direct UI/container and redstone-trigger callback campaigns."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_ui_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_ui_receipt" \
            or receipt.get("version") != 1:
        raise RuntimeError("invalid UI callback receipt")
    campaigns = receipt.get("campaigns", [])
    if [(row.get("name"), row.get("cases")) for row in campaigns] != [
            ("ender_chest", 3), ("structure_block", 1)]:
        raise RuntimeError("UI campaign cardinality changed")
    covered = {
        tuple(family)
        for campaign in campaigns
        for family in campaign.get("families", [])
    }
    expected = {
        ("BlockEnderChest", "onBlockActivated"),
        ("BlockStructure", "neighborChanged"),
    }
    if covered != expected:
        raise RuntimeError("UI family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("UI census owners changed")
    java = (ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java").read_text()
    ender = (ROOT / "magma/trace/test_ender_chest.py").read_text()
    structure = (ROOT / "magma/trace/test_structure_block.py").read_text()
    for token in (
            "Blocks.ENDER_CHEST", ".onBlockActivated(world, chestPos",
            'out.addProperty("rising_powered"',
            'out.addProperty("falling_powered"'):
        if token not in java:
            raise RuntimeError(f"real-Java UI callback lost {token}")
    for token in ("CASES = ((1, 1), (10, 6), (17, 17))",
                  "Ender Chest sabotage escaped"):
        if token not in ender:
            raise RuntimeError(f"Ender Chest comparator lost {token}")
    for token in ("integrity_states", "Structure Block sabotage escaped"):
        if token not in structure:
            raise RuntimeError(f"Structure comparator lost {token}")
    print("PASS UI callbacks: Ender Chest activation and Structure Block "
          "redstone edges match live Java")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL UI callbacks: {exc}")
        raise SystemExit(1)
