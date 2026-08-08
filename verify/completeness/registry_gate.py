#!/usr/bin/env python3
"""Fail when the Java 1.11.2 entity/tile registries escape the coverage ledger."""

from __future__ import annotations

import collections
import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = pathlib.Path(__file__).with_name("registry_manifest.json")
COMPLETENESS = ROOT / "docs" / "COMPLETENESS.md"
ENTITY_SOURCE = (
    ROOT / "java" / "oracle-src" / "net" / "minecraft" / "entity"
    / "EntityList.java"
)
TILE_SOURCE = (
    ROOT / "java" / "oracle-src" / "net" / "minecraft" / "tileentity"
    / "TileEntity.java"
)

ENTITY_RE = re.compile(
    r'register\(\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*'
    r"([A-Za-z0-9_$.]+)\.class"
)
TILE_RE = re.compile(
    r'register\(\s*"([^"]+)"\s*,\s*([A-Za-z0-9_$.]+)\.class'
)
TODO_RE = re.compile(r"^[A-Z]+-[0-9]{2}$")


def fail(message: str) -> None:
    raise AssertionError(message)


def unique(rows: list[tuple], label: str) -> None:
    counts = collections.Counter(rows)
    duplicate = [row for row, count in counts.items() if count != 1]
    if duplicate:
        fail(f"duplicate {label} rows: {duplicate}")


def main() -> int:
    data = json.loads(MANIFEST.read_text())
    statuses = set(data["status_definitions"])
    entities = data["entities"]
    tiles = data["tile_entities"]

    java_entities = [
        (int(entity_id), name, class_name)
        for entity_id, name, class_name in ENTITY_RE.findall(
            ENTITY_SOURCE.read_text()
        )
    ]
    manifest_entities = [
        (row["id"], row["name"], row["class"]) for row in entities
    ]
    java_tiles = TILE_RE.findall(TILE_SOURCE.read_text())
    manifest_tiles = [(row["name"], row["class"]) for row in tiles]

    unique(java_entities, "Java entity registry")
    unique(manifest_entities, "manifest entity registry")
    unique(java_tiles, "Java tile registry")
    unique(manifest_tiles, "manifest tile registry")
    if java_entities != manifest_entities:
        missing = sorted(set(java_entities) - set(manifest_entities))
        stale = sorted(set(manifest_entities) - set(java_entities))
        fail(f"entity registry mismatch: missing={missing}, stale={stale}")
    if java_tiles != manifest_tiles:
        missing = sorted(set(java_tiles) - set(manifest_tiles))
        stale = sorted(set(manifest_tiles) - set(java_tiles))
        fail(f"tile registry mismatch: missing={missing}, stale={stale}")

    completeness = COMPLETENESS.read_text()
    for kind, rows in (("entity", entities), ("tile", tiles)):
        for row in rows:
            if row["status"] not in statuses:
                fail(f"unknown {kind} status: {row}")
            todo = row["todo"]
            if not TODO_RE.fullmatch(todo):
                fail(f"invalid {kind} TODO id: {row}")
            if f"`{todo}`" not in completeness:
                fail(f"{kind} TODO {todo} is absent from docs/COMPLETENESS.md")

    entity_counts = collections.Counter(row["status"] for row in entities)
    tile_counts = collections.Counter(row["status"] for row in tiles)
    print(
        "PASS completeness registry: "
        f"{len(entities)} entities {dict(sorted(entity_counts.items()))}; "
        f"{len(tiles)} tile entities {dict(sorted(tile_counts.items()))}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, KeyError, OSError, ValueError) as exc:
        print(f"FAIL completeness registry: {exc}", file=sys.stderr)
        raise SystemExit(1)
