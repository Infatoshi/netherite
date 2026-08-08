#!/usr/bin/env python3
"""Lock the final non-detector scheduled callback trajectories."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_update_tail_receipt.json").read_text())
    if receipt.get("schema") != \
            "netherite.block_callback_update_tail_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("case_count"), receipt.get("pass"),
                receipt.get("fail"), receipt.get("retries")) \
            != (4, 4, 0, 0) \
            or receipt.get("state_divergences") != 0:
        raise RuntimeError("invalid update-tail callback receipt")
    cases = receipt.get("cases", [])
    covered = {tuple(row) for row in receipt.get("families", [])}
    if len(cases) != 4 or len(set(cases)) != 4 or len(covered) != 4:
        raise RuntimeError("update-tail callback set changed")
    summaries = receipt.get("source_summaries", [])
    if [row.get("case_count") for row in summaries] != [3, 1] \
            or any(len(row.get("sha256", "")) != 64 for row in summaries):
        raise RuntimeError("update-tail source receipt changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("update-tail callback census owners changed")
    matrix = (ROOT / "magma/trace/run_oracle_matrix.py").read_text()
    for case in cases:
        if case.removesuffix("_seed_0") not in matrix:
            raise RuntimeError(f"oracle matrix lost {case}")
    runtime = (ROOT / "magma/game/runtime.c").read_text()
    for token in (
            "runtime_redstone_chorus_plant_can_survive",
            "runtime_tick_command_block",
            "runtime_tick_fire",
            "runtime_redstone_tripwire_update_state"):
        if token not in runtime:
            raise RuntimeError(f"native callback lost {token}")
    print("PASS update-tail callbacks: 4 exact live Java/native trajectories")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL update-tail callbacks: {exc}")
        raise SystemExit(1)
