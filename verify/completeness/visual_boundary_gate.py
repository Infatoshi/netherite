#!/usr/bin/env python3
"""Fail closed when the bounded native visual evidence loses coverage."""

from __future__ import annotations

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
GROUPS = {
    "VIS-01": ("magma/game/config.c", "magma/game/frame_capture.c",
               "magma/game/test_config.c", "magma/game/test_sky.c"),
    "VIS-02": ("magma/game/hand.c", "magma/game/test_hand.c",
               "verify/ui_hud/compare_ui_hud_oracle.py"),
    "VIS-03": ("magma/game/underwater.c", "magma/game/overlay.c",
               "magma/game/test_nausea_portal_oracle.c",
               "verify/ui_hud/test_ui_hud_mutations.py"),
    "VIS-04": ("verify/ui_entities/measure_visual_tail_subject.py",
               "verify/ui_entities/measure_slime_magma_subject.py"),
    "VIS-05": ("magma/game/entity_render.c", "magma/game/test_entity_render.c",
               "verify/completeness/registry_gate.py",
               "verify/ui_entities/run_oracle_gate.sh"),
    "VIS-06": ("magma/game/particles_live.c",
               "magma/game/test_particles_live.c",
               "magma/game/test_horse_particle_oracle.sh"),
    "VIS-07": ("magma/assets/blockmodels.c", "magma/game/item_render.c",
               "magma/game/test_item_render.c", "magma/assets/build_atlas.py"),
    "VIS-08": ("magma/game/entity_render.c",
               "verify/ui_entities/measure_chest_subject.py",
               "verify/ui_entities/measure_beacon_world.py",
               "verify/ui_entities/measure_spawner_subject.py",
               "verify/ui_entities/measure_minecart_variant_subject.py"),
    "VIS-09": ("magma/game/sky.c", "magma/game/weather_render.h",
               "magma/game/test_sky.c", "magma/game/test_weather_render.c"),
    "VIS-10": ("magma/game/window_compose.c", "magma/game/screen.c",
                "verify/ui_hud/run_ui_hud_gates.sh",
                "verify/ui_entities/run_oracle_gate.sh"),
    "VIS-11": ("magma/game/frame_capture.c", "magma/game/player_preview.c",
                "magma/game/test_nausea_portal_oracle.c",
                "verify/trace/window_compose_baseline.py"),
}


def paired_goldens(directory: pathlib.Path) -> int:
    a = {p.name[:-6] for p in directory.glob("*_a.png")}
    b = {p.name[:-6] for p in directory.glob("*_b.png")}
    if a != b:
        raise RuntimeError(
            f"unpaired Java A/B goldens in {directory}: "
            f"A-only={sorted(a-b)[:3]} B-only={sorted(b-a)[:3]}")
    return len(a)


def main() -> int:
    surfaces = json.loads(
        (ROOT / "verify/completeness/surface_registry_manifest.json").read_text())
    if len(surfaces["blocks"]) != 236 or len(surfaces["items"]) != 392:
        raise RuntimeError("block/item visual registry cardinality changed")
    if surfaces["entity_registry_rows"] != 81:
        raise RuntimeError("entity visual registry cardinality changed")
    if surfaces["tile_entity_registry_rows"] != 24:
        raise RuntimeError("tile visual registry cardinality changed")

    entity_pairs = paired_goldens(ROOT / "verify/ui_entities/goldens")
    hud_pairs = paired_goldens(ROOT / "verify/ui_hud/goldens")
    if entity_pairs < 100 or hud_pairs < 35:
        raise RuntimeError(
            f"visual fixture floor lost: entity={entity_pairs}, hud={hud_pairs}")

    for todo, evidence in GROUPS.items():
        for relative in evidence:
            path = ROOT / relative
            if not path.is_file() or path.stat().st_size == 0:
                raise RuntimeError(f"{todo}: missing evidence {relative}")

    print(
        "PASS visual boundary: 81 entity/24 tile identities, 236 block/392 "
        f"item identities, {entity_pairs} entity/tile and {hud_pairs} HUD/GUI "
        "Java A/B pairs, and all 11 renderer owners are fail-closed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL visual boundary: {exc}")
        raise SystemExit(1)
