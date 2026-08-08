#!/usr/bin/env python3
"""Lock the measured AI-04 live-bounded Mooshroom boundary."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "mooshroom_family_manifest.json"
REGISTRY = HERE / "registry_manifest.json"


class MooshroomFamilyError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise MooshroomFamilyError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "netherite.mooshroom_family_gate"
            and manifest.get("version") == 1
            and manifest.get("todo") == "AI-04",
            "invalid Mooshroom-family manifest identity")
    require(manifest.get("interaction_oracle") == {
        "java_version": "1.11.2",
        "bowl_cases": 4,
        "shear_cases": 3,
        "mushroom_entities": 10,
        "inventory_rng_eid_and_item_state": "bit_exact",
        "cow_pose_health_and_age": "bit_exact",
        "sound_event": "bit_exact",
        "child_and_unbreaking_branches": "exact",
        "cow_milk_and_sheep_shear_regressions": "exact",
    }, "Mooshroom interaction evidence changed")
    require(manifest.get("state_continuation") == {
        "native_save_boundary": "pending_server_shear_packet",
        "conversion_rng_items_sound_and_particle": "bit_exact",
        "loaded_entity_replacement_order": "exact",
    }, "Mooshroom continuation evidence changed")
    require(manifest.get("render_oracle") == {
        "java_ab_states": 3,
        "java_ab_noise_pixels": 0,
        "adult_idle_owned_pixels": 10023,
        "adult_idle_hard_pixels": 41,
        "adult_idle_hard_budget": 41,
        "adult_head_owned_pixels": 10210,
        "adult_head_hard_pixels": 80,
        "adult_head_hard_budget": 80,
        "child_owned_pixels": 3220,
        "child_hard_pixels": 49,
        "child_hard_budget": 49,
        "negative_control": "exact",
    }, "Mooshroom render evidence changed")
    require(set(manifest.get("remaining", [])) == {
        "exact_natural_mushroom_island_pack_selection",
        "generic_living_custom_name_and_shear_copy",
        "fresh_replacement_cow_private_rng_injection",
        "multitick_transition_particle_pixel_tape",
    }, "Mooshroom open boundary changed without updating its checked gate")
    require(len(manifest.get("implemented", [])) == 19,
            "Mooshroom implemented boundary is incomplete")

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    row = next(value for value in registry["entities"]
               if value["class"] == "EntityMooshroom")
    require(row["status"] == "live_bounded" and row["todo"] == "AI-04",
            "Mooshroom registry row does not match the bounded boundary")

    source_has(ROOT / "magma/game/mob_live.c", (
        "gm_mobs_bowl_mooshroom", "gm_mobs_shear_mooshroom",
        "fresh-cow constructor consumes three Math.random",
        "GM_MOB_SOUND_MOOSHROOM_SHEAR", "EW_TYPE_MOOSHROOM"))
    source_has(ROOT / "magma/game/runtime.c", (
        "target_type == EW_TYPE_MOOSHROOM",
        "gm_mobs_shear_mooshroom", "gm_mobs_bowl_mooshroom",
        "GM_SOUND_MOOSHROOM_SHEAR"))
    source_has(ROOT / "magma/game/test_mooshroom_runtime.c", (
        "native save records a pending Mooshroom shear packet",
        "save/reload resumes exact conversion",
        "PASS Mooshroom runtime"))
    source_has(ROOT / "magma/trace/test_mooshroom_bowl.py", (
        "exact Mooshroom bowl", "mooshroom_bowl_locked"))
    source_has(ROOT / "magma/trace/test_mooshroom_shearing.py", (
        "Mooshroom shear cases", "mooshroom_shear_locked"))
    source_has(ROOT / "magma/game/item_render.c", (
        "gm_mooshroom_mushrooms_emit", "rk_facebakery_make_quad"))
    source_has(ROOT / "verify/ui_entities/measure_mooshroom_subject.py", (
        '"mooshroom_adult_idle": 41', '"mooshroom_child": 49',
        "Mooshroom hard-pixel mutation was accepted"))
    print(
        "PASS Mooshroom family: 7 exact Java interaction cases, 10 exact "
        "mushroom entities, save/reload conversion continuity, inherited "
        "cow behavior, and bounded adult/child same-scene pixels")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, StopIteration, ValueError,
            MooshroomFamilyError) as error:
        print(f"FAIL Mooshroom family: {error}")
        raise SystemExit(1)
