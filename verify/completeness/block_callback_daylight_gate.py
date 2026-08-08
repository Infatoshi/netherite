#!/usr/bin/env python3
"""Lock physical daylight-detector inversion activation."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads((HERE / "block_callback_daylight_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_daylight_receipt" \
            or receipt.get("version") != 1 or receipt.get("pass") != 1 \
            or receipt.get("state_divergences") != 0 \
            or len(receipt.get("summary_sha256", "")) != 64 \
            or receipt.get("case") != "redstone_player_use_daylight_detector_seed_0":
        raise RuntimeError("invalid daylight callback receipt")
    covered = {(row[0], row[1]) for row in receipt.get("families", [])}
    if covered != {("BlockDaylightDetector", "onBlockActivated")}:
        raise RuntimeError("daylight callback family changed")
    families = {(family["owner"].rsplit(".", 1)[-1], family["callback"]) for family in census()["families"]}
    if not covered <= families:
        raise RuntimeError("daylight callback census owner changed")
    matrix = (ROOT / "magma/trace/run_oracle_matrix.py").read_text()
    if "redstone_player_use_daylight_detector" not in matrix \
            or 'fixture_stage="early"' not in matrix:
        raise RuntimeError("daylight physical-use fixture changed")
    print("PASS daylight callback: exact physical inversion activation")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL daylight callback: {exc}")
        raise SystemExit(1)
