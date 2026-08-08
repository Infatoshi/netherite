#!/usr/bin/env python3
"""Lock direct note-block and liquid callback campaigns."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_note_liquid_receipt.json").read_text())
    if receipt.get("schema") != \
            "netherite.block_callback_note_liquid_receipt" \
            or receipt.get("version") != 1:
        raise RuntimeError("invalid note/liquid callback receipt")
    campaigns = receipt.get("campaigns", [])
    if [(row.get("name"), row.get("rows")) for row in campaigns] != [
            ("note_block", 12), ("static_lava_neighbor", 58)]:
        raise RuntimeError("note/liquid campaign cardinality changed")
    expected = {
        ("BlockNote", "neighborChanged"),
        ("BlockNote", "onBlockActivated"),
        ("BlockStaticLiquid", "neighborChanged"),
        ("BlockDynamicLiquid", "updateTick"),
    }
    covered = {
        tuple(family)
        for campaign in campaigns
        for family in campaign.get("families", [])
    }
    if covered != expected:
        raise RuntimeError("note/liquid family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("note/liquid census owners changed")
    java = (ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java").read_text()
    note = (ROOT / "magma/trace/test_note_block.py").read_text()
    lava = (ROOT / "magma/trace/test_static_lava_neighbor.py").read_text()
    native = (ROOT / "magma/game/runtime.c").read_text()
    for token in (
            "Blocks.NOTEBLOCK.onBlockActivated",
            "Blocks.NOTEBLOCK.neighborChanged",
            "static_lava_neighbor_locked"):
        if token not in java:
            raise RuntimeError(f"real-Java callback campaign lost {token}")
    for token in ("CASES = 12", "wrong effect cardinality", "RNG advanced"):
        if token not in note:
            raise RuntimeError(f"note comparator lost {token}")
    for token in ("range(58)", "expected_scheduled", "wrong block outcome"):
        if token not in lava:
            raise RuntimeError(f"lava comparator lost {token}")
    for token in (
            "runtime_tick_lava_flat", "runtime_lava_neighbor_changed",
            "gm_fluid_forget_near"):
        if token not in native:
            raise RuntimeError(f"native callback implementation lost {token}")
    print("PASS note/liquid callbacks: 4 direct families from 70 live "
          "Java/native rows")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL note/liquid callbacks: {exc}")
        raise SystemExit(1)
