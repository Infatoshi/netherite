#!/usr/bin/env python3
"""Lock the exact detector-rail scheduled callback proof."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_detector_receipt.json").read_text())
    if receipt.get("schema") != \
            "netherite.block_callback_detector_receipt" \
            or receipt.get("version") != 1 \
            or receipt.get("java_rows") != 66 \
            or receipt.get("native_rows") != 66 \
            or receipt.get("exact") is not True \
            or len(receipt.get("output_sha256", "")) != 64 \
            or receipt.get("detector_activation") != "D 9 1" \
            or receipt.get("detector_update_tick") != "D2 1 0":
        raise RuntimeError("invalid detector callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    if covered != {("BlockRailDetector", "updateTick")}:
        raise RuntimeError("detector callback family changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("detector callback census owner changed")
    java = (ROOT / "java/Minecraft/src/main/java/qrl/MinecartGolden.java").read_text()
    native = (ROOT / "magma/game/test_minecart_live.c").read_text()
    runtime = (ROOT / "magma/game/runtime.c").read_text()
    for source, tokens in (
            (java, ("Blocks.DETECTOR_RAIL.updateTick", '"D2 %d %d%n"')),
            (native, ("schedule detector update callback", '"D2 %d %d\\n"')),
            (runtime, ("runtime_minecart_detector_update", "entry.block == 28"))):
        for token in tokens:
            if token not in source:
                raise RuntimeError(f"detector proof lost {token}")
    print("PASS detector callback: exact Java/native activation and updateTick")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL detector callback: {exc}")
        raise SystemExit(1)
