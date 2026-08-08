#!/usr/bin/env python3
"""Lock exact physical activation for the principal container blocks."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_container_use_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_container_use_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("selected_cases"), receipt.get("pass"),
                receipt.get("fail")) != (10, 10, 0) \
            or receipt.get("state_divergences") != 0 \
            or len(receipt.get("source_summaries", [])) != 2 \
            or any(len(value) != 64 for value in receipt["source_summaries"]):
        raise RuntimeError("invalid container-use callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    expected = {
        (owner, "onBlockActivated") for owner in (
            "BlockAnvil", "BlockBed", "BlockBrewingStand", "BlockChest",
            "BlockDispenser", "BlockEnchantmentTable", "BlockFurnace",
            "BlockHopper", "BlockShulkerBox", "BlockWorkbench")
    }
    if covered != expected:
        raise RuntimeError("container-use callback family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("container-use callback census owners changed")
    matrix = (ROOT / "magma/trace/run_oracle_matrix.py").read_text()
    runtime = (ROOT / "magma/game/runtime.c").read_text()
    for name in (
            "anvil", "bed", "brewing_stand", "chest", "dispenser",
            "enchanting_table", "furnace", "hopper", "shulker_box",
            "workbench"):
        if f'"ordinary_player_use_{name}_seed_0"' not in matrix \
                and f'f"ordinary_player_use_{{name}}_seed_0"' not in matrix:
            raise RuntimeError(f"physical container oracle lost {name}")
    for token in ("if (id == 58)", "if (id == 117)",
                  "if (id == 145)", "runtime_is_shulker_box(id)"):
        if token not in runtime:
            raise RuntimeError(f"native container activation lost {token}")
    print("PASS container callbacks: 10 physical activation trajectories exact")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL container callbacks: {exc}")
        raise SystemExit(1)
