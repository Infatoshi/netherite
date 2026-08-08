#!/usr/bin/env python3
"""Lock strict player-use, neighbor, and break trajectories."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_actions_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_actions_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("case_count"), receipt.get("pass"),
                receipt.get("fail"), receipt.get("retries")) \
            != (16, 16, 0, 0) \
            or receipt.get("state_divergences") != 0 \
            or len(receipt.get("summary_sha256", "")) != 64 \
            or receipt.get("excluded_case") != \
            "redstone_player_use_daylight_detector_seed_0":
        raise RuntimeError("invalid action callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    if len(covered) != 16:
        raise RuntimeError("action callback family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("action callback census owners changed")
    matrix = (ROOT / "magma/trace/run_oracle_matrix.py").read_text()
    tokens = (
        "ordinary_player_ignite_tnt_flint",
        "ordinary_player_pot_red_flower",
        "ordinary_player_use_cake",
        "ordinary_player_use_oak_door",
        "ordinary_player_use_oak_fence_gate",
        "ordinary_player_use_oak_trapdoor",
        "redstone_observer_powered_break",
        "redstone_piston_east_empty_extension_start",
        "redstone_player_use_floor_comparator",
        "redstone_player_use_floor_repeater",
        "redstone_player_use_wall_lever",
        "redstone_player_use_wall_stone_button",
        "redstone_power_oak_door",
        "redstone_power_oak_fence_gate",
        "redstone_power_oak_trapdoor",
        "redstone_tripwire_hook_powered_support_remove",
    )
    for token in tokens:
        if token not in matrix:
            raise RuntimeError(f"oracle matrix lost {token}")
    print("PASS action callbacks: 16 exact live Java/native families")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL action callbacks: {exc}")
        raise SystemExit(1)
