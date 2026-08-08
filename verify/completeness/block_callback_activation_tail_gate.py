#!/usr/bin/env python3
"""Lock direct activation for Dragon Egg and Moving Piston."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_activation_tail_receipt.json").read_text())
    if receipt.get("schema") \
            != "netherite.block_callback_activation_tail_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("case_count"), receipt.get("pass"),
                receipt.get("fail")) != (2, 2, 0):
        raise RuntimeError("invalid activation-tail callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    if covered != {
            ("BlockDragonEgg", "onBlockActivated"),
            ("BlockPistonMoving", "onBlockActivated")}:
        raise RuntimeError("activation-tail callback family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("activation-tail callback census owners changed")
    java = (ROOT / "java/Minecraft/src/main/java/qrl/BlockCallbackActivationGolden.java").read_text()
    native = (ROOT / "magma/game/test_block_callback_activation.c").read_text()
    for owner in ("BlockDragonEgg", "BlockPistonMoving"):
        if owner not in java or owner not in native:
            raise RuntimeError(f"activation-tail executor lost {owner}")
    print("PASS activation callbacks: Dragon Egg and Moving Piston exact")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL activation callbacks: {exc}")
        raise SystemExit(1)
