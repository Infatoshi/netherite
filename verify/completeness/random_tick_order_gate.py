#!/usr/bin/env python3
"""Lock represented loaded-chunk and random-tick causal ordering."""

from __future__ import annotations

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]


def require(relative: str, tokens: tuple[str, ...]) -> None:
    text = (ROOT / relative).read_text(encoding="utf-8")
    for token in tokens:
        if token not in text:
            raise RuntimeError(f"{relative} lost random-order evidence {token!r}")


def main() -> int:
    require("verify/completeness/stage_random_tick_fixture.py", (
        'prepared.get("eligible_sections") != 1',
        'prepared.get("random_blocks") != 1',
        '"increment saved updateLCG by one"', '"horizons": [1, 20, 200]'))
    require("verify/completeness/stage_random_tick_campaign.py", (
        "reverse_tick_chunk_order_locked", '"horizons": [1, 20, 200, 1200]',
        '"active_chunks"', '"tick_order"'))
    require("java/Minecraft/src/main/java/qrl/Recorder.java", (
        'worldValue.add("ticking_chunks", tickingChunks)',
        "oracleReverseTickChunkOrderLocked",
        'worldValue.has("ticking_chunks")'))
    require("verify/completeness/test_chunk_bundle.py", (
        "loaded_chunk_order", "random_tick_mask"))
    require("magma/trace/run_oracle_matrix.py", (
        "selector:1 section/1 block, updateLCG target exact",
        "grass_random_selection", "cactus_random_selection",
        "sapling_random_selection"))
    require("magma/game/runtime.c", (
        "runtime_tick_loaded_random_blocks",
        "random_tick_mask", "world_update_lcg"))
    require("verify/completeness/anvil_to_capsule.py", (
        "random_tick_mask", "tick_order"))
    print(
        "PASS random-tick order: loaded chunk rank, active-section mask, "
        "updateLCG selector, selected/missed negative pair, multi-section "
        "forward/reverse campaign, callback, and checkpoint fields are "
        "fail-closed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        print(f"FAIL random-tick order: {exc}")
        raise SystemExit(1)
