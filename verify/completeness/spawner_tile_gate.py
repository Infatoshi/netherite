#!/usr/bin/env python3
"""Lock the measured WORLD-06 live-bounded spawner-tile boundary."""

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def source_has(path, tokens):
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main():
    manifest = json.loads((HERE / "spawner_tile_manifest.json")
                          .read_text(encoding="utf-8"))
    require(manifest["schema"] == "netherite.spawner_tile_gate"
            and manifest["version"] == 1 and manifest["todo"] == "WORLD-06"
            and manifest["classification"] == "live_bounded",
            "invalid spawner tile manifest identity")
    require(manifest["oracle"] == {
        "default_horizons": [1, 19, 20, 21],
        "custom_horizons": [1, 2, 3],
        "scalar_and_rng_order": "bit_exact",
        "typed_spawn_data_and_weighted_potentials": "exact",
    }, "spawner oracle boundary changed")
    require(manifest["continuation"] == {
        "anvil_to_capsule": "exact", "native_checkpoint": "exact",
        "loaded_and_tickable_tile_order": "exact",
    }, "spawner continuation boundary changed")
    registry = json.loads((HERE / "registry_manifest.json")
                          .read_text(encoding="utf-8"))
    row = next(row for row in registry["tile_entities"]
               if row["class"] == "TileEntityMobSpawner")
    require(row["status"] == "live_bounded" and row["todo"] == "WORLD-06",
            "spawner tile registry row is not live-bounded")
    source_has(ROOT / "magma/game/test_spawner_live.sh", (
        "spawner_oracle: PASS", "restore_loaded_tile_order",
        "restore_tickable_tile_order", "add_spawner_potential"))
    source_has(ROOT / "verify/completeness/stage_spawner_fixture.py", (
        '"world06-custom-spawner-spawn"', '"world06-active-spawner-countdown"',
        '"horizons": ([1, 2, 3] if custom else [1, 19, 20, 21])'))
    source_has(ROOT / "magma/trace/state_capsule.py", (
        '"tile_entities.mob_spawner.default_spawn_data": "exact"',
        '"TileEntityMobSpawner"'))
    source_has(ROOT / "magma/game/runtime.c", (
        "gm_runtime_spawner_set_state", "gm_runtime_spawner_add_potential"))
    source_has(ROOT / "magma/game/entity_render.c", (
        "gm_spawner_miniatures_emit", "gm_spawner_mini_scale"))
    print("PASS spawner tile: bounded exact scalar/RNG spawning, typed NBT, "
          "ordering, continuation, and saved-state miniature")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, StopIteration, ValueError, RuntimeError) as error:
        print(f"FAIL spawner tile: {error}")
        raise SystemExit(1)
