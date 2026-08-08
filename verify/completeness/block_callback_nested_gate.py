#!/usr/bin/env python3
"""Lock nested callbacks reached by exact public mutation campaigns."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads((HERE / "block_callback_nested_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_nested_receipt" or receipt.get("version") != 1:
        raise RuntimeError("invalid nested callback receipt")
    covered = {(row[0], row[1]) for row in receipt.get("families", [])}
    if len(covered) != 6:
        raise RuntimeError("nested callback family set changed")
    families = {(family["owner"].rsplit(".", 1)[-1], family["callback"]) for family in census()["families"]}
    if not covered <= families:
        raise RuntimeError("nested callback census owners changed")
    evidence = (
        ROOT / "magma/game/test_plant_support_neighbor_oracle.c",
        ROOT / "magma/trace/test_plant_support_neighbor.py",
        ROOT / "magma/game/test_leaf_decay_oracle.c",
        ROOT / "magma/trace/test_leaf_decay.py",
        ROOT / "magma/game/test_static_lava_neighbor_oracle.c",
        ROOT / "magma/trace/test_static_lava_neighbor.py",
    )
    if any(not path.is_file() or path.stat().st_size == 0 for path in evidence):
        raise RuntimeError("nested callback evidence missing")
    plant = evidence[0].read_text()
    for token in ("fixture < 55", "fixture < 97", "fixture == 98"):
        if token not in plant:
            raise RuntimeError(f"nested plant fixture lost {token}")
    if "fixture >= 11" not in evidence[2].read_text():
        raise RuntimeError("nested log-break fixture lost")
    if "gm_runtime_set_block" not in evidence[4].read_text():
        raise RuntimeError("nested liquid-neighbor fixture lost")
    print("PASS nested callbacks: 6 families through exact mutation paths")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL nested callbacks: {exc}")
        raise SystemExit(1)
