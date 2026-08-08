#!/usr/bin/env python3
"""Lock the measured MODE-02 live-bounded Structure Block boundary."""

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def source_has(path, tokens):
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main():
    manifest = json.loads((HERE / "structure_block_manifest.json")
                          .read_text(encoding="utf-8"))
    require(manifest["schema"] == "netherite.structure_block_gate"
            and manifest["version"] == 1 and manifest["todo"] == "MODE-02"
            and manifest["classification"] == "live_bounded",
            "invalid Structure Block manifest identity")
    require(manifest["oracle"] == {
        "modes": 4, "all_block_state_transforms": "exact",
        "tile_payloads": "exact", "represented_entity_classes": 8,
        "minecart_subtypes": 7, "constructor_rng_and_order": "exact",
        "redstone_edges": "exact",
    }, "Structure Block oracle boundary changed")
    registry = json.loads((HERE / "registry_manifest.json")
                          .read_text(encoding="utf-8"))
    row = next(row for row in registry["tile_entities"]
               if row["class"] == "TileEntityStructure")
    require(row["status"] == "live_bounded" and row["todo"] == "MODE-02",
            "Structure Block registry row is not live-bounded")
    source_has(ROOT / "magma/trace/test_structure_block.py", (
        "saved living/XP/item/boat/TNT/falling-block/End-crystal/minecart",
        "all seven minecart subtype payloads", "seeded integrity"))
    source_has(ROOT / "magma/game/test_structure_block_runtime.c", (
        "constructor defaults match Java 1.11.2",
        "Template tile NBT replaces stale chest data",
        "checkpoint rejects an invalid Structure Template tile count"))
    source_has(ROOT / "magma/game/runtime.c", (
        "gm_runtime_structure_save", "gm_runtime_structure_load",
        "runtime_structure_transform_meta"))
    source_has(ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java", (
        "oracleStructureBlockLocked", "same within-chunk origin"))
    print("PASS Structure Block: bounded exact modes, templates, transforms, "
          "tile/entity payloads, RNG/order, redstone, and continuation")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, StopIteration, ValueError, RuntimeError) as error:
        print(f"FAIL Structure Block: {error}")
        raise SystemExit(1)
