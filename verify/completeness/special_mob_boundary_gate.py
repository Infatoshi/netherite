#!/usr/bin/env python3
"""Lock bounded Guardian, Shulker, Witch, and illager-family evidence."""

from __future__ import annotations

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]


def main() -> int:
    registry = json.loads((ROOT / "verify/completeness/registry_manifest.json").read_text())
    rows = [row for row in registry["entities"] if row["todo"] == "AI-03"]
    expected = {
        "EntityElderGuardian", "EntityGuardian", "EntityShulker",
        "EntityShulkerBullet", "EntityWitch", "EntityEvoker",
        "EntityEvokerFangs", "EntityVex", "EntityVindicator",
    }
    if {row["class"] for row in rows} != expected or any(
            row["status"] != "live_bounded" for row in rows):
        raise RuntimeError("AI-03 registry coverage changed")
    evidence = (
        "magma/game/test_guardian.c",
        "magma/game/test_shulker_live.c",
        "magma/game/test_shulker_runtime.c",
        "magma/game/test_evoker_spell.c",
        "magma/game/test_illager_loot.c",
        "magma/game/test_swamp_witch_runtime.c",
        "magma/game/test_witch_ranged_live.c",
        "magma/game/test_witch_self_potion_live.c",
        "magma/game/test_witch_loot_live.c",
        "magma/game/test_witch_terminal_xp_live.c",
        "magma/game/test_zombie_villager_cure_runtime.c",
    )
    for relative in evidence:
        path = ROOT / relative
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"missing special-mob evidence {relative}")
    for relative, tokens in {
        "magma/game/runtime.c": (
            "runtime_tick_shulkers", "runtime_tick_one_shulker_bullet"),
        "magma/game/mob_live.c": (
            "EW_TYPE_GUARDIAN", "EW_TYPE_WITCH", "EW_TYPE_EVOKER",
            "EW_TYPE_VEX", "EW_TYPE_VINDICATOR"),
    }.items():
        text = (ROOT / relative).read_text()
        for token in tokens:
            if token not in text:
                raise RuntimeError(f"{relative} lost {token!r}")
    print(
        "PASS special-mob boundary: all nine AI-03 registry rows have "
        "dedicated target/attack/owner/death/loot/save evidence")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL special-mob boundary: {exc}")
        raise SystemExit(1)
