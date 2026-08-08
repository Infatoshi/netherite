#!/usr/bin/env python3
"""Lock root-support callbacks already exercised by the plant campaign."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_plant_root_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_plant_root_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("campaign_rows"), receipt.get("pass"),
                receipt.get("fail")) != (99, 99, 0):
        raise RuntimeError("invalid plant-root callback receipt")
    rows = receipt.get("families", [])
    covered = {(row[0], row[1]) for row in rows}
    if covered != {
            ("BlockBush", "neighborChanged"),
            ("BlockCactus", "neighborChanged"),
            ("BlockReed", "neighborChanged")}:
        raise RuntimeError("plant-root callback family set changed")
    if {row[0]: row[2] for row in rows} != {
            "BlockBush": [0, 15], "BlockCactus": [17], "BlockReed": [16]}:
        raise RuntimeError("plant-root fixture map changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("plant-root callback census owners changed")
    comparator = (ROOT / "magma/trace/test_plant_support_neighbor.py").read_text()
    native = (ROOT / "magma/game/test_plant_support_neighbor_oracle.c").read_text()
    for token in ("vacuous callback fixture", "before_blocks", "after_blocks"):
        if token not in comparator:
            raise RuntimeError(f"plant-root comparator lost {token}")
    for token in ("fixture == 16", "fixture == 17", "plant_ids[fixture]"):
        if token not in native:
            raise RuntimeError(f"plant-root fixture lost {token}")
    print("PASS plant-root callbacks: bush, cactus, and reed exact mutations")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL plant-root callbacks: {exc}")
        raise SystemExit(1)
