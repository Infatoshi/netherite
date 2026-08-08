#!/usr/bin/env python3
"""Generate the native static recipe table from the initialized 1.11.2 registry."""

from __future__ import annotations

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "verify" / "completeness" / "surface_registry_manifest.json"
HEADER = ROOT / "blaze" / "core" / "crafting_recipes_full.h"
CASES = ROOT / "magma" / "game" / "crafting_registry_cases.h"


PREAMBLE = r'''/* Generated from the initialized Minecraft 1.11.2 CraftingManager.
 * Source: verify/completeness/surface_registry_manifest.json.
 * The static table contains every ordinary shaped/shapeless vanilla and
 * Forge ore recipe in exact registry order. Stateful special recipes remain
 * in the runtime dispatcher because their result/remainder depends on NBT. */
#ifndef MC_CRAFTING_RECIPES_FULL_H
#define MC_CRAFTING_RECIPES_FULL_H
#include "mc.h"

#define CRF_WILDCARD 32767
typedef struct { i32 item; i32 count; i32 meta; } CRStack;
MC_HD static inline CRStack crf_empty(void) { CRStack s; s.item = 0; s.count = 0; s.meta = 0; return s; }
MC_HD static inline CRStack crf_mk(i32 item, i32 count, i32 meta) { CRStack s; s.item = item; s.count = count; s.meta = meta; return s; }
MC_HD static inline int crf_isEmpty(CRStack s) { return s.item == 0 || s.count <= 0; }
typedef struct { int shaped; int width, height; int nIng; CRStack ing[9]; CRStack output; } CRRecipe;
#define CRF_GRID 3
MC_HD static inline int crf_stack_matches(CRStack expected, CRStack actual) {
    return expected.item == actual.item
        && (expected.meta == CRF_WILDCARD || expected.meta == actual.meta);
}
MC_HD static inline int crf_checkMatch(const CRRecipe *r, const CRStack *grid, int offX, int offY, int mirror) {
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        int k = i - offX, l = j - offY; CRStack ing = crf_empty();
        if (k >= 0 && l >= 0 && k < r->width && l < r->height)
            ing = mirror ? r->ing[r->width - k - 1 + l * r->width] : r->ing[k + l * r->width];
        CRStack g = grid[i + j * CRF_GRID];
        if (!crf_isEmpty(g) || !crf_isEmpty(ing)) {
            if (crf_isEmpty(g) != crf_isEmpty(ing)) return 0;
            if (!crf_stack_matches(ing, g)) return 0;
        }
    }
    return 1;
}
MC_HD static inline int crf_shapedMatches(const CRRecipe *r, const CRStack *grid) {
    for (int i = 0; i <= 3 - r->width; ++i) for (int j = 0; j <= 3 - r->height; ++j) {
        if (crf_checkMatch(r, grid, i, j, 1)) return 1;
        if (crf_checkMatch(r, grid, i, j, 0)) return 1;
    }
    return 0;
}
MC_HD static inline int crf_shapelessMatches(const CRRecipe *r, const CRStack *grid) {
    int used[9]; for (int i = 0; i < r->nIng; ++i) used[i] = 0;
    int remaining = r->nIng;
    for (int i = 0; i < CRF_GRID; ++i) for (int j = 0; j < CRF_GRID; ++j) {
        CRStack g = grid[j + i * CRF_GRID];
        if (!crf_isEmpty(g)) {
            int found = 0;
            for (int z = 0; z < r->nIng; ++z) if (!used[z]
                    && crf_stack_matches(r->ing[z], g)) {
                found = 1; used[z] = 1; --remaining; break;
            }
            if (!found) return 0;
        }
    }
    return remaining == 0;
}
MC_HD static inline int crf_matches(const CRRecipe *r, const CRStack *grid) {
    return r->shaped ? crf_shapedMatches(r, grid) : crf_shapelessMatches(r, grid);
}
MC_HD static inline CRStack crf_findMatching(const CRRecipe *recipes, int n, const CRStack *grid) {
    for (int i = 0; i < n; ++i) if (crf_matches(&recipes[i], grid)) return recipes[i].output;
    return crf_mk((i32)0xffffffff, 0, 0);
}
'''


def stack_expr(ingredient: dict[str, object]) -> str:
    choices = ingredient["choices"]
    if not choices:
        return "crf_empty()"
    if len(choices) != 1:
        raise RuntimeError(
            "native CRRecipe needs alternatives for a multi-choice ingredient")
    stack = choices[0]
    return f"crf_mk({stack['item']},{stack['count']},{stack['meta']})"


