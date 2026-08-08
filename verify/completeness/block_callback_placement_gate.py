#!/usr/bin/env python3
"""Lock every strict placement callback to the exhaustive ItemBlock oracle."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_placement_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_placement_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("rows"), receipt.get("positive"),
                receipt.get("negative"), receipt.get("fail")) \
            != (52635, 50256, 2379, 0):
        raise RuntimeError("invalid placement callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    expected = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
        if family["callback"] == "onBlockPlacedBy"
    }
    if covered != expected:
        raise RuntimeError(
            f"placement callback drift: missing={sorted(expected-covered)}, "
            f"extra={sorted(covered-expected)}")
    java = (ROOT / "java/Minecraft/src/main/java/qrl/ItemBlockGolden.java").read_text()
    native = (ROOT / "magma/game/test_item_block_oracle.c").read_text()
    shell = (ROOT / "magma/game/test_item_block_oracle.sh").read_text()
    for token in ("for (Item item : Item.REGISTRY)", "item.onItemUse(",
                  "for (int face = 0; face < 6; ++face)",
                  "for (int yaw = 0; yaw < 4; ++yaw)"):
        if token not in java:
            raise RuntimeError(f"Java placement corpus lost {token}")
    for token in ("rows != 52635", "positives != 50256",
                  "ibp_placed_meta_exact", "negative row changed"):
        if token not in native:
            raise RuntimeError(f"native placement comparator lost {token}")
    if "negative control did not fail" not in shell:
        raise RuntimeError("placement negative control lost")
    print("PASS placement callbacks: all 16 families in 52,635-row oracle")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL placement callbacks: {exc}")
        raise SystemExit(1)
