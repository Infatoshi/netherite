#!/usr/bin/env python3
"""Generate the exact vanilla 1.11.2 item registry name-to-id table."""

import json
import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[2]
BLOCKS_JAVA = ROOT / "java/oracle-src/net/minecraft/init/Blocks.java"
ITEM_JAVA = ROOT / "java/oracle-src/net/minecraft/item/Item.java"
SURFACE = ROOT / "verify/completeness/surface_registry_manifest.json"
OUTPUT = pathlib.Path(__file__).with_name("item_name_manifest.h")


def main():
    blocks_source = BLOCKS_JAVA.read_text()
    item_source = ITEM_JAVA.read_text()
    surface = json.loads(SURFACE.read_text())
    block_ids = {row["name"].split(":", 1)[1]: row["id"]
                 for row in surface["blocks"]}
    constants = dict(re.findall(
        r"\b([A-Z][A-Z0-9_]*)\s*=\s*(?:\([^;=]+\))?"
        r"getRegisteredBlock\(\"([^\"]+)\"\)",
        blocks_source,
    ))
    rows = {}
    for constant in re.findall(
            r"registerItemBlock\(Blocks\.([A-Z][A-Z0-9_]*)", item_source):
        name = constants[constant]
        rows[f"minecraft:{name}"] = block_ids[name]
    for item_id, name in re.findall(
            r"registerItem\((\d+),\s*\"([^\"]+)\"", item_source):
        rows[f"minecraft:{name}"] = int(item_id)
    if len(rows) != 392 or len(set(rows.values())) != 392:
        raise RuntimeError(
            f"expected 392 unique vanilla item rows, got {len(rows)} names "
            f"and {len(set(rows.values()))} ids")
    ordered = sorted(rows.items())
    lines = [
        "#ifndef MAGMA_ITEM_NAME_MANIFEST_H",
        "#define MAGMA_ITEM_NAME_MANIFEST_H",
        "#include <stddef.h>",
        "#include <string.h>",
        "typedef struct { const char *name; int id; } GmItemNameId;",
        f"static const GmItemNameId gm_item_name_ids[{len(ordered)}] = {{",
    ]
    lines.extend(f'    {{"{name}", {item_id}}},' for name, item_id in ordered)
    lines.extend([
        "};",
        "static int gm_item_id_from_name_1_11_2(const char *name) {",
        "    size_t lo = 0, hi = sizeof gm_item_name_ids / sizeof gm_item_name_ids[0];",
        "    if (!name) return 0;",
        "    while (lo < hi) {",
        "        size_t mid = lo + (hi - lo) / 2;",
        "        int order = strcmp(name, gm_item_name_ids[mid].name);",
        "        if (order < 0) hi = mid;",
        "        else if (order > 0) lo = mid + 1;",
        "        else return gm_item_name_ids[mid].id;",
        "    }",
        "    return 0;",
        "}",
        "#endif",
        "",
    ])
    OUTPUT.write_text("\n".join(lines))
    print(f"item name manifest: {len(ordered)} registry rows")


if __name__ == "__main__":
    main()
