#!/usr/bin/env python3
"""Lock bounded world callback, edit, tile, light, weather, and gen evidence."""

from __future__ import annotations

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]

GROUPS = {
    "WORLD-03": (
        "magma/game/test_fluid_live.c", "magma/game/test_plants_live.c",
        "magma/game/test_leaf_decay_oracle.c",
        "magma/game/test_plant_support_neighbor_oracle.c",
        "magma/game/test_falling_anvil_oracle.c",
        "magma/game/test_falling_dragon_egg_oracle.c",
        "magma/game/test_portal_live.c"),
    "WORLD-04": (
        "magma/game/test_tnt_explosion.c",
        "magma/game/test_explosion_fire_oracle.c",
        "magma/game/test_dragon_crystal_oracle.c",
        "magma/game/test_brewing_live.c"),
    "WORLD-05": (
        "verify/completeness/decorative_tile_gate.py",
        "verify/completeness/skull_tile_gate.py",
        "magma/game/test_special_item_use_oracle.c",
        "magma/game/test_hanging_runtime.c",
        "magma/game/test_map_update_oracle.c"),
    "WORLD-07": (
        "magma/game/test_sky_light_column_oracle.c",
        "magma/game/test_biome_color.c", "magma/tests/test_light.c",
        "verify/completeness/test_chunk_bundle.py"),
    "WORLD-08": (
        "magma/game/test_weather_lightning.c",
        "magma/game/test_weather_world.c",
        "magma/game/test_weather_render.c",
        "magma/game/test_weather_runtime.sh"),
    "WORLD-09": (
        "verify/completeness/test_native_checkpoint.py",
        "verify/completeness/test_native_save_slot.py",
        "magma/game/test_runtime.c"),
    "WORLD-10": (
        "verify/worldgen/wrapper_gate.sh", "verify/worldgen/wrapper_diff.sh",
        "verify/worldgen/nether_census.py", "magma/tests/test_dim_worldgen.py",
        "magma/game/test_end_population_runtime.c"),
    "WORLD-11": (
        "magma/game/config.c", "magma/game/test_config.c",
        "blaze/cpu/chunk_provider_flat.c", "magma/game/world_live.c"),
}


def main() -> int:
    for todo, evidence in GROUPS.items():
        for relative in evidence:
            path = ROOT / relative
            if not path.is_file() or path.stat().st_size == 0:
                raise RuntimeError(f"{todo}: missing evidence {relative}")
    runtime = (ROOT / "magma/game/runtime.c").read_text()
    for token in (
        "runtime_tick_fire", "runtime_random_tick_leaves",
        "runtime_explosion", "runtime_update_filled_map",
        "runtime_tick_weather", "runtime_tick_loaded_random_blocks"):
        if token not in runtime:
            raise RuntimeError(f"world runtime lost {token!r}")
    print(
        "PASS world-system boundary: WORLD-03/04/05/07/08/09/10/11 "
        "have fail-closed callback/edit/tile/light/weather/global/gen/profile "
        "evidence")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        print(f"FAIL world-system boundary: {exc}")
        raise SystemExit(1)
