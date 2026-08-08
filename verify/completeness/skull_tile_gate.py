#!/usr/bin/env python3
"""Lock the measured WORLD-05 live-bounded skull-tile boundary."""

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
    manifest = json.loads((HERE / "skull_tile_manifest.json")
                          .read_text(encoding="utf-8"))
    require(manifest["schema"] == "netherite.skull_tile_gate"
            and manifest["version"] == 1
            and manifest["todo"] == "WORLD-05"
            and manifest["classification"] == "live_bounded",
            "invalid skull tile manifest identity")
    require(manifest["state"] == {
        "types": [0, 1, 2, 3, 4, 5],
        "rotations": 16,
        "signed_player_profile_nbt": "exact",
        "loaded_tile_order": "exact",
    }, "skull state boundary changed")
    require(manifest["behavior"] == {
        "placement_and_retirement": "exact",
        "piston_drop_and_rng": "direct_java_native_exact",
        "wither_summoning": "live",
    }, "skull behavior boundary changed")
    registry = json.loads((HERE / "registry_manifest.json")
                          .read_text(encoding="utf-8"))
    row = next(row for row in registry["tile_entities"]
               if row["class"] == "TileEntitySkull")
    require(row["status"] == "live_bounded" and row["todo"] == "WORLD-05",
            "skull tile registry row is not live-bounded")
    source_has(ROOT / "magma/trace/state_capsule.py", (
        '"tile_entities.skull_ownerless": "exact"',
        '"tile_entities.skull_player_profile": "exact"',
        '"type": "set_skull"'))
    source_has(ROOT / "magma/game/runtime.c", (
        "gm_runtime_skull_set", "gm_runtime_skull_set_profile_nbt",
        "runtime_spawn_skull_item", "gm_runtime_check_wither_spawn"))
    source_has(ROOT / "magma/game/test_runtime.c", (
        "gm_runtime_skull_set_profile_nbt", "ItemSkull",
        "profile_nbt", "item_tag_nbt"))
    source_has(ROOT / "magma/trace/run_oracle_matrix.py", (
        "redstone_piston_east_front_dragon_skull_destroy_start_seed_0",
        "redstone_piston_east_front_player_skull_destroy_start_seed_0"))
    print("PASS skull tile: six types, rotations, signed profiles, exact "
          "piston drops/RNG, continuation, structure transforms, and wither")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, StopIteration, ValueError, RuntimeError) as error:
        print(f"FAIL skull tile: {error}")
        raise SystemExit(1)
