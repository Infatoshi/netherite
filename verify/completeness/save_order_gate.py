#!/usr/bin/env python3
"""Lock the checked SAVE-03 Java A/B/native ordering observations."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "save_order_manifest.json"


class SaveOrderError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SaveOrderError(message)


def source_has(path: pathlib.Path, tokens: tuple[str, ...]) -> None:
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("version") == 1
            and manifest.get("todo") == "SAVE-03",
            "invalid save-order manifest identity")
    cases = manifest.get("cases", {})
    required_names = {
        "entity_chunk_cycle_forward", "entity_chunk_cycle_reverse",
        "hopper_forward", "hopper_reverse", "player_minecart",
    }
    require(set(cases) == required_names,
            "save-order manifest case set changed")
    requirements = manifest["requirements"]
    for name, case in cases.items():
        require(case.get("java_repeat") == requirements["java_repeat"],
                f"{name}: Java repeat is not exact")
        require(case.get("native") == requirements["native"],
                f"{name}: native continuation is not exact")
        require(case.get("rejected_fields")
                == requirements["rejected_fields"],
                f"{name}: capability rejection remains")
        require(case.get("active_chunks") == requirements["active_chunks"]
                and case.get("raw_cells") == requirements["raw_cells"],
                f"{name}: raw world observation changed")
        require(set(case.get("raw_horizons", {}).values()) == {
                    requirements["raw_horizons"]},
                f"{name}: a raw horizon is not exact")

    forward = cases["entity_chunk_cycle_forward"]
    reverse = cases["entity_chunk_cycle_reverse"]
    require(forward["uuid_order"] == list(reversed(reverse["uuid_order"])),
            "entity negative control does not reverse UUID order")
    require(forward["survivor_eid"] != reverse["survivor_eid"]
            and forward["survivor_count"] == reverse["survivor_count"] == 2,
            "entity order no longer controls the merge survivor")
    require(all(case[edge] >= 1 for case in (forward, reverse)
                for edge in ("unloaded_after_ticks", "reloaded_after_ticks")),
            "entity fixture did not cross a real unload/reload boundary")

    hopper_forward = cases["hopper_forward"]
    hopper_reverse = cases["hopper_reverse"]
    require(hopper_forward["tile_order"]
            == hopper_forward["tickable_order"]
            and hopper_reverse["tile_order"]
            == hopper_reverse["tickable_order"],
            "loaded/tickable tile order split")
    require(hopper_forward["chest_slots"]
            == list(reversed(hopper_reverse["chest_slots"])),
            "tile-order negative control no longer changes transfer order")

    passenger = cases["player_minecart"]
    require(passenger["riding_eid_t0"]
            == passenger["riding_eid_t20"] == 5201
            and passenger["loaded_entity_order_t20"] == [5201],
            "player/minecart graph did not survive its continuation")

    source_has(
        ROOT / "java" / "Minecraft" / "src" / "main" / "java"
        / "qrl" / "Recorder.java",
        ("queue_chunk_unload_locked", "passenger_uuids",
         "world.loadedEntityList.clear()", "tickableTileEntities.clear()"))
    source_has(
        HERE / "stage_entity_order_fixture.py",
        ("--cycle-chunk", "uuid_order", "queue_chunk_unload_locked"))
    source_has(
        HERE / "fork_runner.py",
        ("--keep-reload-topology", "loaded_chunk_order"))
    source_has(
        ROOT / "magma" / "game" / "runtime.c",
        ("gm_runtime_restore_loaded_entity_order",
         "gm_runtime_restore_loaded_tile_order",
         "gm_runtime_restore_tickable_tile_order"))
    source_has(
        ROOT / "magma" / "trace" / "state_capsule.py",
        ('"restore_loaded_entity_order"', '"restore_loaded_tile_order"',
         '"restore_tickable_tile_order"'))
    print("PASS save order: chunks/entities/tiles/passengers, "
          "opposite-order causal branches, Java A/B/native")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, SaveOrderError) as error:
        print(f"FAIL save order: {error}")
        raise SystemExit(1)
