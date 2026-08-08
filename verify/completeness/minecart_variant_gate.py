#!/usr/bin/env python3
"""Lock the measured ENT-07 chest/furnace minecart boundary."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "minecart_variant_manifest.json"
REGISTRY = HERE / "registry_manifest.json"


class MinecartVariantError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise MinecartVariantError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "netherite.minecart_variant_gate"
            and manifest.get("version") == 1
            and manifest.get("todo") == "ENT-07",
            "invalid minecart variant manifest identity")
    require(manifest.get("direct_java_native") == {
        "rail_shapes": 10,
        "straight_powered_braking_slope_and_derailed": "bit_exact",
        "collision_rider_activator_detector_and_damage": "bit_exact",
        "furnace_fuel_push_smoke_speed_and_interaction": "bit_exact",
        "chest_inventory_and_destruction": "bit_exact",
        "spawner_activation_countdown_reset_and_rng": "bit_exact",
        "command_activator_cooldown_payload_and_searge": "bit_exact",
    }, "minecart direct-oracle evidence changed")
    require(manifest.get("structure_persistence") == {
        "minecart_classes": 7,
        "base_display_inventory_fuel_push_and_rng": "exact",
        "fresh_uuid_and_constructor_rng": "exact",
        "all_state_transforms": "exact",
    }, "minecart Structure persistence evidence changed")
    require(manifest.get("continuation") == {
        "arbitrary_tagged_chest_slots": 27,
        "capsule_restore": "exact",
        "native_checkpoint": "exact",
        "loaded_entity_order": "exact",
    }, "minecart continuation evidence changed")
    require(manifest.get("render_contract") == {
        "concrete_subtypes": 7,
        "chest_model_and_texture": "exact",
        "furnace_directional_face": "exact",
        "spawner_and_command_default_displays": "exact",
        "strict_furnace_pixels": "channel_one_floor",
    }, "minecart render evidence changed")
    require(set(manifest.get("promoted", [])) == {
        "EntityMinecartEmpty", "EntityMinecartChest",
        "EntityMinecartFurnace", "EntityMinecartTNT",
        "EntityMinecartMobSpawner", "EntityMinecartHopper",
        "EntityMinecartCommandBlock",
    }, "minecart promotion set changed")
    require(set(manifest.get("remaining", [])) == {
        "deferred_worldgen_chest_minecart_loot_table_lifecycle",
        "arbitrary_non_cube_custom_display_blocks",
        "broader_adjacent_junction_and_chunk_unload_topologies",
        "bounded_chest_fixed_function_shading_ties",
        "general_command_dispatch_owned_by_MODE_02",
        "arbitrary_spawner_entity_tag_families_owned_by_WORLD_06",
        "command_cart_editor_screen_owned_by_UI_02",
    }, "minecart open boundary changed without updating its checked gate")

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    for class_name in manifest["promoted"]:
        row = next(value for value in registry["entities"]
                   if value["class"] == class_name)
        require(row["status"] == "live_bounded"
                and row["todo"] == "ENT-07",
                f"{class_name} registry row is not live-bounded")
    source_has(ROOT / "magma" / "game" / "test_minecart_live.sh", (
        "minecart_oracle: PASS",
        "real 1.11.2 rails, derailment, collision, callbacks"))
    source_has(ROOT / "magma" / "game" / "test_minecart_live.c", (
        "furnace speed-cap fixture", "playable furnace interaction fixture",
        "destroy chest cart with entity drops disabled",
        "all minecart variants produce render views",
        "spawner minecart checkpoint retains exact scalar/RNG state",
        "command minecart checkpoint retains clocks and payload"))
    source_has(ROOT / "magma" / "trace" / "test_structure_block.py", (
        "all seven minecart subtype payloads", "constructor RNG"))
    source_has(ROOT / "magma" / "trace" / "state_capsule.py", (
        '"EntityMinecartChest": (1, 27)',
        '"EntityMinecartFurnace": (2, 0)',
        '"set_minecart_slot"'))
    source_has(ROOT / "magma" / "game" / "runtime.c", (
        "EntityMinecartFurnace.moveAlongTrack",
        "GM_MINECART_CHEST", "GM_MINECART_FURNACE",
        "runtime_tick_minecart_spawner",
        "runtime_minecart_command_trigger"))
    source_has(ROOT / "magma" / "game" / "item_render.c", (
        "GM_VIEW_MINECART_CHEST", "GM_VIEW_MINECART_FURNACE"))
    source_has(ROOT / "magma" / "game" / "entity_render.c", (
        '"EntityMinecartChest"', '"EntityMinecartFurnace"'))
    print(
        "PASS minecart variants: all seven are live-bounded across real-Java "
        "motion, callbacks, payload persistence, continuation, and render")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, StopIteration, ValueError,
            MinecartVariantError) as error:
        print(f"FAIL minecart variants: {error}")
        raise SystemExit(1)
