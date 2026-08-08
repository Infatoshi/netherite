#!/usr/bin/env python3
"""Lock exact support-removal callback trajectories."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_support_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_support_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("selected_cases"), receipt.get("pass"),
                receipt.get("fail"), receipt.get("retries")) \
            != (9, 9, 0, 0) \
            or receipt.get("state_divergences") != 0 \
            or len(receipt.get("campaign_summary_sha256", "")) != 64:
        raise RuntimeError("invalid support callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    if len(covered) != 12:
        raise RuntimeError("support callback family set changed")
    if set(receipt.get("excluded", [])) != {
            "falling_sand_farmland_dry_drop_seed_0",
            "redstone_piston_east_stone_settlement_invalidates_cactus_seed_0",
            "redstone_tnt_direct_add_powered_seed_0"}:
        raise RuntimeError("support exclusion set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("support callback census owners changed")
    matrix = (ROOT / "magma/trace/run_oracle_matrix.py").read_text()
    for token in (
            "chorus_flower_remove_end_stone_support",
            "redstone_comparator_unpowered_support_remove_drop",
            "redstone_lever_floor_support_remove_drop",
            "redstone_repeater_unpowered_support_remove_drop",
            "redstone_stone_button_wall_support_remove_drop",
            "redstone_stone_pressure_plate_support_remove_drop",
            "redstone_torch_floor_support_remove_drop",
            "redstone_tripwire_hook_alternate_support_remove",
            "redstone_wire_support_remove_powered_line"):
        if token not in matrix:
            raise RuntimeError(f"oracle matrix lost {token}")
    print("PASS support callbacks: 12 families from 9 exact trajectories")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL support callbacks: {exc}")
        raise SystemExit(1)
