#!/usr/bin/env python3
"""Lock the measured AI-04 live-bounded Snow Golem boundary."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "snowman_family_manifest.json"
REGISTRY = HERE / "registry_manifest.json"


class SnowmanFamilyError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SnowmanFamilyError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "netherite.snowman_family_gate"
            and manifest.get("version") == 1
            and manifest.get("todo") == "AI-04",
            "invalid Snow Golem family manifest identity")
    require(manifest.get("interaction_oracle") == {
        "java_version": "1.11.2",
        "shear_cases": 3,
        "pumpkin_tool_rng_and_sound": "bit_exact",
        "child_and_unbreaking_branches": "exact",
    }, "Snow Golem interaction evidence changed")
    require(manifest.get("ranged_oracle") == {
        "launch_cases": 3,
        "owned_impact_cases": 5,
        "position_motion_rotation": "bit_exact",
        "owner_rng_uuid_eid_and_sound": "bit_exact",
        "zero_and_blaze_damage_branches": "exact",
        "revenge_owner_identity": "exact",
    }, "Snow Golem ranged evidence changed")
    require(manifest.get("loot_oracle") == {
        "snowman_rows": 15,
        "aggregate_hostile_rows": 375,
        "world_rng_and_stack_count": "bit_exact",
    }, "Snow Golem loot evidence changed")
    require(manifest.get("state_continuation") == {
        "first_active_shot_tick": 21,
        "native_save_boundaries": [
            "pending_server_shear_packet",
            "snowball_in_flight",
        ],
        "interaction_and_projectile_state": "bit_exact",
    }, "Snow Golem continuation evidence changed")
    require(set(manifest.get("implemented", [])) == {
        "adult_snow_golem_live_type",
        "exact_dimensions_eye_height_and_base_attributes",
        "pumpkin_data_parameter_and_shear_state",
        "forge_shears_handled_noop_and_success_paths",
        "unbreaking_and_tool_damage_rng_order",
        "active_hostile_target_acquisition",
        "sixty_tick_unseen_target_memory",
        "ranged_stop_range_and_cooldown",
        "exact_snowball_launch_geometry",
        "snowball_owner_rng_uuid_and_entity_id",
        "owned_snowball_runtime_collision",
        "zero_damage_and_blaze_damage_impact",
        "nonplayer_revenge_owner_identity",
        "wet_and_hot_biome_damage",
        "mobgriefing_guarded_snow_trail",
        "zero_to_fifteen_snowball_loot",
        "ambient_hurt_death_and_shoot_audio",
        "five_box_snow_golem_model",
        "conditional_baked_pumpkin_layer",
        "jar_exact_skin_and_generated_pumpkin_atlas",
        "delayed_main_world_interaction_dispatch",
        "native_pending_interaction_continuation",
        "native_in_flight_projectile_continuation",
    }, "Snow Golem implemented boundary is incomplete")
    require(set(manifest.get("remaining", [])) == {
        "exact_snow_block_and_pumpkin_construction_spawn",
        "real_java_multitick_target_scheduler_path_wander_watch_and_look",
        "represented_hostile_retaliation_ai_consumes_nonplayer_revenge_target",
        "wet_hot_and_snow_trail_cross_stack_tick_oracle",
        "bounded_same_scene_pumpkin_and_bare_pixel_capture",
    }, "Snow Golem open boundary changed without updating its checked gate")

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    row = next(value for value in registry["entities"]
               if value["class"] == "EntitySnowman")
    require(row["status"] == "live_bounded" and row["todo"] == "AI-04",
            "Snow Golem registry row does not match the bounded boundary")

    source_has(ROOT / "magma" / "game" / "mob_live.c", (
        "gm_mobs_shear_snowman", "gm_mobs_snowman_attack_exact",
        "snowman_try_acquire_target", "snowman_target_unseen_ticks",
        "tick_snowman_living_tail", "snowman_snow_can_place",
        "GM_MOB_SOUND_SNOWMAN_SHOOT", "case EW_TYPE_SNOWMAN:"))
    source_has(ROOT / "magma" / "game" / "runtime.c", (
        "gm_mobs_take_snowman_shot", "gm_mobs_entity_throwable_hit",
        "target_type == EW_TYPE_SNOWMAN", "GM_SOUND_SNOWMAN_AMBIENT"))
    source_has(ROOT / "magma" / "game" / "test_snowman_runtime.c", (
        "Snow Golem waits the exact 20-tick first-shot interval",
        "native save records the in-flight Snow Golem snowball",
        "save/reload preserves exact owned-snowball continuation",
        "PASS Snow Golem runtime"))
    source_has(ROOT / "magma" / "trace" / "test_snowman_shearing.py", (
        "snowman_shear_locked", "Snow Golem shear cases"))
    source_has(ROOT / "magma" / "trace" / "test_snowman_ranged.py", (
        "snowman_ranged_locked", "Snow Golem ranged launches"))
    source_has(ROOT / "magma" / "trace" / "test_snowball_impact.py", (
        '"snowman_zombie_fresh_ground"', '"snowman_blaze_fresh"',
        "player and Snow Golem-owned snowball impacts"))
    source_has(ROOT / "magma" / "trace" / "test_hostile_loot.py", (
        '"snowman"', "exact hostile loot rows"))
    source_has(ROOT / "magma" / "game" / "entity_render.c", (
        "M_SNOWMAN", "GM_ENTITY_FLAG_SNOWMAN_PUMPKIN",
        "CR_MOB_SNOWMAN_PUMPKIN"))
    source_has(ROOT / "magma" / "assets" / "build_mob_atlas.py", (
        '("snowman", "snowman.png")',
        '("snowman_pumpkin", "@snowman_pumpkin")'))
    source_has(
        ROOT / "java" / "Minecraft" / "src" / "main" / "java"
        / "qrl" / "Recorder.java",
        ('case "snowman_shear_locked":', 'case "snowman_ranged_locked":',
         'action.get("thrower").getAsString().equals("snowman")'))
    print(
        "PASS Snow Golem family: live-bounded shearing, hostile targeting, "
        "ranged launch and owned impact, loot, native save continuation, "
        "audio, environment, and model/atlas paths are explicitly covered")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, StopIteration, ValueError,
            SnowmanFamilyError) as error:
        print(f"FAIL Snow Golem family: {error}")
        raise SystemExit(1)
