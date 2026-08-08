#!/usr/bin/env python3
"""Lock the exact powered-TNT neighbor callback trajectory."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_tnt_neighbor_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_tnt_neighbor_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("selected_cases"), receipt.get("pass"),
                receipt.get("fail")) != (1, 1, 0) \
            or receipt.get("state_divergences") != 0 \
            or any(len(receipt.get(key, "")) != 64 for key in (
                "campaign_summary_sha256", "neighbor_summary_sha256")):
        raise RuntimeError("invalid TNT-neighbor callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    if covered != {("BlockTNT", "neighborChanged")}:
        raise RuntimeError("TNT-neighbor callback family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("TNT-neighbor callback census owner changed")
    matrix = (ROOT / "magma/trace/run_oracle_matrix.py").read_text()
    runtime = (ROOT / "magma/game/runtime.c").read_text()
    for token in ("redstone_tnt_ignite_seed_0", "redstone_tnt_direct_add_powered_seed_0"):
        if token not in matrix:
            raise RuntimeError(f"TNT oracle case lost {token}")
    for token in ("prime_tnt_on_add", "id == 46 && runtime_redstone_is_powered"):
        if token not in runtime:
            raise RuntimeError(f"TNT native callback lost {token}")
    print("PASS TNT callback: exact powered neighbor ignition trajectory")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL TNT callback: {exc}")
        raise SystemExit(1)
