#!/usr/bin/env python3
"""Lock exact crop and bed destruction/drop callbacks."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads((HERE / "block_callback_drops_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_drops_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("case_count"), receipt.get("pass"),
                receipt.get("fail"), receipt.get("retries")) \
            != (7, 7, 0, 0) \
            or receipt.get("state_divergences") != 0 \
            or len(receipt.get("summary_sha256", "")) != 64:
        raise RuntimeError("invalid drop callback receipt")
    covered = {(row[0], row[1]) for row in receipt.get("families", [])}
    if len(covered) != 6:
        raise RuntimeError("drop callback family set changed")
    families = {(family["owner"].rsplit(".", 1)[-1], family["callback"]) for family in census()["families"]}
    if not covered <= families:
        raise RuntimeError("drop callback census owners changed")
    matrix = (ROOT / "magma/trace/run_oracle_matrix.py").read_text()
    for token in (
            "wheat_invalid_support_random_tick_drop",
            "potato_invalid_support_random_tick_regrow",
            "nether_wart_invalid_support_random_tick_age_0",
            "nether_wart_invalid_support_random_tick_age_3",
            "pumpkin_stem_invalid_support_random_tick_regrow",
            "melon_stem_invalid_support_random_tick_regrow",
            "redstone_piston_east_front_bed_foot_destroy_start"):
        if token not in matrix:
            raise RuntimeError(f"oracle matrix lost {token}")
    print("PASS drop callbacks: 6 families from 7 exact trajectories")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL drop callbacks: {exc}")
        raise SystemExit(1)
