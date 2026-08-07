#!/usr/bin/env python3
"""Pack VillageTemplateGolden output into a compact C data header."""

from __future__ import annotations

import argparse
from pathlib import Path


def parse(path: Path):
    templates = []
    current = None
    for raw in path.read_text().splitlines():
        fields = raw.split()
        if not fields:
            continue
        if fields[0] == "T":
            if current is not None and len(current["cells"]) != current["count"]:
                raise ValueError("cell count mismatch")
            values = [int(value) for value in fields[1:]]
            if len(values) != 8:
                raise ValueError(f"bad template header: {raw}")
            kind, variant, facing, sx, sy, sz, source_y, count = values
            current = dict(kind=kind, variant=variant, facing=facing,
                           sx=sx, sy=sy, sz=sz, source_y=source_y,
                           count=count, cells=[])
            templates.append(current)
        elif fields[0] == "B":
            if current is None:
                raise ValueError("cell before template")
            values = tuple(int(value) for value in fields[1:])
            if len(values) != 4:
                raise ValueError(f"bad cell: {raw}")
            current["cells"].append(values)
    if current is not None and len(current["cells"]) != current["count"]:
        raise ValueError("final cell count mismatch")
    keys = {(t["kind"], t["variant"], t["facing"]) for t in templates}
    if len(templates) != 68 or len(keys) != 68:
        raise ValueError(f"expected 68 unique templates, got {len(templates)}/{len(keys)}")
    return templates


def generate(templates) -> str:
    cells = []
    descriptors = []
    for template in templates:
        first = len(cells)
        cells.extend(template["cells"])
        descriptors.append((template, first))
    out = [
        "/* Generated from the owned 1.11.2 client by build_village_templates.py. */",
        "#ifndef MAGMA_ASSETS_VILLAGE_TEMPLATES_H",
        "#define MAGMA_ASSETS_VILLAGE_TEMPLATES_H",
        "#include <stdint.h>",
        "typedef struct { int8_t x,y,z; uint16_t state; } GmVillageTemplateCell;",
        "typedef struct {",
        "    uint32_t first; uint16_t count;",
        "    uint8_t kind,variant,facing,sx,sy,sz;",
        "} GmVillageTemplate;",
        "static const GmVillageTemplateCell gm_village_template_cells[] = {",
    ]
    for index in range(0, len(cells), 6):
        row = cells[index:index + 6]
        out.append("    " + ", ".join(
            "{%d,%d,%d,%d}" % cell for cell in row) + ",")
    out.extend([
        "};",
        "static const GmVillageTemplate gm_village_templates[] = {",
    ])
    for template, first in descriptors:
        out.append("    {%d,%d,%d,%d,%d,%d,%d,%d}," % (
            first, len(template["cells"]), template["kind"],
            template["variant"], template["facing"], template["sx"],
            template["sy"], template["sz"]))
    out.extend([
        "};",
        "enum { GM_VILLAGE_TEMPLATE_COUNT = 68 };",
        "#endif",
        "",
    ])
    return "\n".join(out)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(generate(parse(args.input)))


if __name__ == "__main__":
    main()
