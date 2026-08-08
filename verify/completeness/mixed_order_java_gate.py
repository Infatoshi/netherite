#!/usr/bin/env python3
"""Lock AI-05's opposite-order real-Java A/B/native campaign."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
RECEIPT = HERE / "mixed_order_java_receipt.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    require(receipt.get("schema") == "netherite.ai05_mixed_order_receipt"
            and receipt.get("version") == 1
            and receipt.get("todo") == "AI-05",
            "invalid mixed-order receipt identity")
    require((receipt.get("entity_count"), receipt.get("store_count"),
             receipt.get("actions")) == (103, 5, 1200),
            "mixed-order campaign dimensions changed")
    require(receipt.get("horizons") == [0, 1, 20, 200, 1200],
            "mixed-order horizons changed")
    forward, reverse = receipt["forward"], receipt["reverse"]
    require((forward["first_eid"], forward["last_eid"])
            == (reverse["last_eid"], reverse["first_eid"])
            == (62000, 62102), "opposite-order negative control changed")
    for name, case in (("forward", forward), ("reverse", reverse)):
        require(case["java_a_sha256"] == case["java_b_sha256"],
                f"{name}: Java A/B trace is not byte-identical")
        require(all(len(case[field]) == 64 for field in (
                    "java_a_sha256", "java_b_sha256", "native_sha256")),
                f"{name}: invalid retained trace digest")
    require(receipt["requirements"] == {
        "java_repeat": "exact", "native": "exact",
        "represented_t0": "exact", "state_ticks_compared": 1201,
        "raw_horizons": "exact", "rejected_fields": 0,
        "world_cells": 17918, "persisted_do_mob_loot": False,
    }, "mixed-order strict requirements changed")
    source_has(HERE / "stage_mixed_order_campaign.py", (
        "set_random_tick_speed_locked", "reverse 103-entity spawn insertion order",
        '"horizons": [1, 20, 200, 1200]'))
    source_has(HERE / "fork_runner.py", (
        '"do_mob_loot"', "_normalized_entities", "state_ticks_compared"))
    source_has(ROOT / "magma" / "trace" / "state_capsule.py", (
        '"doMobLoot"', '"restore_squid_state"', '"restore_loaded_entity_order"'))
    source_has(ROOT / "magma" / "game" / "mob_live.c", (
        "tick_squid_air_tail", "living_talk_interval", "loaded_order_prepare"))
    print("PASS AI-05 mixed order: 103 entities, five stores, opposite "
          "orders, Java A/B/native exact through tick 1200")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as error:
        print(f"FAIL AI-05 mixed order: {error}")
        raise SystemExit(1)
