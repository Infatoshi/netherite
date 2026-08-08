#!/usr/bin/env python3
"""Lock exact cauldron and redstone-ore physical activations."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads((HERE / "block_callback_material_use_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_material_use_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("selected_cases"), receipt.get("pass"),
                receipt.get("fail")) != (2, 2, 0) \
            or receipt.get("state_divergences") != 0 \
            or len(receipt.get("source_summaries", [])) != 2 \
            or any(len(value) != 64 for value in receipt["source_summaries"]):
        raise RuntimeError("invalid material-use callback receipt")
    covered = {(row[0], row[1]) for row in receipt.get("families", [])}
    if covered != {
            ("BlockCauldron", "onBlockActivated"),
            ("BlockRedstoneOre", "onBlockActivated")}:
        raise RuntimeError("material-use callback family set changed")
    families = {(family["owner"].rsplit(".", 1)[-1], family["callback"]) for family in census()["families"]}
    if not covered <= families:
        raise RuntimeError("material-use callback census owners changed")
    matrix = (ROOT / "magma/trace/run_oracle_matrix.py").read_text()
    runtime = (ROOT / "magma/game/runtime.c").read_text()
    for token in ("ordinary_player_use_cauldron", "ordinary_player_use_redstone_ore"):
        if token not in matrix:
            raise RuntimeError(f"material-use oracle case lost {token}")
    for token in ("id == 118", "id == 73 || id == 74"):
        if token not in runtime:
            raise RuntimeError(f"material-use native callback lost {token}")
    print("PASS material-use callbacks: exact cauldron and redstone ore activation")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL material-use callbacks: {exc}")
        raise SystemExit(1)
