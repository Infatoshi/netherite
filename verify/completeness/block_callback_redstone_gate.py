#!/usr/bin/env python3
"""Lock strict scheduled redstone callback trajectories."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_redstone_receipt.json").read_text())
    if receipt.get("schema") != \
            "netherite.block_callback_redstone_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("case_count"), receipt.get("pass"),
                receipt.get("fail"), receipt.get("retries")) \
            != (10, 10, 0, 0) \
            or receipt.get("state_divergences") != 0 \
            or len(receipt.get("summary_sha256", "")) != 64:
        raise RuntimeError("invalid redstone callback receipt")
    cases = receipt.get("cases", [])
    if len(cases) != 10 or len(set(cases)) != 10:
        raise RuntimeError("redstone callback case set changed")
    covered = {tuple(row) for row in receipt.get("families", [])}
    if len(covered) != 12:
        raise RuntimeError("redstone callback family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("redstone callback census owners changed")
    matrix = (ROOT / "magma/trace/run_oracle_matrix.py").read_text()
    for case in cases:
        if case.removesuffix("_seed_0") not in matrix:
            raise RuntimeError(f"redstone matrix lost {case}")
    runtime = (ROOT / "magma/game/runtime.c").read_text()
    for token in (
            "runtime_redstone_neighbor_changed",
            "runtime_redstone_observer_tick_pending",
            "runtime_redstone_torch_is_burned_out",
            "runtime_redstone_tripwire_hook_calculate",
            "runtime_tick_scheduled"):
        if token not in runtime:
            raise RuntimeError(f"native redstone callback lost {token}")
    print("PASS redstone callbacks: 12 direct scheduled/neighbor families "
          "from 10 exact live Java/native trajectories")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL redstone callbacks: {exc}")
        raise SystemExit(1)
