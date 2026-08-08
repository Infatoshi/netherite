#!/usr/bin/env python3
"""Lock ore drop callbacks against the live Java harvest campaign."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_harvest_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_harvest_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("case_count"), receipt.get("pass"),
                receipt.get("fail")) != (390, 390, 0):
        raise RuntimeError("invalid harvest callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    expected = {
        ("BlockOre", "dropBlockAsItemWithChance"),
        ("BlockRedstoneOre", "dropBlockAsItemWithChance"),
    }
    if covered != expected:
        raise RuntimeError("harvest callback family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("harvest callback census owners changed")
    trace = (ROOT / "magma/trace/test_block_harvest.py").read_text()
    native = (ROOT / "magma/game/test_block_harvest_oracle.c").read_text()
    for token in ('("coal", 16', '("diamond", 56',
                  '("redstone", 73', '("lit_redstone", 74'):
        if token not in trace:
            raise RuntimeError(f"harvest fixture lost {token}")
    if "gm_runtime_harvest_drop_result" not in native:
        raise RuntimeError("native harvest callback entry lost")
    print("PASS harvest callbacks: ore and redstone ore across 390 exact rows")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL harvest callbacks: {exc}")
        raise SystemExit(1)
