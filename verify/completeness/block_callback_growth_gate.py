#!/usr/bin/env python3
"""Lock the direct plant, terrain, snow, ice, and dispenser callbacks."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_growth_receipt.json").read_text())
    rows = receipt.get("families", [])
    if receipt.get("schema") != "netherite.block_callback_growth_receipt" \
            or receipt.get("version") != 1 \
            or receipt.get("rows") != 388 \
            or len(rows) != 21:
        raise RuntimeError("invalid growth callback receipt")
    covered = {tuple(row) for row in rows}
    if len(covered) != 21 or any(callback != "updateTick"
                                 for _, callback in covered):
        raise RuntimeError("growth family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("growth census owners changed")
    comparator = (
        ROOT / "magma/trace/test_dispenser_bonemeal.py").read_text()
    java = (ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java").read_text()
    native = (
        ROOT / "magma/game/test_dispenser_bonemeal_oracle.c").read_text()
    runtime = (ROOT / "magma/game/runtime.c").read_text()
    for token in (
            '"crop": 212', '"crop": 106', '"crop": 11',
            '"crop": 60', '"crop": 200', "first_block_diffs"):
        if token not in comparator:
            raise RuntimeError(f"growth comparator lost {token}")
    for token in (
            "tickState.getBlock().updateTick", "Blocks.DISPENSER.updateTick",
            "snow/ice block-light fixture mismatch"):
        if token not in java:
            raise RuntimeError(f"real-Java growth campaign lost {token}")
    for token in (
            "gm_runtime_random_tick_block", "gm_world_debug_set_block_light",
            "tick_block_light"):
        if token not in native:
            raise RuntimeError(f"native growth campaign lost {token}")
    for token in (
            "runtime_random_tick_crop", "runtime_random_tick_vine",
            "runtime_random_tick_static_lava", "runtime_tick_dispenser"):
        if token not in runtime:
            raise RuntimeError(f"native growth callback lost {token}")
    print("PASS growth callbacks: 21 direct implementation families from "
          "388 live Java/native rows")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL growth callbacks: {exc}")
        raise SystemExit(1)
