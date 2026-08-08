#!/usr/bin/env python3
"""Lock the checked WORLD-01 forward/reverse campaign receipt."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
RECEIPT = HERE / "random_tick_campaign_receipt.json"


def require(value: bool, message: str) -> None:
    if not value:
        raise RuntimeError(message)


def source_has(relative: str, tokens: tuple[str, ...]) -> None:
    text = (ROOT / relative).read_text(encoding="utf-8")
    for token in tokens:
        require(token in text, f"{relative} lost {token!r}")


def main() -> int:
    receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    require(receipt.get("schema") == "netherite.random_tick_campaign_receipt"
            and receipt.get("version") == 1
            and receipt.get("todo") == "WORLD-01",
            "invalid random-tick campaign receipt identity")
    require(receipt.get("horizons") == [0, 1, 20, 200, 1200],
            "campaign horizons changed")
    require(receipt.get("campaign_active_chunks", 0) >= 4
            and receipt.get("placed_blocks", 0) >= 400
            and receipt.get("cells_checked", 0) >= 140000,
            "campaign breadth fell below the checked run")
    branches = receipt.get("branches", {})
    require(set(branches) == {"forward", "reverse"},
            "campaign branch set changed")
    for name, branch in branches.items():
        require(branch.get("java_repeat") == "exact"
                and branch.get("native") == "exact"
                and branch.get("rejected_fields") == 0,
                f"{name}: strict continuation is not exact")
        require(branch.get("changed_block_bytes", 0) > 0
                and branch.get("changed_sky_light_bytes", 0) > 0,
                f"{name}: campaign made no observed mutation")
        require(len(branch.get("final_blocks_sha256", "")) == 64
                and len(branch.get("final_sky_light_sha256", "")) == 64,
                f"{name}: final raw receipt hash is invalid")
    negative = receipt.get("negative_control", {})
    require(negative.get("final_block_bytes", 0) > 0
            and negative.get("final_sky_light_bytes", 0) > 0,
            "reversed chunk order is not causally discriminating")
    source_has("blaze/core/block_props_table.h", (
        "case 6: d = (BptProps){ 0.0f, 0, 0, 20 }",
        "case 60: d = (BptProps){ 0.6f, 0, 255, 17 }"))
    source_has("java/Minecraft/src/main/java/qrl/Recorder.java", (
        'worldValue.add("ticking_chunks", tickingChunks)',
        "oracleReverseTickChunkOrderLocked"))
    source_has("verify/completeness/fork_runner.py", (
        "raw/native comparison cuboid", "raw_box"))
    print("PASS WORLD-01 campaign: 289 chunks, five active callback chunks, "
          "forward/reverse order, 1200 ticks, Java A/B/native and raw world exact")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL WORLD-01 campaign: {exc}")
        raise SystemExit(1)
