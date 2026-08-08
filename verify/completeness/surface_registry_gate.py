#!/usr/bin/env python3
"""Generate and gate completeness candidates from initialized Java registries."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import subprocess
import sys
import time
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ORACLE_SOURCE = ROOT / "java" / "oracle-src" / "net" / "minecraft"
START = ROOT / "java" / "start_oracle_instance.sh"
MANIFEST = HERE / "surface_registry_manifest.json"
COMPLETENESS = ROOT / "docs" / "COMPLETENESS.md"
sys.path.insert(0, str(HERE))
import save_fork  # noqa: E402


class SurfaceRegistryError(RuntimeError):
    pass


def _oracle(action: str, instance: int, environment: dict[str, str]) -> None:
    command = ["bash", str(START), action, str(instance)]
    if action == "start":
        command.append("0")
    result = subprocess.run(
        command, cwd=ROOT, env=environment, check=False)
    if result.returncode:
        raise SurfaceRegistryError(
            f"oracle instance {action} failed with rc={result.returncode}")


def _capture_live(instance: int) -> dict[str, Any]:
    run_root = ROOT / ".tmp" / f"registry-surfaces-{os.getpid()}"
    environment = dict(os.environ)
    environment.update({
        "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
        "ORACLE_POOL_OUT_ROOT": str(run_root),
        "ORACLE_POOL_WAIT": "1",
        "TMPDIR": str(ROOT / ".tmp"),
        "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache" / "uv"),
    })
    port = int(environment.get("ORACLE_POOL_PORT_BASE", "25600")) + instance
    started = False
    try:
        _oracle("start", instance, environment)
        started = True
        deadline = time.monotonic() + 120.0
        while True:
            try:
                result = save_fork.request(port, "registry_surfaces")
                if result.get("ok"):
                    return result
                raise SurfaceRegistryError(
                    f"registry export failed: {result}")
            except save_fork.SaveForkError:
                if time.monotonic() >= deadline:
                    raise SurfaceRegistryError(
                        "registry oracle did not become ready")
                time.sleep(0.1)
    finally:
        if started:
            _oracle("stop", instance, environment)


def _source_classes(directory: pathlib.Path, prefix: str,
                    exclude: set[str]) -> list[str]:
    classes = []
    for path in sorted(directory.glob(f"{prefix}*.java")):
        if path.stem in exclude:
            continue
        classes.append(path.stem)
    return classes


def _enrich(raw: dict[str, Any]) -> dict[str, Any]:
    blocks = []
    for row in raw["blocks"]:
        enriched = dict(row)
        enriched.update({
            "fixture": f"block-{row['id']:03d}-{row['name'].split(':')[-1]}",
            "todo": "WORLD-02",
        })
        if row["comparator_override"]:
            enriched["secondary_todo"] = "RED-02"
        blocks.append(enriched)

    items = []
    for row in raw["items"]:
        enriched = dict(row)
        enriched.update({
            "fixture": f"item-{row['id']:03d}-{row['name'].split(':')[-1]}",
            "todo": "ITEM-08",
        })
        items.append(enriched)

    crafting = []
    for row in raw["crafting_recipes"]:
        enriched = dict(row)
        enriched.update({
            "fixture": f"crafting-recipe-{row['index']:03d}",
            "todo": "ITEM-01",
        })
        crafting.append(enriched)

    smelting = []
    for index, row in enumerate(raw["smelting_recipes"]):
        enriched = dict(row)
        enriched.update({
            "fixture": f"smelting-recipe-{index:03d}",
            "todo": "ITEM-02",
        })
        smelting.append(enriched)

    potions = []
    for row in raw["potions"]:
        enriched = dict(row)
        enriched.update({
            "fixture": f"potion-effect-{row['id']:02d}",
            "todo": "ITEM-05",
        })
        potions.append(enriched)
    potion_types = []
    for row in raw["potion_types"]:
        enriched = dict(row)
        enriched.update({
            "fixture": f"potion-type-{row['id']:02d}",
            "todo": "ITEM-05",
        })
        potion_types.append(enriched)

    loot_tables = [{
        "name": name,
        "fixture": "loot-" + name.split(":")[-1].replace("/", "-"),
        "todo": "ITEM-07",
    } for name in raw["loot_tables"]]

    container_dir = ORACLE_SOURCE / "inventory"
    containers = [{
        "class": f"net.minecraft.inventory.{name}",
        "fixture": "container-" + re.sub(
            r"(?<!^)(?=[A-Z])", "-", name).lower(),
        "todo": "UI-02",
        **({"secondary_todo": "UI-04"} if name in {
            "ContainerBeacon", "ContainerHorseChest",
            "ContainerHorseInventory", "ContainerShulkerBox",
        } else {}),
    } for name in _source_classes(
        container_dir, "Container", {"Container", "ContainerHorseChest"})]

    gui_dir = ORACLE_SOURCE / "client" / "gui" / "inventory"
    guis = [{
        "class": f"net.minecraft.client.gui.inventory.{name}",
        "fixture": "gui-" + re.sub(
            r"(?<!^)(?=[A-Z])", "-", name).lower(),
        "todo": "UI-02",
        **({"secondary_todo": "UI-04"} if name in {
            "GuiBeacon", "GuiContainerCreative", "GuiScreenHorseInventory",
            "GuiShulkerBox",
        } else {}),
    } for name in _source_classes(gui_dir, "Gui", {"GuiContainer"})]

    base_manifest = json.loads(
        (HERE / "registry_manifest.json").read_text())
    return {
        "schema": "netherite.registry_fixture_candidates",
        "version": 1,
        "source_version": "Minecraft Java 1.11.2",
        "entity_registry_rows": len(base_manifest["entities"]),
        "tile_entity_registry_rows": len(base_manifest["tile_entities"]),
        "blocks": blocks,
        "items": items,
        "crafting_recipes": crafting,
        "smelting_recipes": smelting,
        "potions": potions,
        "potion_types": potion_types,
        "loot_tables": loot_tables,
        "containers": containers,
        "guis": guis,
    }


def _source_registry_counts() -> dict[str, int]:
    block_source = (ORACLE_SOURCE / "block" / "Block.java").read_text()
    potion_source = (ORACLE_SOURCE / "potion" / "Potion.java").read_text()
    type_source = (ORACLE_SOURCE / "potion" / "PotionType.java").read_text()
    loot_source = (
        ORACLE_SOURCE / "world" / "storage" / "loot" / "LootTableList.java"
    ).read_text()
    return {
        "blocks": len(re.findall(
            r'registerBlock\(\s*\d+\s*,\s*(?:"[^"]+"|AIR_ID)\s*,',
            block_source)),
        "items": 392,
        "potions": len(re.findall(
            r'REGISTRY\.register\(\s*\d+\s*,\s*new ResourceLocation\("',
            potion_source)),
        "potion_types": len(re.findall(
            r'registerPotionType\("', type_source)),
        "loot_tables": len(re.findall(r'= register\("', loot_source)),
        "containers": len(_source_classes(
            ORACLE_SOURCE / "inventory", "Container",
            {"Container", "ContainerHorseChest"})),
        "guis": len(_source_classes(
            ORACLE_SOURCE / "client" / "gui" / "inventory", "Gui",
            {"GuiContainer"})),
    }


def _validate(manifest: dict[str, Any]) -> None:
    expected_keys = {
        "schema", "version", "source_version", "entity_registry_rows",
        "tile_entity_registry_rows", "blocks", "crafting_recipes",
        "items",
        "smelting_recipes", "potions", "potion_types", "loot_tables",
        "containers", "guis",
    }
    if set(manifest) != expected_keys \
            or manifest["schema"] != "netherite.registry_fixture_candidates" \
            or manifest["version"] != 1:
        raise SurfaceRegistryError("invalid surface registry manifest header")
    source_counts = _source_registry_counts()
    for family, count in source_counts.items():
        if len(manifest[family]) != count:
            raise SurfaceRegistryError(
                f"{family} census stale: {len(manifest[family])} != {count}")
    base = json.loads((HERE / "registry_manifest.json").read_text())
    if manifest["entity_registry_rows"] != len(base["entities"]) \
            or manifest["tile_entity_registry_rows"] \
                != len(base["tile_entities"]):
        raise SurfaceRegistryError("entity/tile census link is stale")
    completeness = COMPLETENESS.read_text()
    fixture_ids: set[str] = set()
    for family in expected_keys - {
            "schema", "version", "source_version", "entity_registry_rows",
            "tile_entity_registry_rows"}:
        rows = manifest[family]
        if not isinstance(rows, list) or not rows:
            raise SurfaceRegistryError(f"{family} is empty")
        for row in rows:
            fixture = row.get("fixture")
            todo = row.get("todo")
            if not isinstance(fixture, str) or not fixture \
                    or fixture in fixture_ids:
                raise SurfaceRegistryError(
                    f"duplicate/invalid fixture candidate: {fixture!r}")
            fixture_ids.add(fixture)
            for value in (todo, row.get("secondary_todo")):
                if value is not None and f"`{value}`" not in completeness:
                    raise SurfaceRegistryError(
                        f"fixture {fixture} has unknown TODO {value}")
    block_ids = [row["id"] for row in manifest["blocks"]]
    if len(block_ids) != len(set(block_ids)) or block_ids != sorted(block_ids):
        raise SurfaceRegistryError("block registry IDs are duplicate/unsorted")
    item_ids = [row["id"] for row in manifest["items"]]
    if len(item_ids) != len(set(item_ids)) or item_ids != sorted(item_ids):
        raise SurfaceRegistryError("item registry IDs are duplicate/unsorted")
    recipe_indices = [row["index"] for row in manifest["crafting_recipes"]]
    if recipe_indices != list(range(len(recipe_indices))):
        raise SurfaceRegistryError("crafting recipe order is not contiguous")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--update", action="store_true",
                        help="capture the live Java registries and rewrite manifest")
    parser.add_argument("--live", action="store_true",
                        help="also compare the manifest with a live Java export")
    parser.add_argument("--instance", type=int, default=99)
    args = parser.parse_args()
    captured = None
    if args.update or args.live:
        captured = _enrich(_capture_live(args.instance))
    if args.update:
        MANIFEST.write_text(
            json.dumps(captured, indent=2, sort_keys=True) + "\n")
    if not MANIFEST.is_file():
        raise SurfaceRegistryError(
            f"missing {MANIFEST}; run with --update")
    manifest = json.loads(MANIFEST.read_text())
    _validate(manifest)
    if args.live and captured != manifest:
        raise SurfaceRegistryError(
            "checked registry candidates differ from live Java; run --update")
    counts = {
        key: len(manifest[key]) for key in (
            "blocks", "items", "crafting_recipes", "smelting_recipes", "potions",
            "potion_types", "loot_tables", "containers", "guis")
    }
    print("PASS generated registry candidates: " + ", ".join(
        f"{key}={value}" for key, value in counts.items()))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, SurfaceRegistryError, ValueError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL generated registry candidates: {exc}", file=sys.stderr)
        raise SystemExit(1)
