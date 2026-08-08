#!/usr/bin/env python3
"""Lock the final ordinary neighbor-callback trajectories."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_neighbor_tail_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_neighbor_tail_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("selected_cases"), receipt.get("pass"),
                receipt.get("fail")) != (8, 8, 0) \
            or receipt.get("state_divergences") != 0 \
            or len(receipt.get("source_summaries", [])) != 2 \
            or any(len(value) != 64 for value in receipt["source_summaries"]):
        raise RuntimeError("invalid neighbor-tail callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    expected = {
        (owner, "neighborChanged") for owner in (
            "BlockChest", "BlockCommandBlock", "BlockDispenser",
            "BlockFarmland", "BlockHopper", "BlockPistonExtension",
            "BlockPistonMoving", "BlockRailBase")
    }
    if covered != expected:
        raise RuntimeError("neighbor-tail callback family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("neighbor-tail callback census owners changed")
    matrix = (ROOT / "magma/trace/run_oracle_matrix.py").read_text()
    for name in (
            "chest", "command_block", "dispenser", "farmland", "hopper",
            "piston_extension", "piston_moving", "rail"):
        if name not in matrix:
            raise RuntimeError(f"neighbor oracle lost {name}")
    print("PASS neighbor callbacks: 8 final ordinary families exact")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL neighbor callbacks: {exc}")
        raise SystemExit(1)
