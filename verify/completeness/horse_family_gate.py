#!/usr/bin/env python3
"""Lock the checked ENT-02 live-bounded horse-family boundary."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "horse_family_manifest.json"
REGISTRY = HERE / "registry_manifest.json"


class HorseFamilyError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise HorseFamilyError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "netherite.horse_family_gate"
            and manifest.get("version") == 1
            and manifest.get("todo") == "ENT-02"
            and manifest.get("classification") == "live_bounded",
            "invalid horse-family manifest identity")
    case = manifest.get("case", {})
    classes = {
        "EntityHorse", "EntityDonkey", "EntityMule",
        "EntitySkeletonHorse", "EntityZombieHorse",
    }
    require(set(case.get("classes", [])) == classes,
            "the five Java horse classes are not all locked")
    require(case.get("horizons") == [0, 1, 2, 3, 4, 8, 20]
            and case.get("java_repeat") == "exact"
            and case.get("native") == "exact"
            and case.get("represented_t0") == "exact"
            and case.get("owner_uuid") == "exact_nonzero"
            and case.get("rejected_fields") == 0
            and case.get("state_ticks_compared") == 21
            and case.get("raw_horizons") == "exact",
            "checked Java/native horse continuation is not exact")
    breeding = manifest.get("breeding", {})
    rows = breeding.get("rows", [])
    require(breeding.get("parent_rng_seed48") == 0x23456789ABCD
            and breeding.get("birth_update") == 60
            and breeding.get("heart_particles") == 7
            and breeding.get("heart_runtime_pipeline") == {
                "payload_order": "bit_exact",
                "factory": "ParticleHeart_1_11_2",
                "lifecycle_and_render": "source_exact_native_gate",
                "constructor_state_replay": "exact_observed",
            }
            and breeding.get("id_order") == ["child", "xp"]
            and len(rows) == 4,
            "horse breeding boundary is not fully locked")
    require(breeding.get("full_world_tick") == {
        "oracle_command": "mate_animal_tick_locked",
        "cases": [
            "horse_horse_live_birth", "horse_donkey_live_birth",
            "donkey_horse_live_birth", "donkey_donkey_live_birth",
        ],
        "java_native": "exact", "first_update": "exact",
        "state": "exact", "events": "exact", "rng": "exact",
        "physics": "exact", "loaded_and_update_order": "exact",
    }, "four full-world Java/native horse births are not locked")
    require({tuple(row["parents"]) for row in rows} == {
        ("horse", "horse"), ("horse", "donkey"),
        ("donkey", "horse"), ("donkey", "donkey"),
    }, "all four legal horse/donkey parent orderings must remain measured")
    horse_row = next(row for row in rows
                     if row["parents"] == ["horse", "horse"])
    require(horse_row == {
        "parents": ["horse", "horse"], "child": "horse",
        "genetics_seed48": 160092359067293,
        "final_seed48": 156360562369922,
        "max_health_bits": "4038000000000000",
        "jump_strength_bits": "3fe66b729b7922eb",
        "movement_speed_bits": "3fcc6b881390fcb3",
        "variant": 258, "xp": 4,
    }, "horse/horse Java genetics row changed")
    for row in rows:
        if row is horse_row:
            continue
        require(row["genetics_seed48"] == 30191428589163
                and row["final_seed48"] == 14448659193736
                and row["max_health_bits"] == "4037aaaaaaaaaaab"
                and row["jump_strength_bits"] == "3fe5a0ca3a888378"
                and row["movement_speed_bits"] == "3fcf4013612bce21"
                and row["variant"] == 0 and row["xp"] == 6,
                f"{row['parents']} Java genetics row changed")
    require(next(row for row in rows
                 if row["parents"] == ["horse", "donkey"])["child"] == "mule"
            and next(row for row in rows
                     if row["parents"] == ["donkey", "horse"])["child"] == "mule"
            and next(row for row in rows
                     if row["parents"] == ["donkey", "donkey"])["child"] == "donkey",
            "horse-family child subtype matrix changed")
    taming = manifest.get("taming", {})
    tame_cases = {row["id"]: row for row in taming.get("cases", [])}
    require(taming.get("kinds") == ["horse", "donkey", "mule"]
            and set(tame_cases) == {
                "no_trigger", "temper_99_equality_failure",
                "temper_100_success",
            }, "horse taming oracle matrix is incomplete")
    require(taming.get("full_world_tick") == {
        "oracle_command": "horse_tame_tick_locked",
        "cases": [
            "horse_success", "donkey_success", "mule_success",
            "horse_failure", "donkey_temper_cap_failure", "mule_failure",
            "horse_no_trigger", "donkey_no_trigger", "mule_no_trigger",
        ],
        "java_native": "exact", "task_and_rng": "exact",
        "state_and_owner": "exact", "motion": "exact",
        "passenger_pose_and_dismount": "exact", "events": "exact",
    }, "nine full-world Java/native horse taming ticks are not locked")
    require(taming.get("client_status_particles") == {
        "statuses": [6, 7], "particles_each": 7, "ids": [11, 34],
        "positions": "bit_exact_java_oracle",
        "entity_rng_cursor": "bit_exact_java_oracle",
        "server_entity_rng_unchanged": True,
        "factory_lifecycle_and_render": "source_exact_native_gate",
        "constructor_state_replay": "exact_observed",
    }, "horse status 6/7 client particle expansion is not locked")
    require(tame_cases["no_trigger"] == {
        "id": "no_trigger", "temper": 0, "seed48": 1,
        "final_seed48": 25214903928, "ridden": True, "events": [],
    }, "horse taming non-trigger row changed")
    require(tame_cases["temper_99_equality_failure"] == {
        "id": "temper_99_equality_failure", "temper": 99,
        "seed48": 1000, "final_seed48": 23098218260332,
        "final_temper": 100, "ridden": False, "rearing": False,
        "angry_pitch_bits": "3f8aa0e1",
        "event_order": ["angry_sound", "status_6"],
    }, "horse taming equality-failure row changed")
    require(tame_cases["temper_100_success"] == {
        "id": "temper_100_success", "temper": 100, "seed48": 0,
        "final_seed48": 277363943098, "ridden": True,
        "owner_most": 5077202253465992214,
        "owner_least": -4639709590604183656,
        "events": ["status_7"],
    }, "horse taming success/owner row changed")
    trap = manifest.get("skeleton_trap", {})
    require(trap == {
        "fixture_horse_eid": 8399,
        "spawned_eids": [8400, 8401, 8402, 8403, 8404, 8405, 8406],
        "living_entities_after_tick": 8,
        "loaded_update_order": [
            8399, 8400, 8401, 8402, 8403, 8404, 8405, 8406,
        ],
        "entity_seed_generator_seed48": 224432795029324,
        "server_uuid_seed48": 278761879117434,
        "next_entity_id": 8407,
        "construction_rng_uuid_equipment": "exact",
        "same_tick_rng_gaussian_cache": "exact",
        "same_tick_position_motion": "translated_java_exact_5e-13",
        "ticks_compared": 120,
        "tick20_rng_cursors": "exact",
        "tick20_position_motion": "translated_java_exact_5e-13",
        "tick20_bow_strafe_transition": "exact",
        "tick120_rng_cursors": "exact",
        "tick120_positions": "translated_java_exact_1e-12",
        "tick120_motion": "translated_java_within_1e-6",
        "post20_target_revenge_bow_wander": "exact",
        "projectile_rows": 26,
        "projectile_eids": [8407, 8408, 8409, 8410],
        "projectile_state": "translated_java_exact_1e-12",
        "mount_edges_and_pose": "exact",
        "effect_only_lightning": "exact",
    }, "skeleton-trap 20-tick Java row changed")
    require(manifest.get("passenger_edges") == {
        "explicit_dismount_oracle_command": "horse_dismount_locked",
        "explicit_dismount_case_count": 54,
        "explicit_dismount_layouts": [
            "open", "first_blocked", "twice_blocked",
        ],
        "explicit_dismount_yaws": [
            -90, -45, 0, 30, 45, 89, 90, 135, 180,
        ],
        "explicit_dismount_primary_hands": ["right", "left"],
        "explicit_dismount_java_native": "bit_exact",
        "live_runtime_primary_hand": "configurable",
        "attach_and_vehicle_replacement": "source_exact",
        "mounted_pose_and_fall_damage": "source_exact",
    }, "horse passenger edge contract changed")
    require(manifest.get("particle_pixels") == {
        "gate": "verify/ui_entities/measure_horse_particles.py",
        "java_control_ab_diff_pixels": 0,
        "negative_control": "PASS",
        "cases": [
            {
                "id": "horse_taming_smoke", "particle_id": 11,
                "signal_pixels": 50, "java_ab_diff_pixels": 0,
                "ownership": "exact", "rgb": "exact",
            },
            {
                "id": "horse_breeding_heart", "particle_id": 34,
                "signal_pixels": 9270, "java_ab_diff_pixels": 0,
                "ownership": "exact", "rgb": "exact",
            },
        ],
    }, "horse status particle pixel evidence changed")
    require(manifest.get("horse_inventory_gui") == {
        "gameplay_gate": "magma/game/test_container_live.sh",
        "pixel_gate": "verify/ui_entities/measure_horse_gui.py",
        "java_ab_diff_pixels": 0,
        "ordinary_horse_hard_pixels": 0,
        "chested_donkey_hard_pixels": 22,
        "negative_control": "PASS",
        "status": "live_bounded_fixed_function_edges",
    }, "horse inventory GUI evidence changed")
    require(manifest.get("model_pixels") == {
        "gate": "verify/ui_entities/measure_horse_subject.py",
        "java_ab_diff_pixels": 0,
        "atomic_group_render_pins": 8,
        "skeleton_trap_rider_hard_pixels": 538,
        "skeleton_trap_group_hard_pixels": 3574,
        "skeleton_trap_group_common_hard_pixels": 1672,
        "negative_control": "PASS",
        "status": "live_bounded_fixed_function_edges",
    }, "horse world-model pixel evidence changed")
    require(set(manifest.get("remaining", [])) == {
        "skeleton_trap_pixels",
        "horse_inventory_fixed_function_edges",
        "model_fixed_function_edges",
    }, "ENT-02 partial boundary changed without updating its checked gate")

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    horse_rows = [row for row in registry["entities"]
                  if row["class"] in classes]
    require(len(horse_rows) == 5
            and all(row["status"] == "live_bounded"
                    and row["todo"] == "ENT-02" for row in horse_rows),
            "horse registry rows must remain truthful live_bounded entries")

    source_has(
        HERE / "stage_horse_fixture.py",
        ("ent02-horse-family-save", '"horizons": horizons',
         '"EntitySkeletonHorse"', '"negative_control"',
         '"owner_player": True', '"horse_owner_present"'))
    source_has(
        ROOT / "java" / "Minecraft" / "src" / "main" / "java"
        / "qrl" / "Recorder.java",
        ('"horse".equalsIgnoreCase(type)',
         'AbstractHorse.class',
         '"first_entity_genetics_seed48"',
         '"first_kind"',
         '"horse_tame_attempt_locked"',
         '"horse_tame_tick_locked"',
         '"horse_dismount_locked"',
         '"horse_owner_uuid_most"',
         'value.addProperty("horse_exact", exact)',
         'value.addProperty("horse_head_lean"'))
    source_has(
        ROOT / "magma" / "trace" / "test_sheep_mating_tick.py",
        ("horse_horse_live_birth", "horse_donkey_live_birth",
         "donkey_horse_live_birth", "donkey_donkey_live_birth",
         'request(args.port, "mate_animal_tick_locked", case)'))
    source_has(
        ROOT / "magma" / "trace" / "test_horse_tame_tick.py",
        ("horse_success", "donkey_temper_cap_failure", "mule_no_trigger",
         'request(args.port, "horse_tame_tick_locked", case)',
         'if java != magma:'))
    source_has(
        ROOT / "magma" / "trace" / "test_horse_dismount.py",
        ("YAWS = (-90.0, -45.0, 0.0, 30.0, 45.0, 89.0, 90.0, 135.0, 180.0)",
         'for left in (False, True)',
         '("open", "first_blocked", "twice_blocked")',
         'request(args.port, "horse_dismount_locked"',
         'if java != magma:'))
    source_has(
        ROOT / "magma" / "trace" / "state_capsule.py",
        ('"spawn_horse_fixture"', '"restore_horse_lifecycle"',
         '"restore_horse_inventory"', '"owner": "horse"'))
    source_has(
        ROOT / "magma" / "game" / "test_horse_runtime.c",
        ("exact_family_and_inventory", "feeding_semantics",
         "genetics_semantics", "mating_boundary_semantics",
         "automatic_crossbreed_scheduler",
         "explicit_dismount_edges",
         "taming_attempt_semantics", "automatic_taming_scheduler",
         "armor_and_death_inventory", "checkpoint_continuation",
         "skeleton_trap_construction_boundary",
         "skeleton_trap_live_scheduler",
         "trap_expiry_removes_loaded_order"))
    source_has(
        ROOT / "magma" / "game" / "particles_live.c",
        ("gm_particles_live_spawn_heart",
         "gm_particles_live_spawn_tame_effect",
         "GM_LIVE_PARTICLE_SMOKE_NORMAL",
         "GM_LIVE_PARTICLE_HEART"))
    source_has(
        ROOT / "magma" / "game" / "test_horse_particle_oracle.sh",
        ("horseParticleGolden", "cmp", "status 6/7 positions"))
    source_has(
        ROOT / "verify" / "ui_entities" / "measure_horse_particles.py",
        ("horse_taming_smoke", "horse_breeding_heart",
         "particle ownership differs", "pixel comparator accepted"))
    source_has(
        ROOT / "magma" / "game" / "test_container_live.c",
        ("test_horse_container", "untamed horse inventory is not usable",
         "donkey QUICK_MOVE merges storage before empties",
         "walking away closes horse UI"))
    source_has(
        ROOT / "verify" / "ui_entities" / "measure_horse_gui.py",
        ("gui_horse_armor", "gui_horse_donkey_chest",
         "mutation_selftest", "bounded raster edge residual locked"))
    source_has(
        ROOT / "verify" / "ui_entities" / "measure_horse_subject.py",
        ("skeleton_trap_rider", "skeleton_trap_group",
         "FIXED_FUNCTION_HARD_BUDGET", "FIXED_FUNCTION_NATIVE_BUDGET"))
    source_has(
        ROOT / "verify" / "ui_entities" / "capture_ui_entities_driver.py",
        ('state_id == "skeleton_trap_group"',
         'fa.get("render_pin_count") != 8', "render_pin_group=True"))
    print("PASS horse family: five live-bounded classes, "
          "save continuation exact through tick 20, four full-world Java "
          "breeding ticks, nine full-world taming ticks, exact status 6/7 "
          "client particles and exact isolated status pixels, a live bounded "
          "horse container, 54 exact explicit dismount cases, and the exact "
          "skeleton-trap construction and 120-tick combat/projectile group "
          "locked")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, HorseFamilyError) as error:
        print(f"FAIL horse family: {error}")
        raise SystemExit(1)
