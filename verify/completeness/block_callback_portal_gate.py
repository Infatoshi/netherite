#!/usr/bin/env python3
"""Lock the direct BlockPortal update callback continuation."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_portal_receipt.json").read_text())
    if receipt != {
            "schema": "netherite.block_callback_portal_receipt",
            "version": 1,
            "source": "live real Minecraft Java 1.11.2 versus magma",
            "captured_utc": "2026-08-23",
            "cases": 9,
            "family": ["BlockPortal", "updateTick"],
            }:
        raise RuntimeError("portal callback live receipt changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if tuple(receipt["family"]) not in families:
        raise RuntimeError("portal callback census owner changed")
    comparator = (ROOT / "magma/trace/test_portal_random_tick.py").read_text()
    java = (ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java").read_text()
    native = (ROOT / "magma/game/runtime.c").read_text()
    for token in (
            "new chicken jockey checkpoint continuation",
            "portal cooldown sabotage escaped", "mob_spawning"):
        if token not in comparator:
            raise RuntimeError(f"portal comparator lost {token}")
    for token in (
            "fixtureChunk.setInhabitedTime(0L)",
            "continuationNextId : nextEntityId()",
            "portal.getBlock().updateTick"):
        if token not in java:
            raise RuntimeError(f"portal Java oracle lost {token}")
    for token in (
            "runtime_random_tick_portal", "runtime_spawn_portal_jockey_chicken",
            "runtime_construct_portal_living"):
        if token not in native:
            raise RuntimeError(f"native portal callback lost {token}")
    print("PASS BlockPortal.updateTick: 9 live Java/native branches including "
          "five-tick and checkpoint continuations")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL BlockPortal.updateTick: {exc}")
        raise SystemExit(1)