def main() -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    recipes = [
        row for row in manifest["crafting_recipes"]
        if row["kind"] != "special"
    ]
    old = HEADER.read_text(encoding="utf-8")
    battery_marker = "MC_HD static inline void crf_battery"
    if battery_marker not in old:
        raise RuntimeError("existing crafting battery is missing")
    battery = battery_marker + old.split(battery_marker, 1)[1]
    battery = battery.rsplit("#endif", 1)[0].rstrip()

    lines = [PREAMBLE.rstrip(), "", f"#define CRF_NRECIPES {len(recipes)}",
             "#define CRF_NTESTS 54",
             "MC_HD static inline int crf_build(CRRecipe *R) {", "    int n = 0;"]
    for row in recipes:
        ingredients = row["ingredients"]
        shaped = row["kind"] in ("shaped", "shaped_ore")
        width = row["width"] if shaped else 0
        height = row["height"] if shaped else 0
        lines.append(
            f"    /* registry {row['index']}: {row['class']} */")
        lines.append(
            f"    R[n].shaped={1 if shaped else 0}; R[n].width={width}; "
            f"R[n].height={height}; R[n].nIng={len(ingredients)};")
        lines.append(
            f"    R[n].output=crf_mk({row['output_item']},"
            f"{row['output_count']},{row['output_meta']});")
        for index, ingredient in enumerate(ingredients):
            lines.append(f"    R[n].ing[{index}]={stack_expr(ingredient)};")
        lines.append("    ++n;")
    lines.extend(["    return n;", "}", "", battery, "", "#endif", ""])
    HEADER.write_text("\n".join(lines), encoding="utf-8")
    case_rows: list[tuple[int, str, list[dict[str, int] | None]]] = []
    for row in recipes:
        source = [
            ingredient["choices"][0] if ingredient["choices"] else None
            for ingredient in row["ingredients"]
        ]
        shaped = row["kind"] in ("shaped", "shaped_ore")

        def make_grid(offset_x: int, offset_y: int,
                      mirror: bool) -> list[dict[str, int] | None]:
            grid: list[dict[str, int] | None] = [None] * 9
            if shaped:
                for y in range(row["height"]):
                    for x in range(row["width"]):
                        source_x = row["width"] - x - 1 if mirror else x
                        grid[x + offset_x + (y + offset_y) * 3] = source[
                            source_x + y * row["width"]]
            else:
                values = list(reversed(source)) if mirror else source
                grid[:len(values)] = values
            return grid

        positive = make_grid(0, 0, False)
        case_rows.append((row["index"], "P", positive))
        if shaped and (row["width"] < 3 or row["height"] < 3):
            case_rows.append((row["index"], "O", make_grid(
                3 - row["width"], 3 - row["height"], False)))
        if ((shaped and row["width"] > 1)
                or (not shaped and len(source) > 1)):
            case_rows.append((row["index"], "M", make_grid(0, 0, True)))
        negative = list(positive)
        for index, stack in enumerate(negative):
            if stack is not None:
                negative[index] = None
                break
        case_rows.append((row["index"], "N", negative))

    case_lines = [
        "/* Generated canonical/offset/mirror/negative cases for every static recipe. */",
        "#ifndef MAGMA_CRAFTING_REGISTRY_CASES_H",
        "#define MAGMA_CRAFTING_REGISTRY_CASES_H",
        "#include \"crafting_recipes_full.h\"",
        "typedef struct { int registry_index; char variant; CRStack grid[9]; } CrfRegistryCase;",
        f"#define CRF_REGISTRY_NCASES {len(case_rows)}",
        "static const CrfRegistryCase crf_registry_cases[CRF_REGISTRY_NCASES] = {",
    ]
    for registry_index, variant, grid in case_rows:
        values = ", ".join(
            "{0,0,0}" if stack is None else
            f"{{{stack['item']},{stack['count']},{stack['meta']}}}"
            for stack in grid)
        case_lines.append(
            f"    {{{registry_index},'{variant}',{{{values}}}}},")
    case_lines.extend(["};", "#endif", ""])
    CASES.write_text("\n".join(case_lines), encoding="utf-8")
    specials = len(manifest["crafting_recipes"]) - len(recipes)
    print(
        f"generated {len(recipes)} static recipes, {len(case_rows)} cases; "
        f"{specials} special")


if __name__ == "__main__":
    main()
