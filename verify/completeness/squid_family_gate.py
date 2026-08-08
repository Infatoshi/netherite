#!/usr/bin/env python3
"""Lock the measured AI-04 live-bounded Squid boundary."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "squid_family_manifest.json"
REGISTRY = HERE / "registry_manifest.json"


class SquidFamilyError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SquidFamilyError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "netherite.squid_family_gate"
            and manifest.get("version") == 1
            and manifest.get("todo") == "AI-04",
            "invalid Squid-family manifest identity")
    require(manifest.get("active_ai_oracle") == {
        "java_version": "1.11.2",
        "scenarios": 5,
        "ticks": 76,
        "position_motion_and_aabb": "bit_exact",
        "animation_and_render_rotation": "bit_exact",
        "entity_rng_cursor": "exact",
        "water_and_dry_branches": "exact",
        "random_motion_refresh": "exact",
        "age_stop_boundary": "exact",
        "persistence_state": "exact",
    }, "Squid active-AI evidence changed")
    require(manifest.get("state_continuation") == {
        "native_checkpoint_ticks": 12,
        "native_checkpoint_after_tick": 6,
        "java_capsule_warmup_ticks": 9,
        "java_native_continuation_ticks": 16,
        "private_animation_motion_and_rng": "bit_exact",
        "loaded_and_mob_update_order": "exact",
    }, "Squid continuation evidence changed")
    require(manifest.get("render_oracle") == {
        "java_ab_states": 3,
        "java_ab_noise_pixels": 0,
        "swim_owned_pixels": 18642,
        "swim_hard_pixels": 1,
        "swim_hard_budget": 1,
        "swim_max_channel_delta": 32,
        "dry_owned_pixels": 6889,
        "dry_hard_pixels": 0,
        "dry_max_channel_delta": 1,
        "ownership_xor_pixels": 28,
        "negative_control": "exact",
    }, "Squid render evidence changed")
    require(set(manifest.get("implemented", [])) == {
        "active_random_swim_vector_task",
        "java_random_call_order",
        "water_motion_and_rotation",
        "dry_gravity_rotation_and_tentacle",
        "cycle_boundary_motion_refresh",
        "entity_age_motion_stop",
        "persistence_state",
        "exact_mathhelper_atan2_bias",
        "base_living_tick_and_aabb",
        "native_checkpoint_continuation",
        "real_java_state_capsule_continuation",
        "private_animation_and_motion_restore",
        "live_render_pose_handoff",
        "exact_rendersquid_transform_chain",
        "live_tentacle_angle_model",
        "swimming_and_dry_render_poses",
        "bounded_same_scene_model_pixels",
    }, "Squid implemented boundary is incomplete")
    require(set(manifest.get("remaining", [])) == {
        "exact_world_entity_spawner_pack_and_spawn_eligibility",
    }, "Squid open boundary changed without updating its checked gate")

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    row = next(value for value in registry["entities"]
               if value["class"] == "EntitySquid")
    require(row["status"] == "live_bounded" and row["todo"] == "AI-04",
            "Squid registry row does not match the bounded boundary")

    source_has(
        ROOT / "java" / "Minecraft" / "src" / "main" / "java"
        / "qrl" / "Recorder.java",
        ('"squid_active_exact", activeSquidExact',
         '"squid_entity_age", entityAge',
         'renderPinSquidTentacle',
         '"squid".equalsIgnoreCase(kind)'))
    source_has(
        ROOT / "magma" / "game" / "mob_live.c",
        ("tick_squid", "squid_atan2",
         "UINT64_C(4805340802404319232)",
         "m->squid_random_motion_x", "EW_TYPE_SQUID",
         "simulation-to-render handoff"))
    source_has(
        ROOT / "magma" / "game" / "entity_render.c",
        ("emit_squid", "RenderSquid.applyRotations",
         "live interpolated tentacle angle",
         "RenderLivingBase disables culling around ModelSquid"))
    source_has(
        ROOT / "magma" / "game" / "test_squid_runtime.c",
        ('"cycle_refresh"', "gm_native_save_write",
         "live Squid pose reaches the render view bit-exactly",
         "private state and RNG"))
    source_has(
        ROOT / "magma" / "trace" / "test_squid_ai.py",
        ('("cycle_refresh", 8)', '("dry", 12)',
         "PASS active Squid Java/native", "canonical_java"))
    source_has(
        ROOT / "magma" / "trace" / "test_squid_capsule.py",
        ("WARMUP_TICKS = 9", "CONTINUATION_TICKS = 16",
         "restore_squid_ai_state", "compare_squid"))
    source_has(
        ROOT / "magma" / "trace" / "state_capsule.py",
        ('if entity_type == "EntitySquid":',
         '"type": "restore_squid_ai_state"',
         '"type": "set_mob_no_ai"'))
    source_has(
        ROOT / "verify" / "ui_entities" / "measure_squid_subject.py",
        ("HARD_BUDGET", '"squid_swim_pose": 1',
         "Squid hard-pixel mutation was accepted"))
    print(
        "PASS Squid family: live-bounded active swim, dry, animation, and "
        "lifetime behavior have 76 bit-exact real-Java ticks across five "
        "fixtures, a 12-tick native checkpoint, 16 exact ticks after a "
        "warmed Java state capsule, and bounded swimming/dry same-scene pixels")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, StopIteration, ValueError,
            SquidFamilyError) as error:
        print(f"FAIL Squid family: {error}")
        raise SystemExit(1)
