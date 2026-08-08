#!/usr/bin/env python3
"""Lock all remaining strict break callbacks to exact trajectories."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_break_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_break_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("case_count"), receipt.get("pass"),
                receipt.get("fail")) != (15, 15, 0) \
            or receipt.get("state_divergences") != 0 \
            or len(receipt.get("summary_sha256", "")) != 64:
        raise RuntimeError("invalid break callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    expected = {
        (owner, "breakBlock") for owner in (
            "BlockBrewingStand", "BlockChest", "BlockContainer",
            "BlockDispenser", "BlockFurnace", "BlockHopper",
            "BlockJukebox", "BlockPistonExtension", "BlockPistonMoving",
            "BlockRailBase", "BlockShulkerBox", "BlockSkull",
            "BlockStainedGlass", "BlockStainedGlassPane", "BlockTripWire")
    }
    if covered != expected:
        raise RuntimeError("break callback family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("break callback census owners changed")
    matrix = (ROOT / "magma/trace/run_oracle_matrix.py").read_text()
    for name in (
            "brewing_stand", "chest", "container", "dispenser", "furnace",
            "hopper", "jukebox", "piston_extension", "piston_moving", "rail",
            "shulker_box", "skull", "stained_glass",
            "stained_glass_pane", "tripwire"):
        if name not in matrix:
            raise RuntimeError(f"break oracle lost {name}")
    print("PASS break callbacks: all 15 remaining families exact")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL break callbacks: {exc}")
        raise SystemExit(1)
