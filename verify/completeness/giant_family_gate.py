#!/usr/bin/env python3
"""Lock the measured AI-01 live-bounded Giant boundary."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "giant_family_manifest.json"
REGISTRY = HERE / "registry_manifest.json"


class GiantFamilyError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise GiantFamilyError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "netherite.giant_family_gate"
            and manifest.get("version") == 1
            and manifest.get("todo") == "AI-01",
            "invalid Giant family manifest identity")
    require(manifest.get("java_contract") == {
        "width": 3.6,
        "height": 11.7,
        "eye_height": 10.440001,
        "max_health": 100.0,
        "movement_speed": 0.5,
        "attack_damage": 50.0,
        "installed_tasks": 0,
        "installed_target_tasks": 0,
    }, "Giant Java contract changed")
    require(manifest.get("death_loot_oracle") == {
        "giant_death_rows": 9,
        "aggregate_death_rows": 261,
        "giant_loot_rows": 15,
        "aggregate_hostile_loot_rows": 375,
        "empty_table_and_rng": "bit_exact",
        "terminal_xp": 5,
    }, "Giant death/loot evidence changed")
    require(manifest.get("state_continuation") == {
        "java_native_no_ai_ticks": 20,
        "shared_plain_entity_classes": 34,
        "native_active_ticks": 40,
        "save_boundary_tick": 20,
        "goal_less_state": "exact",
    }, "Giant continuation evidence changed")
    require(manifest.get("render_contract") == {
        "model": "zombie",
        "uniform_scale": 6.0,
        "texture": "zombie_jar_exact",
        "eye_height": 10.440001,
    }, "Giant render contract changed")
    require(set(manifest.get("implemented", [])) == {
        "live_giant_type",
        "exact_dimensions_eye_height_and_attributes",
        "zero_task_and_zero_target_task_active_behavior",
        "shared_living_gravity_collision_despawn_and_damage",
        "exact_empty_loot_table",
        "exact_five_xp_terminal_death",
        "java_nbt_capsule_native_continuation",
        "native_active_save_continuation",
        "zombie_model_at_uniform_scale_six",
        "jar_exact_zombie_skin",
    }, "Giant implemented boundary is incomplete")
    require(set(manifest.get("remaining", [])) == {
        "arbitrary_multiplayer_despawn_and_loaded_entity_order",
        "full_environment_damage_matrix",
        "command_spawner_and_cross_dimension_integration",
        "bounded_same_scene_java_native_model_pixels",
    }, "Giant open boundary changed without updating its checked gate")

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    row = next(value for value in registry["entities"]
               if value["class"] == "EntityGiantZombie")
    require(row["status"] == "live_bounded" and row["todo"] == "AI-01",
            "Giant registry row does not match the bounded boundary")

    oracle_source = (ROOT / "java" / "oracle-src" / "net" / "minecraft"
                     / "entity" / "monster" / "EntityGiantZombie.java")
    source = oracle_source.read_text(encoding="utf-8")
    source_has(oracle_source, (
        "this.setSize(this.width * 6.0F, this.height * 6.0F);",
        "return 10.440001F;", "MAX_HEALTH).setBaseValue(100.0D)",
        "MOVEMENT_SPEED).setBaseValue(0.5D)",
        "ATTACK_DAMAGE).setBaseValue(50.0D)",
        "return LootTableList.ENTITIES_GIANT;"))
    require("initEntityAI" not in source,
            "Java Giant unexpectedly installs an AI task list")
    source_has(ROOT / "magma" / "game" / "mob_live.c", (
        "if(type==EW_TYPE_GIANT)wants=0;",
        "EntityGiantZombie never installs tasks or targetTasks",
        "case EW_TYPE_GIANT:"))
    source_has(ROOT / "magma" / "game" / "test_giant_runtime.c", (
        "goal-less Giant neither targets, navigates, wanders, nor attacks",
        "save/reload preserves exact active Giant continuation",
        "PASS Giant runtime"))
    source_has(ROOT / "magma" / "trace" / "test_hostile_loot.py", (
        '("giant", 1)', "exact hostile loot rows"))
    source_has(ROOT / "magma" / "trace" / "test_hostile_player_death.py", (
        '("giant", 1)', "exact composed living deaths"))
    source_has(ROOT / "magma" / "trace" / "test_no_ai_mob_capsule.py", (
        '(65, "EntityGiantZombie")',))
    source_has(ROOT / "magma" / "game" / "entity_render.c", (
        '"EntityGiantZombie"', "if (t == ER_TYPE_GIANT) sc *= 6.0f;",
        "case ER_TYPE_GIANT:    return 10.440001f;"))
    source_has(ROOT / "magma" / "game" / "test_entity_render.c", (
        '65, "giant"',
        "Giant reuses the zombie model at exact uniform scale 6"))
    print(
        "PASS Giant family: live-bounded goal-less active behavior, exact "
        "attributes, death/loot/XP, continuation, and scale-six render path")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, StopIteration, ValueError,
            GiantFamilyError) as error:
        print(f"FAIL Giant family: {error}")
        raise SystemExit(1)
