#!/usr/bin/env python3
"""Lock the measured AI-01 live-bounded Endermite boundary."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "endermite_family_manifest.json"
REGISTRY = HERE / "registry_manifest.json"


class EndermiteFamilyError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise EndermiteFamilyError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "netherite.endermite_family_gate"
            and manifest.get("version") == 1
            and manifest.get("todo") == "AI-01",
            "invalid Endermite family manifest identity")
    require(manifest.get("pearl_spawn_oracle") == {
        "impact_cases": 5,
        "spawn_cases": 1,
        "spawn_roll_rng_uuid_eid_and_state": "bit_exact",
    }, "Endermite pearl-spawn evidence changed")
    require(manifest.get("death_loot_oracle") == {
        "composed_endermite_death_rows": 9,
        "aggregate_death_rows": 261,
        "endermite_loot_rows": 15,
        "aggregate_hostile_loot_rows": 375,
        "empty_table_and_rng": "bit_exact",
        "terminal_xp": 3,
    }, "Endermite death/loot evidence changed")
    require(manifest.get("state_continuation") == {
        "java_native_ticks": 20,
        "shared_plain_entity_classes": 34,
        "native_save_boundary": 2399,
        "expiry_tick": 2400,
        "lifetime_player_spawned_and_persistence": "bit_exact",
    }, "Endermite continuation evidence changed")
    require(manifest.get("render_contract") == {
        "model_boxes": 4,
        "texture": "jar_exact",
        "animation": "exact_formula",
        "death_rotation_degrees": 180,
    }, "Endermite render evidence changed")
    require(set(manifest.get("implemented", [])) == {
        "live_endermite_type",
        "exact_dimensions_eye_height_health_and_arthropod_classification",
        "ender_pearl_five_percent_spawn_branch",
        "player_spawned_lifetime_and_persistence_nbt",
        "exact_lifetime_2400_retirement",
        "active_generic_hostile_target_melee_and_despawn_path",
        "exact_empty_loot_table",
        "exact_three_xp_terminal_death",
        "hostile_fall_damage_and_audio_path",
        "ambient_hurt_death_and_step_resources",
        "four_box_animated_model",
        "jar_exact_skin_and_atlas",
        "native_save_continuation",
        "java_nbt_capsule_native_continuation",
    }, "Endermite implemented boundary is incomplete")
    require(set(manifest.get("remaining", [])) == {
        "real_java_multitick_active_target_melee_swim_wander_watch_and_look",
        "natural_spawn_nearest_player_exclusion_and_pack_order",
        "enderman_targets_player_spawned_endermite_integration",
        "type_specific_step_event_from_live_locomotion",
        "client_portal_particle_rng_lifecycle_and_pixels",
        "bounded_same_scene_java_native_model_pixels",
    }, "Endermite open boundary changed without updating its checked gate")

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    row = next(value for value in registry["entities"]
               if value["class"] == "EntityEndermite")
    require(row["status"] == "live_bounded" and row["todo"] == "AI-01",
            "Endermite registry row does not match the bounded boundary")

    source_has(ROOT / "magma" / "game" / "mob_live.c", (
        "++m->endermite_lifetime[i]", "m->endermite_lifetime[i] >= 2400",
        "s->type[slot] == EW_TYPE_ENDERMITE ? 3",
        "GM_MOB_SOUND_ENDERMITE_AMBIENT", "case EW_TYPE_ENDERMITE:"))
    source_has(ROOT / "magma" / "game" / "runtime.c", (
        "runtime_spawn_pearl_endermite",
        "endermite_player_spawned[slot] = 1",
        "GM_SOUND_ENDERMITE_AMBIENT"))
    source_has(ROOT / "magma" / "game" / "test_endermite_runtime.c", (
        "tick-2399 Endermite boundary",
        "retires exactly at lifetime 2400",
        "save/reload preserves exact lifetime-2400 retirement",
        "PASS Endermite runtime"))
    source_has(ROOT / "magma" / "game" / "test_hostile_death_live.c", (
        "{EW_TYPE_ENDERMITE, 1}", "type == EW_TYPE_ENDERMITE ? three_xp",
        "attacks=25 terminal=25"))
    source_has(ROOT / "magma" / "game" / "test_mob_live.c", (
        "EW_TYPE_PIGMAN, EW_TYPE_SILVERFISH,",
        "GM_MOB_SOUND_SILVERFISH_HURT, GM_MOB_SOUND_ENDERMITE_HURT",
        '"endermite",'))
    source_has(ROOT / "magma" / "game" / "test_audio_live.c", (
        "GM_SOUND_ENDERMITE_AMBIENT", "GM_SOUND_ENDERMITE_HURT",
        "GM_SOUND_ENDERMITE_DEATH", "GM_SOUND_ENDERMITE_STEP"))
    source_has(ROOT / "magma" / "trace" / "test_ender_pearl_impact.py", (
        '("endermite", 5', "exact Ender Pearl impacts"))
    source_has(ROOT / "magma" / "trace" / "test_hostile_loot.py", (
        '("endermite", 1)', "exact hostile loot rows"))
    source_has(ROOT / "magma" / "trace" / "test_hostile_player_death.py", (
        '("endermite", 1)', "exact composed living deaths"))
    source_has(ROOT / "magma" / "trace" / "test_no_ai_mob_capsule.py", (
        '"endermite_lifetime": 237', '"endermite_player_spawned": True',
        '"endermite_persistence_required": False'))
    source_has(ROOT / "magma" / "game" / "entity_render.c", (
        "M_ENDERMITE", '"EntityEndermite"',
        "v->type == ER_TYPE_ENDERMITE ? ER_PI"))
    source_has(ROOT / "magma" / "assets" / "build_mob_atlas.py", (
        '("endermite", "endermite.png")',))
    source_has(
        ROOT / "java" / "Minecraft" / "src" / "main" / "java"
        / "qrl" / "Recorder.java",
        ('name.equals("endermite")',
         "endermite.setSpawnedByPlayer(",
         '"endermite_lifetime", lifetime)'))
    print(
        "PASS Endermite family: live-bounded pearl spawn, lifetime, NBT and "
        "native save continuation, death/loot/XP, fall, audio, and animated "
        "model paths are explicitly covered")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, StopIteration, ValueError,
            EndermiteFamilyError) as error:
        print(f"FAIL Endermite family: {error}")
        raise SystemExit(1)
