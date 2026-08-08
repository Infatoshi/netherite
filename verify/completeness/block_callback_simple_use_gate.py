#!/usr/bin/env python3
"""Lock exact physical activation for low-state special blocks."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_simple_use_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_simple_use_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("selected_cases"), receipt.get("pass"),
                receipt.get("fail")) != (4, 4, 0) \
            or receipt.get("state_divergences") != 0 \
            or len(receipt.get("source_summaries", [])) != 2 \
            or any(len(value) != 64 for value in receipt["source_summaries"]):
        raise RuntimeError("invalid simple-use callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    expected = {
        (owner, "onBlockActivated") for owner in (
            "BlockCommandBlock", "BlockFence", "BlockSign", "BlockStructure")
    }
    if covered != expected:
        raise RuntimeError("simple-use callback family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("simple-use callback census owners changed")
    matrix = (ROOT / "magma/trace/run_oracle_matrix.py").read_text()
    for name in ("command_block", "fence", "sign", "structure"):
        if name not in matrix:
            raise RuntimeError(f"simple-use oracle lost {name}")
    capsule = (ROOT / "magma/trace/state_capsule.py").read_text()
    if "without byte reordering" not in capsule:
        raise RuntimeError("raw decorative NBT continuation regressed")
    print("PASS simple-use callbacks: 4 physical activation trajectories exact")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL simple-use callbacks: {exc}")
        raise SystemExit(1)
