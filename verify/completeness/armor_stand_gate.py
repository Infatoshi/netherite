#!/usr/bin/env python3
"""Lock the measured ENT-04 live/save boundary without claiming closure."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "armor_stand_manifest.json"
REGISTRY = HERE / "registry_manifest.json"


class ArmorStandError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ArmorStandError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "netherite.armor_stand_gate"
            and manifest.get("version") == 1
            and manifest.get("todo") == "ENT-04",
            "invalid Armor Stand manifest identity")
    case = manifest.get("case", {})
    require(case == {
        "horizons": [0, 1, 2, 3, 4, 8, 20],
        "java_repeat": "exact",
        "native": "exact",
        "represented_t0": "exact",
        "rejected_fields": 0,
        "state_ticks_compared": 21,
        "raw_horizons": "exact",
        "world_probe_cells": 3757,
        "nbt_roundtrip_count": 1,
        "nbt_persistent": "exact",
        "nbt_resets": [
            "ticks_existed", "punch_cooldown", "last_damage",
            "entity_random",
        ],
    }, "checked Java/native Armor Stand continuation changed")
    behavior = manifest.get("behavior_oracle", {})
    interactions = behavior.get("interactions", {})
    damage = behavior.get("damage", {})
    require(behavior.get("rows") == 14
            and interactions.get("equip_two_helmets") == {
                "result": "success", "player": [310, 1, 7],
                "stand": [[5, 310, 1, 7]],
                "sound": ["item.armor.equip_diamond", "neutral"],
            }
            and interactions.get("disabled_head") == {
                "result": "fail", "disabled_bit": 4}
            and interactions.get("hidden_arms") == {"result": "fail"}
            and interactions.get("remove_boots") == {
                "result": "success", "player": [313, 1, 4],
                "sound": ["item.armor.equip_diamond", "players"],
            }
            and interactions.get("name_tag") == {"result": "pass"},
            "checked Java Armor Stand interaction rows changed")
    require(damage.get("first_punch") == {
                "active": True, "status": 32}
            and damage.get("second_punch") == {
                "active": False,
                "drops": [[416, 1, 0], [310, 1, 2]],
                "sound": "entity.armorstand.break",
                "dust": [38, 10, 5],
            }
            and damage.get("arrow") == {
                "active": False, "arrow_dead": True,
                "drops": [[416, 1, 0], [280, 1, 0]],
                "dust": [38, 10, 5],
            }
            and damage.get("explosion") == {
                "active": False, "drops": [[313, 1, 1]], "dust": []}
            and damage.get("creative") == {
                "active": False, "drops": [], "dust": [38, 10, 5]}
            and damage.get("in_fire_first") == {
                "health_bits": "41a00000", "fire": 100}
            and damage.get("in_fire_repeat") == {
                "health_bits": "419ecccd", "fire": 100}
            and damage.get("on_fire") == {
                "health_bits": "41800000", "fire": -1}
            and damage.get("void") == {
                "active": False, "drops": [], "dust": []}
            and behavior.get("normal_dust_descriptor_bits") == [
                "3fc0000000000000", "3fdf9999a0000000",
                "3fc0000000000000", "3fa999999999999a",
            ], "checked Java Armor Stand damage/event rows changed")
    require(set(manifest.get("implemented", [])) == {
        "placement_and_entity_tag",
        "six_equipment_slots_and_disabled_masks",
        "six_part_pose",
        "small_show_arms_base_plate_marker_no_gravity_invisible",
        "player_interaction_and_two_hit_break",
        "arrow_explosion_fire_lava_void_damage",
        "water_rain_fall_and_landing_events",
        "rideable_minecart_collision",
        "drops_sounds_particles",
        "direct_java_interaction_damage_and_drop_oracle",
        "native_checkpoint_and_java_save_continuation",
        "global_cross_store_attack_selector",
        "placement_collision_all_represented_entity_stores",
        "software_render_model_armor_held_items_and_leather_dye",
        "generic_saved_living_effect_minecart_passenger_name_glow_and_tag_state",
        "health_boost_attribute_expiry_and_health_clamp",
        "elytra_skull_types_0_to_5_and_arbitrary_head_item_render_layers",
    }, "Armor Stand implemented boundary is incomplete")
    require(set(manifest.get("remaining", [])) == {
        "profile_custom_head_texture_outside_the_skin_service_cut",
        "strict_same_scene_pixels_including_nameplate_and_glow_outline",
    }, "ENT-04 open boundary changed without updating its checked gate")

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    rows = [row for row in registry["entities"]
            if row["class"] == "EntityArmorStand"]
    require(len(rows) == 1 and rows[0]["status"] == "live_bounded"
            and rows[0]["todo"] == "ENT-04",
            "Armor Stand registry row must be truthful live_bounded")

    source_has(
        HERE / "stage_armor_stand_fixture.py",
        ('"ent04-armor-stand-save"', '"entity_nbt_roundtrip_locked"',
         '"negative_control"', '"EntityArmorStand"',
         '"equipment"', '"pose"'))
    source_has(
        HERE / "probe_armor_stand.py",
        ('"armor_stand_action_locked"', '"equip_two_helmets"',
         '"second_punch"', '"explosion_break"', '"in_fire_repeat"'))
    source_has(
        ROOT / "java" / "Minecraft" / "src" / "main" / "java"
        / "qrl" / "Recorder.java",
        ('"armor_stand".equalsIgnoreCase(type)',
         'net.minecraft.entity.item.EntityArmorStand',
         'value.addProperty("armor_stand_exact", exact)',
         '"entity_nbt_roundtrip_locked"',
         '"armor_stand_action_locked"'))
    source_has(
        ROOT / "magma" / "trace" / "state_capsule.py",
        ('"spawn_armor_stand_fixture"',
         '"set_armor_stand_living_state"',
         '"set_armor_stand_generic_state"',
         '"add_armor_stand_effect"',
         '"set_armor_stand_equipment"',
         '"set_armor_stand_pose"'))
    source_has(
        ROOT / "magma" / "game" / "test_armor_stand_runtime.c",
        ("placement_and_interaction", "entity_tag_placement",
         "placement_collision_matrix", "damage_drop_and_events",
         "live_tick_routes", "nearest_attack_ordering",
         "environment_tick", "generic_state_and_passenger",
         "expiring Health Boost", "checkpoint_continuation"))
    source_has(
        ROOT / "magma" / "game" / "item_render.c",
        ("ir_stand_head_matrix", "gm_held_blocks_emit",
         "IR_DISP_HEAD"))
    source_has(
        ROOT / "magma" / "game" / "entity_render.c",
        ("ARMOR_STAND_ELYTRA_PARTS", "er_armor_stand_skull_sprite",
         "er_emit_armor_stand_dragon_head"))
    source_has(
        ROOT / "magma" / "game" / "runtime.c",
        ("runtime_player_attack_target",
         "gm_mobs_any_intersects_aabb"))
    print("PASS Armor Stand: live-bounded placement/lifecycle, all-store "
          "collision/attack selection, rich NBT/effects/passenger state, "
          "14 real-Java interaction/damage rows, Java reload, and exact "
          "native continuation through tick 20; built-in render layers locked")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, ArmorStandError) as error:
        print(f"FAIL Armor Stand: {error}")
        raise SystemExit(1)
