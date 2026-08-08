#!/usr/bin/env python3
"""Lock promoted mixed-store ordering and equal-distance selectors."""

from __future__ import annotations

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]


def require(relative: str, tokens: tuple[str, ...]) -> None:
    text = (ROOT / relative).read_text(encoding="utf-8")
    for token in tokens:
        if token not in text:
            raise RuntimeError(f"{relative} lost order evidence {token!r}")


def main() -> int:
    require("verify/completeness/save_order_gate.py", (
        "entity_chunk_cycle_forward", "entity_chunk_cycle_reverse",
        "player_minecart", "Java A/B/native"))
    require("verify/completeness/hanging_family_gate.py", (
        '"all_6_permutations_exact"',
        "mixed_item_frame_painting_and_knot_loaded_update_order"))
    require("magma/game/test_hanging_runtime.c", (
        "mixed_hanging_loaded_order",
        "mixed callback drop order follows loadedEntityList"))
    require("magma/game/test_llama_runtime.c", (
        "restore item order opposite the fixed pool slot order",
        "equal-distance spit target follows restored loaded-entity order"))
    require("magma/game/test_tnt_explosion.c", (
        "four-item chain follows supplied loaded order, not cold slots",
        "cold pickup inventory insertion follows supplied loaded order"))
    require("magma/game/test_armor_stand_runtime.c", (
        "nearest stand receives attack instead of earlier-store frame",
        "nearest ordinary mob receives attack instead of earlier-store stand",
        "nearest minecart receives attack instead of earlier-store stand"))
    require("magma/game/runtime.c", (
        "loaded_entity_order", "runtime_loaded_entity_order_append"))
    print(
        "PASS cross-store order: opposite loaded orders, all hanging-store "
        "permutations, equal-distance projectiles, passengers, drops, and "
        "global nearest attacks are regression-locked")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        print(f"FAIL cross-store order: {exc}")
        raise SystemExit(1)
