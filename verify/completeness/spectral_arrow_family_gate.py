#!/usr/bin/env python3
"""Lock the measured ENT-06 live-bounded Spectral Arrow boundary."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "spectral_arrow_family_manifest.json"
REGISTRY = HERE / "registry_manifest.json"


class SpectralArrowFamilyError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SpectralArrowFamilyError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema")
            == "netherite.spectral_arrow_family_gate"
            and manifest.get("version") == 1
            and manifest.get("todo") == "ENT-06",
            "invalid Spectral Arrow family manifest identity")
    require(manifest.get("payload_oracle") == {
        "java_version": "1.11.2",
        "aggregate_cases": 8,
        "spectral_duration_case": 1,
        "glowing_effect_and_duration": "exact",
    }, "Spectral Arrow payload evidence changed")
    require(manifest.get("state_continuation") == {
        "joint_arrow_classes": 3,
        "native_ticks": 1,
        "motion_collision_payload_uuid_rng_and_nbt": "bit_exact",
    }, "Spectral Arrow continuation evidence changed")
    require(set(manifest.get("implemented", [])) == {
        "live_spectral_arrow_subclass",
        "bow_launch_and_ammunition_selection",
        "infinity_consumes_spectral_ammunition",
        "exact_configurable_glowing_duration",
        "mob_and_player_payload_delivery",
        "glowing_render_flag_handoff",
        "spectral_item_pickup_and_status",
        "block_impact_and_embedded_state",
        "checkpoint_subclass_payload_continuation",
        "java_capsule_native_motion_continuation",
        "uuid_entity_rng_and_semantic_nbt_continuation",
        "shared_arrow_model_and_texture_path",
    }, "Spectral Arrow implemented boundary is incomplete")
    require(set(manifest.get("remaining", [])) == {
        "arbitrary_multi_entity_projectile_collision_order",
        "foreign_dimension_terminal_event_delivery",
        "strict_same_scene_spectral_arrow_pixels",
    }, "Spectral Arrow open boundary changed without updating its gate")

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    row = next(value for value in registry["entities"]
               if value["class"] == "EntitySpectralArrow")
    require(row["status"] == "live_bounded" and row["todo"] == "ENT-06",
            "Spectral Arrow registry row does not match the bounded boundary")

    source_has(ROOT / "magma" / "game" / "runtime.c", (
        "arrow->arrow_kind = GM_ARROW_SPECTRAL",
        "arrow->arrow_pickup_item = 439",
        "arrow->arrow_spectral_duration",
        "runtime_arrow_apply_player_payload",
        "runtime_arrow_apply_mob_payload"))
    source_has(ROOT / "magma" / "game" / "test_arrow_payload_live.c", (
        "Infinity bow fires spectral ammunition",
        "spectral impact applies the configured Glowing duration",
        "active Glowing effect reaches Entity.isGlowing render state",
        "spectral pickup returns Items.SPECTRAL_ARROW",
        "checkpoint retains arrow class, payload, pickup, and ground state"))
    source_has(ROOT / "magma" / "trace" / "test_arrow_payload.py", (
        '"spectral_duration"',
        "arrow payload cases"))
    source_has(ROOT / "magma" / "trace" / "test_arrow_capsule.py", (
        '"arrow_kind": 2, "spectral_duration": 321',
        "normal/tipped/spectral arrow capsule"))
    source_has(ROOT / "magma" / "trace" / "state_capsule.py", (
        '"EntityTippedArrow", "EntitySpectralArrow"',
        '"entities.player_tipped_spectral_arrow": "exact"'))
    source_has(ROOT / "magma" / "game" / "entity_render.c", (
        '"EntitySpectralArrow",  ER_TYPE_ARROW', "emit_arrow("))
    print(
        "PASS Spectral Arrow family: live-bounded launch/ammo, Glowing "
        "impact, pickup, checkpoint, capsule/NBT continuation, and shared "
        "render paths are explicitly covered")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, StopIteration, ValueError,
            SpectralArrowFamilyError) as error:
        print(f"FAIL Spectral Arrow family: {error}")
        raise SystemExit(1)
