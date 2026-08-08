#!/usr/bin/env python3
"""Lock the final four drop callbacks and their disabled-drop RNG boundary."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_drop_tail_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_drop_tail_receipt" \
            or receipt.get("version") != 1 \
            or receipt.get("game_rule") != "doTileDrops=false" \
            or (receipt.get("case_count"), receipt.get("pass"),
                receipt.get("fail")) != (4, 4, 0):
        raise RuntimeError("invalid drop-tail callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    expected = {
        (owner, "dropBlockAsItemWithChance") for owner in (
            "BlockJukebox", "BlockMobSpawner", "BlockPistonMoving",
            "BlockSilverfish")
    }
    if covered != expected:
        raise RuntimeError("drop-tail callback family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("drop-tail callback census owners changed")
    java = (ROOT / "java/Minecraft/src/main/java/qrl/BlockCallbackDropGolden.java").read_text()
    native = (ROOT / "magma/game/test_block_callback_drop.c").read_text()
    for owner in ("BlockJukebox", "BlockMobSpawner",
                  "BlockPistonMoving", "BlockSilverfish"):
        if owner not in java or owner not in native:
            raise RuntimeError(f"drop-tail executor lost {owner}")
    print("PASS drop callbacks: final 4 families execute with exact RNG controls")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL drop callbacks: {exc}")
        raise SystemExit(1)
