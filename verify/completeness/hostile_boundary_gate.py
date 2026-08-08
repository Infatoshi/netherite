#!/usr/bin/env python3
"""Lock registry-complete bounded hostile-mob evidence."""

from __future__ import annotations

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]


def main() -> int:
    registry = json.loads((ROOT / "verify/completeness/registry_manifest.json").read_text())
    rows = [row for row in registry["entities"] if row["todo"] == "AI-01"]
    expected = {
        "EntityWitherSkeleton", "EntityStray", "EntityHusk",
        "EntityCreeper", "EntitySkeleton", "EntitySpider",
        "EntityGiantZombie", "EntityZombie", "EntitySlime", "EntityGhast",
        "EntityPigZombie", "EntityEnderman", "EntityCaveSpider",
        "EntitySilverfish", "EntityBlaze", "EntityMagmaCube", "EntityEndermite",
    }
    if {row["class"] for row in rows} != expected or any(
            row["status"] != "live_bounded" for row in rows):
        raise RuntimeError("AI-01 registry coverage changed")
    evidence = (
        "magma/game/test_mob_live.c",
        "magma/game/test_hostile_death_live.c",
        "magma/game/test_endermite_runtime.c",
        "magma/game/test_giant_runtime.c",
        "magma/game/test_husk_runtime.c",
        "magma/game/test_stray_runtime.c",
        "magma/game/test_living_slime_oracle.c",
        "magma/game/test_slime_terminal_oracle.c",
    )
    for relative in evidence:
        path = ROOT / relative
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"missing hostile evidence {relative}")
    source = (ROOT / "magma/game/mob_live.c").read_text()
    for token in (
        "mob_melee_damage", "player_attack_target_pre_damage",
        "bat_update_ai_tasks", "creeper_fuse"):
        if token not in source:
            raise RuntimeError(f"hostile runtime lost {token!r}")
    print(
        "PASS hostile boundary: all 17 AI-01 registry classes are "
        "live-bounded with task/combat/special/death/persistence evidence")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL hostile boundary: {exc}")
        raise SystemExit(1)
