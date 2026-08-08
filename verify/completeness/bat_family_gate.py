#!/usr/bin/env python3
"""Lock the measured AI-04 live-bounded Bat boundary."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "bat_family_manifest.json"
REGISTRY = HERE / "registry_manifest.json"


class BatFamilyError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise BatFamilyError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "netherite.bat_family_gate"
            and manifest.get("version") == 1
            and manifest.get("todo") == "AI-04",
            "invalid Bat-family manifest identity")
    require(manifest.get("active_ai_oracle") == {
        "java_version": "1.11.2",
        "scenarios": 11,
        "ticks": 101,
        "position_motion_and_aabb": "bit_exact",
        "rotation_and_body_helper": "bit_exact",
        "entity_rng_cursor": "exact",
        "hanging_ceiling_and_takeoff_event": "exact",
        "survival_and_creative_player_wake": "exact",
        "flight_retarget_and_invalid_target": "exact",
        "age_reset_soft_and_hard_despawn": "exact",
        "persistence_override": "exact",
    }, "Bat active-AI evidence changed")
    require(manifest.get("state_continuation") == {
        "native_checkpoint_ticks": 12,
        "native_checkpoint_after_tick": 6,
        "java_capsule_warmup_ticks": 9,
        "java_native_continuation_ticks": 16,
        "private_flight_state_and_rng": "bit_exact",
        "loaded_and_mob_update_order": "exact",
    }, "Bat continuation evidence changed")
    require(manifest.get("render_oracle") == {
        "java_ab_states": 3,
        "java_ab_noise_pixels": 0,
        "flying_owned_pixels": 1064,
        "flying_hard_pixels": 0,
        "flying_max_channel_delta": 1,
        "hanging_owned_pixels": 852,
        "hanging_hard_pixels": 1,
        "hanging_hard_budget": 1,
        "hanging_max_channel_delta": 33,
        "ownership_xor_pixels": 0,
        "negative_control": "exact",
    }, "Bat render evidence changed")
    require(set(manifest.get("implemented", [])) == {
        "active_hanging_and_flight_state_machine",
        "normal_cube_ceiling_support",
        "survival_player_wake_and_creative_exclusion",
        "takeoff_world_event_1025",
        "private_spawn_position_validation_and_retarget",
        "java_random_call_order",
        "flight_motion_yaw_look_and_body_helpers",
        "hanging_anchor_and_flight_vertical_damping",
        "no_push_fall_walking_or_pressure_contact",
        "damage_wakes_before_hurt_rejection",
        "ambient_hurt_death_audio",
        "entity_age_and_persistence",
        "soft_despawn_rng_and_distance_threshold",
        "hard_despawn_distance_threshold",
        "native_checkpoint_continuation",
        "real_java_state_capsule_continuation",
        "live_model_and_animation",
        "live_render_hanging_bit_and_client_age",
        "exact_modelrenderer_parent_child_hierarchy",
        "flying_and_hanging_render_poses",
        "bounded_same_scene_model_pixels",
    }, "Bat implemented boundary is incomplete")
    require(set(manifest.get("remaining", [])) == {
        "exact_world_entity_spawner_pack_and_spawn_eligibility",
    }, "Bat open boundary changed without updating its checked gate")

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    row = next(value for value in registry["entities"]
               if value["class"] == "EntityBat")
    require(row["status"] == "live_bounded" and row["todo"] == "AI-04",
            "Bat registry row does not match the bounded boundary")

    source_has(
        ROOT / "java" / "Minecraft" / "src" / "main" / "java"
        / "qrl" / "Recorder.java",
        ('value.addProperty("bat_active_exact"',
         '"bat_entity_age", entityAge',
         'action.has("bat_spawn_valid")',
         'oracleDeclaredField(bat, "spawnPosition")'))
    source_has(
        ROOT / "magma" / "game" / "mob_live.c",
        ("bat_update_ai_tasks", "GM_MOB_SOUND_BAT_AMBIENT",
         "bat_despawn_entity",
         "EntityBat wakes before EntityLivingBase can reject",
         "m->player_creative", "type == EW_TYPE_BAT",
         "GM_ENTITY_FLAG_BAT_HANGING"))
    source_has(
        ROOT / "magma" / "game" / "entity_render.c",
        ("emit_bat", "g_bat_wing_x", "exact ModelRenderer parent-child",
         "RenderLivingBase disables culling around ModelBat"))
    source_has(
        ROOT / "magma" / "game" / "test_bat_runtime.c",
        ('"creative_hang"', "world_event_count", "gm_native_save_write",
         "live Bat view carries client age and hanging render bit",
         "private-RNG/checkpoint"))
    source_has(
        ROOT / "magma" / "trace" / "test_bat_ai.py",
        ('("creative_hang", 12)', 'gamemode creative @p',
         '("persistent_far", 1)', "PASS active Bat Java/native",
         "canonical_java"))
    source_has(
        ROOT / "magma" / "trace" / "test_bat_capsule.py",
        ("WARMUP_TICKS = 9", "CONTINUATION_TICKS = 16",
         "restore_bat_ai_state", "compare_bat"))
    source_has(
        ROOT / "magma" / "trace" / "state_capsule.py",
        ('entity_type == "EntityBat"',
         '"type": "restore_bat_ai_state"',
         '"type": "set_mob_no_ai"'))
    source_has(
        ROOT / "verify" / "ui_entities" / "measure_bat_subject.py",
        ("HARD_BUDGET", '"bat_hanging": 1',
         "Bat hard-pixel mutation was accepted"))
    print(
        "PASS Bat family: live-bounded active AI and lifetime have 101 "
        "bit-exact real-Java ticks across eleven fixtures, a 12-tick native "
        "checkpoint, 16 exact ticks after a warmed Java state capsule, and "
        "bounded flying/hanging same-scene pixels")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, StopIteration, ValueError,
            BatFamilyError) as error:
        print(f"FAIL Bat family: {error}")
        raise SystemExit(1)
