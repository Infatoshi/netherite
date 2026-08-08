#!/usr/bin/env python3
"""Lock the measured ENT-03 live-bounded llama/spit boundary."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "llama_family_manifest.json"
REGISTRY = HERE / "registry_manifest.json"


class LlamaFamilyError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise LlamaFamilyError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "netherite.llama_family_gate"
            and manifest.get("version") == 1
            and manifest.get("todo") == "ENT-03"
            and manifest.get("classification") == "live_bounded",
            "invalid llama-family manifest identity")
    require(manifest.get("state_continuation") == {
        "ticks": 4, "java_native": "bit_exact", "llamas": 3,
        "spits": 1, "item_targets": 1,
        "leash_break_drop": "exact",
        "reciprocal_caravan_graph": "exact",
        "loaded_and_mob_update_order": "exact",
    }, "llama Java/native continuation evidence changed")
    require(manifest.get("genetics") == {
        "java_c_rows": 64,
        "health_jump_speed": "bit_exact",
        "strength_variant": "exact",
    }, "llama Java/C genetics oracle changed")
    require(manifest.get("loot") == {
        "java_c_rows": 96, "looting_levels": [0, 1, 2, 3],
        "leather_and_rng_cursor": "exact",
    }, "llama Java/C loot oracle changed")
    require(manifest.get("caravan_task") == {
        "real_java_native_rows": 16,
        "private_clock_rows": 5,
        "in_range_grace_aging": "exact",
        "acceleration_and_expiry": "exact",
        "reset_counter_retention": "exact",
        "terrain_obstacle_ticks": 20,
        "terrain_motion": "bit_exact",
        "materialized_paths": "exact",
        "java_current_path_null_ticks": [1],
        "dynamic_obstacle_ticks": 8,
        "distant_collision_edit_path": "unchanged",
        "near_wall_open_replan": "exact",
        "dynamic_motion": "bit_exact",
        "task_conflict_rows": 4,
        "task_conflicts": [
            "caravan_suppresses_ranged",
            "caravan_suppresses_mate",
            "swim_concurrent",
            "follow_parent_concurrent",
        ],
        "follow_parent_ticks": 16,
        "follow_parent_path_and_motion": "bit_exact",
        "follow_parent_rng_cursor": "exact",
        "lower_task_rows": 3,
        "wander_watch_idle_rng_and_motion": "bit_exact",
        "panic_scenarios": 2,
        "panic_ticks": 24,
        "panic_water_and_random_path_motion_rng": "bit_exact",
        "mating_scenarios": 1,
        "mating_ticks": 16,
        "mating_path_motion_rng": "bit_exact",
        "ranged_scenarios": 1,
        "ranged_ticks": 41,
        "ranged_path_clocks_motion_rng": "bit_exact",
        "ranged_wolf_target_retention": "exact",
        "real_java_oracle": "magma/trace/test_llama_caravan.py",
    }, "llama caravan task evidence changed")
    require(manifest.get("spit") == {
        "launch_particles": 7, "particle_id": 48,
        "client_factory": "ParticleExplosion_1_11_2",
        "motion_gravity_water_and_block_ray": "source_exact",
        "nearest_and_loaded_order_ties": "exact",
        "native_target_kinds": 19,
        "real_java_target_classes": 11,
        "real_java_oracle": "magma/trace/test_llama_spit_targets.py",
    }, "llama-spit lifecycle/collision evidence changed")
    require(manifest.get("inventory_gui") == {
        "java_ab_diff_pixels": 0, "native_diff_pixels": 622,
        "native_over_1_pixels": 4, "native_hard_pixels": 0,
        "native_max_channel": 10, "negative_control": "PASS",
    }, "llama inventory GUI evidence changed")
    require(manifest.get("model_pixels") == {
        "subject_states": 24, "java_ab_diff_pixels": 0,
        "hard_threshold": 25, "maximum_hard_pixels_per_state": 4,
        "spit_hard_pixels": 0, "negative_control": "PASS",
        "status": "live_bounded_fixed_function_edges",
    }, "llama subject pixel evidence changed")
    require(set(manifest.get("implemented", [])) == {
        "taming_feeding_and_mounting",
        "reciprocal_caravan_and_leash_lifecycle",
        "chest_decor_strength_inventory_and_container",
        "breeding_genetics_and_birth",
        "damage_death_inventory_and_loot",
        "audio_and_client_particles",
        "spit_motion_collision_damage_and_persistence",
        "ordinary_ranged_and_wolf_defense_ai",
        "real_java_caravan_private_task_boundaries",
        "real_java_terrain_caravan_path_and_motion",
        "real_java_dynamic_obstacle_replanning",
        "real_java_task_priority_conflicts",
        "real_java_follow_parent_path_motion_and_rng",
        "real_java_wander_watch_idle_task_rng",
        "real_java_panic_water_random_path_motion_and_rng",
        "real_java_mating_path_motion_and_rng",
        "real_java_ranged_path_clocks_motion_and_rng",
        "models_four_coats_sixteen_decor_chest_child_gait_and_spit",
        "real_java_capsule_native_continuation",
        "real_java_inventory_and_subject_pixel_gates",
    }, "llama implemented boundary is incomplete")
    require(set(manifest.get("remaining", [])) == {
        "foreign_target_terminal_state_owned_by_ent05_ent06_ai04",
        "llama_world_model_fixed_function_edges",
    }, "ENT-03 open boundary changed without updating its checked gate")

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    rows = [row for row in registry["entities"]
            if row["class"] in {"EntityLlama", "EntityLlamaSpit"}]
    require(len(rows) == 2
            and all(row["status"] == "live_bounded"
                    and row["todo"] == "ENT-03" for row in rows),
            "llama registry rows must remain truthful live_bounded entries")

    source_has(
        ROOT / "magma" / "trace" / "test_llama_capsule.py",
        ("TICKS = 4", '"llama_spit"',
         "llama player leash, reciprocal ", "caravan graph/task clocks",
         "loaded order mismatch"))
    source_has(
        ROOT / "magma" / "trace" / "test_llama_caravan.py",
        ('"inside"', '"far_accel"', '"far_expired"',
         '"reset_preserve"', "TERRAIN_TICKS = 20",
         "DYNAMIC_TICKS = 8", "FOLLOW_PARENT_TICKS = 16",
         "PANIC_TICKS = 12", "MATE_TICKS = 16", "RANGED_TICKS = 41",
         "native_lower_task_rows", "llama lower-task Java/native mismatch",
         "native_panic_rows", "native_mate_rows", "native_ranged_rows",
         "distant collision edit",
         "near wall opening", "Java/native navigation mismatch",
         "Java/native motion mismatch"))
    source_has(
        ROOT / "magma" / "trace" / "test_llama_spit_targets.py",
        ("passive_cases", '"small_fireball"', '"wither_skull"',
         "Wither llama-source damage mismatch",
         "llama spit did not destroy End crystal"))
    source_has(
        ROOT / "magma" / "game" / "test_llama_runtime.c",
        ("genetics_and_mating", "runtime_wolf_defense_ai",
         "llama_lower_task_oracle", '"caravan_follow_parent"',
         "llama_panic_terrain_oracle", "llama_mate_terrain_oracle",
         "llama_ranged_terrain_oracle",
         "projectile_material_boundaries",
         "represented_entity_collision_tail",
         "small fireball override rejects indirect projectile damage"))
    source_has(
        ROOT / "magma" / "game" / "mob_live.c",
        ("llama_follow_parent_tick", "llama_nav_goal_pending",
         "animal_random_position_exact", "GM_LLAMA_TASK_FOLLOW_PARENT"))
    source_has(
        ROOT / "magma" / "game" / "runtime.c",
        ("runtime_llama_spit_entity_target",
         "runtime_llama_spit_destroy_end_crystal",
         "GM_SPIT_TARGET_SHULKER_BULLET",
         "EntityLlamaSpit deliberately does not call canBeCollidedWith"))
    source_has(
        ROOT / "magma" / "game" / "particles_live.c",
        ("GM_LIVE_PARTICLE_SPIT", "gm_particles_live_spawn_spit"))
    source_has(
        ROOT / "java" / "Minecraft" / "src" / "main" / "java"
        / "qrl" / "Recorder.java",
        ('"llama_spit".equalsIgnoreCase(type)',
         'value.addProperty("llama_spit_exact", exact)',
         'value.addProperty("llama_entity_seed48"',
         'action.has("fire_ticks")',
         'float yaw = action.has("yaw")'))
    source_has(
        ROOT / "verify" / "ui_entities" / "measure_llama_subject.py",
        ("llama_decor_black", "llama_gray_decor_chest",
         "llama_child_decor", "llama_spit",
         "FIXED_FUNCTION_HARD_BUDGET"))
    source_has(
        ROOT / "verify" / "ui_entities" / "measure_horse_gui.py",
        ('"gui_horse_llama_chest"', "bounded raster edge residual locked"))
    print("PASS llama family: live-bounded llama/spit, 64 genetics and 96 "
          "loot Java/C rows, exact four-tick Java/native continuation, "
          "sixteen real-Java AI boundary fixtures, exact 20-tick obstacle "
          "motion and materialized paths, exact distant/near dynamic "
          "block-update behavior, exact 16-tick follow-parent navigation "
          "and RNG, accepted wander/watch/idle RNG and motion, exact 24-tick "
          "water/random panic, 16-tick mating navigation, and 41-tick ranged "
          "path/clock/motion/RNG continuation, represented-store "
          "collision dispatch, and bounded UI/model pixel evidence locked")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, LlamaFamilyError) as error:
        print(f"FAIL llama family: {error}")
        raise SystemExit(1)
