#!/usr/bin/env python3
"""Lock the measured ENT-05 live-bounded hanging-entity boundary."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "hanging_family_manifest.json"
REGISTRY = HERE / "registry_manifest.json"


class HangingFamilyError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise HangingFamilyError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "netherite.hanging_family_gate"
            and manifest.get("version") == 1
            and manifest.get("todo") == "ENT-05",
            "invalid hanging-family manifest identity")
    require(manifest.get("callback_oracle") == {
        "rows": 227,
        "java_version": "1.11.2",
        "pose_and_aabb": "bit_exact",
        "painting_support_clock": "post_increment_100_exact",
        "painting_drop_callback": "exact",
        "painting_constructor_rng": "all_26_art_and_6_support_shapes_exact",
        "knot_llama_terminal_state": "exact",
        "item_frame_pose_and_aabb": "bit_exact",
        "item_frame_support_clock": "post_increment_100_exact",
        "item_frame_damage_interaction_and_rng": "exact",
        "mixed_store_loaded_update_order": "all_6_permutations_exact",
        "damage_source_equivalence_classes":
            "4_entities_x_8_sources_x_2_creative_exact",
        "item_frame_map_decoration":
            "32_scale_facing_boundary_lifecycle_rows_exact",
        "living_leash_matrix":
            "16_classes_x_4_core_plus_7_edges_exact",
    }, "real-Java callback evidence changed")
    require(manifest.get("save_continuation") == {
        "format": "anvil-1.11.2",
        "cold_reload": "real_java",
        "painting_art_pose_uuid": "retained",
        "painting_support_clock": "constructor_reset_zero",
        "item_frame_stack_nbt_rotation_chance_uuid": "retained",
        "item_frame_map_colors":
            "exact_16384_byte_plane_and_frame_marker",
        "item_frame_support_clock": "constructor_reset_zero",
        "leash_knot_persisted": False,
        "llama_fence_coordinates": "retained_pending_nbt",
        "knot_recreated_tick": 1,
        "java_native_ticks": 3,
        "item_frame_painting_and_knot_state": "exact",
        "llama_leash_identity": "exact",
        "living_leash_capsule_graph":
            "all_holder_kinds_and_wolf_angry",
    }, "real-Anvil hanging continuation evidence changed")
    require(set(manifest.get("implemented", [])) == {
        "cold_growable_item_frame_painting_and_leash_knot_stores",
        "all_26_painting_art_dimensions_and_horizontal_geometry",
        "painting_and_knot_aabb",
        "surface_and_hanging_overlap_validation",
        "periodic_post_increment_support_clock",
        "painting_item_placement_selection_and_survival_consumption",
        "painting_player_break_drop_and_sounds",
        "painting_constructor_fitting_set_and_rng_corpus",
        "item_frame_arbitrary_stack_nbt_rotation_and_comparator_output",
        "item_frame_placement_interaction_two_stage_damage_and_sounds",
        "item_frame_projectile_explosion_creative_and_support_damage",
        "item_frame_entity_rng_uuid_and_relative_loaded_order",
        "mixed_item_frame_painting_and_knot_loaded_update_order",
        "all_hanging_damage_source_equivalence_classes_and_creative_edges",
        "item_frame_map_decoration_quantization_tracker_and_cleanup",
        "llama_player_lead_transfer_to_fence_knot",
        "knot_interaction_break_and_llama_lead_terminal_state",
        "player_raycast_attack_and_knot_interaction",
        "uuid_and_loaded_entity_order_membership",
        "native_checkpoint_round_trip",
        "authoritative_java_export_and_trace_comparison",
        "state_capsule_restore",
        "real_anvil_painting_reload",
        "real_anvil_item_frame_reload_and_three_tick_capsule_continuation",
        "real_anvil_pending_leash_nbt_and_first_tick_knot_reconstruction",
        "all_16_vanilla_leashable_living_classes",
        "living_leash_player_fence_pull_break_and_class_specific_edges",
        "class_neutral_leash_authoritative_export_and_capsule_restore",
        "painting_all_art_mesh_texture_and_per_tile_light",
        "item_frame_baked_block_generated_item_and_dynamic_map_rendering",
        "leash_knot_model_and_flat_provoking_color_living_leash_strips",
        "map_color_plane_authoritative_export_capsule_and_checkpoint_restore",
        "strict_same_scene_hanging_pixel_gate_with_mutation_control",
    }, "hanging-family implemented boundary is incomplete")
    require(set(manifest.get("remaining", [])) == {
        "strict_fixed_function_edge_tail_23_pixels",
    }, "ENT-05 open boundary changed without updating its checked gate")
    require(manifest.get("pixel_oracle") == {
        "java_ab_noise": 0,
        "max_channel_threshold": 25,
        "painting_kebab": 0,
        "painting_pointer": 3,
        "item_frame_empty": 0,
        "item_frame_stick": 1,
        "item_frame_dirt": 17,
        "item_frame_map": 0,
        "leash_knot": 0,
        "leashed_llama": 2,
    }, "hanging-family pixel evidence changed")

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    rows = {row["class"]: row for row in registry["entities"]
            if row["class"] in {
                "EntityLeashKnot", "EntityPainting", "EntityItemFrame"}}
    require(rows["EntityLeashKnot"]["status"] == "live_bounded"
            and rows["EntityPainting"]["status"] == "live_bounded"
            and rows["EntityItemFrame"]["status"] == "live_bounded"
            and all(row["todo"] == "ENT-05" for row in rows.values()),
            "hanging registry rows do not match the bounded boundary")

    source_has(
        ROOT / "java" / "Minecraft" / "src" / "main" / "java"
        / "qrl" / "Recorder.java",
        ('"hanging_entity_locked"', 'EntityPainting.EnumArt.values()',
         'EntityItemFrame.class', '"itemDropChance"',
         '.getKnotForPosition(world, fence)',
         '"llama_leash_pending", llamaLeashPending',
         'authoritative.add("paintings"',
         'authoritative.add("leash_knots"',
         'authoritative.add("living_leashes"', '"map_colors_b64"'))
    source_has(
        ROOT / "magma" / "game" / "test_hanging_runtime.c",
        ("painting_geometry_and_lifecycle",
         "painting_footprint_and_placement",
         "leash_knot_transfer_and_break", "hanging_checkpoint",
         "pending_leash_reload_boundary", "item_frame_lifecycle",
         "--oracle"))
    source_has(
        ROOT / "magma" / "trace" / "test_hanging_entities.py",
        ('"painting_counter_100_break"', '"knot_break_leash"',
         '"frame_explosion"', '"frame_insert_rotate"',
         '"painting_constructor_all_art_',
         '"mixed_hanging_order_',
         '"damage_entity_',
         '"map_boundary_',
         '"living_leash"',
         "227 real-Java/native rows"))
    source_has(
        ROOT / "magma" / "trace" / "test_hanging_save.py",
        ("save_fork.capture_locked", '"llama_leash_pending"',
         '"item_frames"', '"stack_payload"',
         '"living_leashes"',
         '"isolate_server_globals_locked"',
         "recreated the knot on the first controlled tick"))
    source_has(
        ROOT / "magma" / "trace" / "state_capsule.py",
        ('"set_painting"', '"set_leash_knot"',
         '"set_item_frame"', '"item_frame"',
         '"map_colors_file"', '"map_payloads"',
         '"restore_living_leash_pending"',
         '"restore_living_leash_knot"'))
    source_has(
        ROOT / "magma" / "game" / "runtime.c",
        ("gm_runtime_place_painting", "gm_runtime_break_painting",
         "gm_runtime_place_item_frame", "gm_runtime_damage_item_frame",
         "gm_runtime_damage_leash_knot",
         "gm_runtime_item_frame_tracker_tick",
         "gm_runtime_item_frame_set_map_colors",
         "gm_runtime_attach_living_to_fence",
         "runtime_restore_pending_living_knots"))
    source_has(
        ROOT / "magma" / "game" / "entity_render.c",
        ("gm_paintings_emit", "gm_leash_knots_emit",
         "gm_living_leashes_emit", "provoking (last) vertex"))
    source_has(
        ROOT / "magma" / "game" / "item_render.c",
        ("gm_item_frames_emit", "gm_item_frame_block_items_emit",
         "gm_item_frame_flat_items_emit", "gm_item_frame_map_plane_emit",
         "gm_item_frame_map_icon_emit", "gm_item_frame_map_rgba"))
    source_has(
        ROOT / "verify" / "ui_entities" / "measure_hanging_subject.py",
        ('"hanging_frame_map"', "HARD_THRESHOLD = 25",
         "mutation_selftest", '"hanging_leashed_llama"'))
    print("PASS hanging family: item frame, painting, and leash knot are "
          "live-bounded; 227 real-Java callback rows, constructor-art RNG, "
          "all six mixed-store orders, "
          "all hanging damage equivalence classes, "
          "map-decoration quantization/lifecycle, "
          "native checkpoint/capsule "
          "restore, tagged-item-frame Anvil reload, and first-tick knot "
          "reconstruction and all 16 vanilla leashable classes are locked. "
          "All hanging render paths are live; ENT-05 remains open only for "
          "the measured 23-pixel fixed-function coverage tail")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, HangingFamilyError) as error:
        print(f"FAIL hanging family: {error}")
        raise SystemExit(1)
