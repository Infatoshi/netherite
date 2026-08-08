#!/usr/bin/env python3
"""Inventory the residual fixed living-entity surface for ENT-09."""

from __future__ import annotations

import argparse
import json
import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "magma" / "game" / "mob_live.h"
GENERATED = ROOT / "magma" / "game" / "living_cold_slot.generated.h"
ALLOWED_HOT_LOOPS = {
    ("blaze/core/ew_entity_store.h", "ew_store_clear"),
    ("blaze/core/ew_entity_store.h", "ew_store_copy"),
    ("magma/game/runtime.c", "runtime_capture_mob_slots"),
    ("magma/game/runtime.c", "runtime_reconcile_removed_mob_order"),
    ("magma/game/runtime.c", "runtime_checkpoint_payload_valid"),
    ("magma/game/mob_live.c", "mob_source_damage_passive_slot_exact"),
    ("magma/game/mob_live.c", "loaded_order_prepare"),
    ("magma/game/mob_live.c", "alive_count"),
    ("magma/game/mob_live.c", "living_count"),
    ("magma/game/mob_live.c", "passive_count"),
    ("magma/game/mob_live.c", "type_count"),
    ("magma/game/mob_live.c", "mob_slot_by_eid"),
    ("magma/game/mob_live.c", "living_next_slot_from_store"),
    ("magma/game/mob_live.c", "gm_mobs_fill_views"),
}
ALLOWED_STORE_SPAWNS = {
    ("blaze/core/ew_entity_store.h", "ew_store_spawn"),
    ("magma/game/mob_live.c", "living_spawn_slot"),
}
ALLOWED_LOADED_REF_SLOT_FUNCTIONS = {
    "loaded_ref_valid_any",
    "gm_mobs_living_cold_park_hot",
    "living_loaded_ref_stage",
    "living_loaded_ref_stage_secondary",
    "living_spawn_finish",
    "gm_mobs_tick_controlled",
    "gm_mobs_tick",
    # The only direct ref.slot use in this dispatcher is the bounded XP-orb
    # branch. Living refs immediately pass through living_loaded_ref_stage.
    "gm_mobs_tick_spawn_context",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--strict", action="store_true")
    return parser.parse_args()


def living_fields() -> list[dict[str, object]]:
    source = HEADER.read_text(encoding="utf-8")
    begin = source.index("typedef struct {\n    EwStore a, b;")
    end = source.index("} GmMobLive;", begin)
    body = re.sub(r"/\*.*?\*/", "", source[begin:end], flags=re.S)
    fields = []
    pattern = re.compile(
        r"^(.*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*"
        r"\[EW_MAX_ENTITIES\](.*)$")
    for statement in body.split(";"):
        normalized = " ".join(statement.split())
        if "EW_MAX_ENTITIES" not in normalized:
            continue
        match = pattern.match(normalized)
        if not match:
            raise RuntimeError(
                f"cannot classify fixed living field: {normalized}")
        fields.append({
            "type": match.group(1).strip(),
            "name": match.group(2),
            "tail_dimensions": match.group(3).strip(),
        })
    return fields


def production_sources() -> list[pathlib.Path]:
    roots = (ROOT / "magma" / "game", ROOT / "blaze" / "core")
    paths = []
    for root in roots:
        for suffix in ("*.c", "*.h"):
            paths.extend(path for path in root.glob(suffix)
                         if not path.name.startswith("test_"))
    return sorted(paths)


def enclosing_function(source: str, offset: int) -> str:
    prefix = source[:offset]
    pattern = re.compile(
        r"(?:^|\n)[ \t]*(?:[A-Za-z_][A-Za-z0-9_]*[ \t*]+)+"
        r"([A-Za-z_][A-Za-z0-9_]*)[ \t]*\([^;{}]*\)[ \t]*\{",
        re.S,
    )
    matches = list(pattern.finditer(prefix))
    return matches[-1].group(1) if matches else "<file>"


def locations(pattern: re.Pattern[str]) -> list[dict[str, object]]:
    found = []
    for path in production_sources():
        source = path.read_text(encoding="utf-8")
        offset = 0
        for number, line in enumerate(source.splitlines(keepends=True), 1):
            if pattern.search(line):
                found.append({
                    "location": f"{path.relative_to(ROOT)}:{number}",
                    "path": str(path.relative_to(ROOT)),
                    "function": enclosing_function(source, offset),
                    "line": line.strip(),
                    "context": source[max(0, offset - 1600):offset + 800],
                })
            offset += len(line)
    return found


def multiline_locations(pattern: re.Pattern[str]) -> list[dict[str, object]]:
    found = []
    for path in production_sources():
        source = path.read_text(encoding="utf-8")
        for match in pattern.finditer(source):
            offset = match.start()
            number = source.count("\n", 0, offset) + 1
            found.append({
                "location": f"{path.relative_to(ROOT)}:{number}",
                "path": str(path.relative_to(ROOT)),
                "function": enclosing_function(source, offset),
                "line": " ".join(match.group(0).split()),
                "context": source[max(0, offset - 1600):offset + 1600],
            })
    return found


def main() -> int:
    args = parse_args()
    fields = living_fields()
    generated = GENERATED.read_text(encoding="utf-8")
    uncovered_fields = [
        field for field in fields
        if f" {field['name']}{field['tail_dimensions']};" not in generated
        or f"dst->{field['name']}" not in generated
        or f"src->{field['name']}" not in generated
    ]
    loops = multiline_locations(re.compile(
        r"\b(?:for|while)\s*\([^)]*\b(?:EW_MAX_ENTITIES|"
        r"EW_SPAWN_LIMIT|GM_MOB_CAPACITY)\b[^)]*\)",
        re.S))
    spawns = locations(re.compile(r"\bew_store_spawn\s*\("))
    loaded_ref_slots = locations(re.compile(r"\bref(?:\.|->)slot\b"))
    def reviewed_loop(item: dict[str, object]) -> bool:
        if (item["path"], item["function"]) in ALLOWED_HOT_LOOPS:
            return True
        context = str(item["context"])
        if item["path"] == "blaze/core/ew_entity_store.h":
            return "ew_store_clear" in context or "ew_store_copy" in context
        if item["path"] != "magma/game/runtime.c":
            return (item["path"] == "magma/game/mob_live.c"
                    and "cold->polar_player_target = 1" in context)
        if item["line"] == "for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)":
            return ("runtime_capture_mob_slots" in context
                    or "runtime_reconcile_removed_mob_order" in context)
        return (item["line"]
                == "for (int entity = 0; entity < EW_MAX_ENTITIES; ++entity)"
                and "cold->villager_inventory" in context)

    def reviewed_spawn(item: dict[str, object]) -> bool:
        return ((item["path"], item["function"]) in ALLOWED_STORE_SPAWNS
                or (item["path"] == "blaze/core/ew_entity_store.h"
                    and "static inline int ew_store_spawn" in item["line"]))

    unreviewed_loops = [item for item in loops if not reviewed_loop(item)]
    unreviewed_spawns = [item for item in spawns if not reviewed_spawn(item)]
    unreviewed_loaded_ref_slots = [
        item for item in loaded_ref_slots
        if item["function"] not in ALLOWED_LOADED_REF_SLOT_FUNCTIONS
    ]
    payload = {
        "schema": "netherite.ent09_living_capacity_census",
        "version": 1,
        "hot_slots_including_player": 96,
        "fixed_living_field_count": len(fields),
        "fixed_living_fields": fields,
        "production_hot_cap_loop_count": len(loops),
        "production_hot_cap_loops": [item["location"] for item in loops],
        "reviewed_hot_page_loop_count": len(loops) - len(unreviewed_loops),
        "unreviewed_hot_cap_loops": [
            {key: value for key, value in item.items()
             if key not in {"line", "context"}}
            for item in unreviewed_loops
        ],
        "direct_store_spawn_count": len(spawns),
        "direct_store_spawns": [item["location"] for item in spawns],
        "unreviewed_direct_store_spawns": [
            {key: value for key, value in item.items()
             if key not in {"line", "context"}}
            for item in unreviewed_spawns
        ],
        "loaded_ref_slot_access_count": len(loaded_ref_slots),
        "unreviewed_loaded_ref_slot_accesses": [
            {key: value for key, value in item.items()
             if key not in {"line", "context"}}
            for item in unreviewed_loaded_ref_slots
        ],
        "generated_cold_field_count": len(fields) - len(uncovered_fields),
        "uncovered_cold_fields": uncovered_fields,
    }
    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print("ENT-09 living capacity census: "
              f"{len(fields) - len(uncovered_fields)}/{len(fields)} fields "
              f"cold-covered, {len(unreviewed_loops)} unreviewed hot-cap "
              f"loops, {len(unreviewed_spawns)} unreviewed spawn sites, "
              f"{len(unreviewed_loaded_ref_slots)} unreviewed loaded-ref "
              "slot accesses")
    residual = bool(uncovered_fields or unreviewed_loops
                    or unreviewed_spawns or unreviewed_loaded_ref_slots)
    if args.strict and residual:
        print("FAIL ENT-09 living capacity: unreviewed fixed surface remains")
        return 1
    if args.strict:
        print("PASS ENT-09 living capacity architectural census")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"FAIL ENT-09 living capacity census: {error}")
        raise SystemExit(1)
