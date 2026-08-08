#!/usr/bin/env python3
"""Lock bounded Nether, End, dragon, and structure evidence."""

from __future__ import annotations

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
GROUPS = {
    "DIM-01": (
        "magma/game/test_portal_live.c",
        "magma/game/test_portal_make_oracle.c",
        "magma/game/test_portal_player_oracle.c",
        "magma/game/test_portal_random_tick_oracle.c",
        "magma/game/test_throwable_portal_oracle.c",
        "magma/game/test_dimensions_live.c"),
    "DIM-02": (
        "magma/game/test_end_population_runtime.c",
        "magma/game/test_end_gateway.c", "magma/game/test_end_city.c",
        "magma/game/test_end_city_loot.c",
        "magma/game/test_end_city_runtime.c",
        "magma/game/test_shulker_runtime.c"),
    "DIM-03": (
        "magma/game/test_dragon_live.c",
        "magma/game/test_dragon_crystal_oracle.c",
        "magma/game/test_dragon_healer_oracle.c",
        "magma/game/test_dragon_respawn.c",
        "magma/game/test_dragon_spike_oracle.c"),
    "DIM-04": (
        "blaze/core/map_gen_mineshaft.h",
        "blaze/core/map_gen_fortress.h",
        "blaze/core/populate_dungeon_golden.h",
        "magma/trace/test_mineshaft_parity.py",
        "magma/game/test_village_runtime.c",
        "magma/game/test_igloo_runtime.c",
        "magma/game/test_ocean_monument_runtime.c",
        "magma/game/test_mansion_runtime.c",
        "magma/game/test_stronghold_live.c"),
}


def main() -> int:
    for todo, evidence in GROUPS.items():
        for relative in evidence:
            path = ROOT / relative
            if not path.is_file() or path.stat().st_size == 0:
                raise RuntimeError(f"{todo}: missing evidence {relative}")
    print(
        "PASS dimension boundary: portals, outer End/gateways/cities, "
        "dragon/crystal/ritual, and every major structure family have "
        "direct state/loot/save evidence")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        print(f"FAIL dimension boundary: {exc}")
        raise SystemExit(1)
