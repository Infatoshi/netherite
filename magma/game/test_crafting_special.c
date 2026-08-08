#include "game/crafting_special.h"

#include <stdio.h>
#include <stdlib.h>

/* The matcher only needs the cold ItemStack tag table. Keeping this candidate
 * independent of the full game makes the exhaustive registry gate cheap. */
int gm_runtime_stack_tag_intern(
        GmRuntime *r, const void *data, size_t len) {
    GmNbtBlob candidate = {0};
    for (int i = 0; i < r->stack_tag_count; ++i)
        if (r->stack_tags[i].tag.len == len
                && !memcmp(r->stack_tags[i].tag.data, data, len))
            return i + 1;
    if (!gm_nbt_blob_set(&candidate, data, len)) return 0;
    GmRuntimeStackTag *grown = realloc(r->stack_tags,
        (size_t)(r->stack_tag_count + 1) * sizeof *grown);
    if (!grown) { gm_nbt_blob_clear(&candidate); return 0; }
    r->stack_tags = grown;
    r->stack_tags[r->stack_tag_count].tag = candidate;
    return ++r->stack_tag_count;
}

const GmNbtBlob *gm_runtime_stack_tag(const GmRuntime *r, int id) {
    return r && id > 0 && id <= r->stack_tag_count
        ? &r->stack_tags[id - 1].tag : NULL;
}

const GmRuntimeMapData *gm_runtime_map_data_ref(
        const GmRuntime *r, int map_id) {
    if (!r) return NULL;
    for (int i = 0; i < r->map_data_cap; ++i)
        if (r->map_data[i].active && r->map_data[i].map_id == map_id)
            return &r->map_data[i];
    return NULL;
}

int gm_runtime_map_data_set(
        GmRuntime *r, int map_id, int scale, int exploration) {
    GmRuntimeMapData *grown;
    if (!r || map_id < 0 || scale < 0 || scale > 4) return 0;
    for (int i = 0; i < r->map_data_cap; ++i)
        if (r->map_data[i].active && r->map_data[i].map_id == map_id) {
            r->map_data[i].scale = scale;
            r->map_data[i].has_exploration_marker = exploration;
            return 1;
        }
    grown = realloc(r->map_data,
        (size_t)(r->map_data_cap + 1) * sizeof *grown);
    if (!grown) return 0;
    r->map_data = grown;
    r->map_data[r->map_data_cap++] = (GmRuntimeMapData){
        1, map_id, scale, exploration
    };
    return 1;
}

static void put(ICStack grid[9], int slot, int item, int meta) {
    grid[slot] = ic_mk(item, 1, meta);
}

static int tagged(GmRuntime *r, ICStack *stack, GmNbtBlob *tag) {
    stack->tag_id = gm_runtime_stack_tag_intern(r, tag->data, tag->len);
    gm_nbt_blob_clear(tag);
    return stack->tag_id > 0;
}

static void print_tag(const GmRuntime *r, const ICStack *stack) {
    static const unsigned char empty[] = {10, 0, 0, 0};
    const GmNbtBlob *tag = gm_runtime_stack_tag(r, stack->tag_id);
    const unsigned char *data = tag ? tag->data : empty;
    size_t length = tag ? tag->len : sizeof empty;
    for (size_t i = 0; i < length; ++i) printf("%02x", data[i]);
}

static void print_result(GmRuntime *r, const char *name, int registry,
        const GmCraftSpecial *match) {
    printf("S %s %d %d:%d:%d repair=%d tag=", name, registry,
           match->output.item, match->output.count, match->output.meta,
           match->output.repair_cost);
    print_tag(r, &match->output);
    for (int i = 0; i < 9; ++i)
        printf(" %d:%d:%d", match->remainder[i].item,
               match->remainder[i].count, match->remainder[i].meta);
    putchar('\n');
}

static int run(GmRuntime *r, const char *name, int registry,
        ICStack grid[9]) {
    GmCraftSpecial match;
    ICStack mutated[9];
    int first = -1, empty = -1;
    char edge_name[96];
    if (gm_crafting_special_match(r, grid, &match) <= 0) {
        fprintf(stderr, "special recipe did not match: %s\n", name);
        return 0;
    }
    print_result(r, name, registry, &match);
    for (int i = 0; i < 9; ++i) {
        if (cc_is_empty(&grid[i])) {
            if (empty < 0) empty = i;
        } else if (first < 0) first = i;
    }
    memcpy(mutated, grid, sizeof mutated);
    mutated[first] = ic_empty();
    snprintf(edge_name, sizeof edge_name, "%s_edge_remove", name);
    int result = gm_crafting_special_match_registry(
        r, registry, mutated, &match);
    if (result < 0) return 0;
    print_result(r, edge_name, result > 0 ? registry : -1, &match);

    memcpy(mutated, grid, sizeof mutated);
    mutated[first] = ic_mk(1, 1, 0);
    snprintf(edge_name, sizeof edge_name, "%s_edge_replace", name);
    result = gm_crafting_special_match_registry(
        r, registry, mutated, &match);
    if (result < 0) return 0;
    print_result(r, edge_name, result > 0 ? registry : -1, &match);

    memcpy(mutated, grid, sizeof mutated);
    mutated[empty >= 0 ? empty : 8] = ic_mk(1, 1, 0);
    snprintf(edge_name, sizeof edge_name, "%s_edge_extra", name);
    result = gm_crafting_special_match_registry(
        r, registry, mutated, &match);
    if (result < 0) return 0;
    print_result(r, edge_name, result > 0 ? registry : -1, &match);
    return 1;
}

static int run_invalid(GmRuntime *r, const char *name, int registry,
        ICStack grid[9]) {
    GmCraftSpecial match;
    int result = gm_crafting_special_match_registry(
        r, registry, grid, &match);
    if (result != 0) {
        fprintf(stderr, "invalid special recipe matched: %s\n", name);
        return 0;
    }
    print_result(r, name, -1, &match);
    return 1;
}

static void clear_grid(ICStack grid[9]) {
    for (int i = 0; i < 9; ++i) grid[i] = ic_empty();
}

int main(void) {
    GmRuntime runtime;
    ICStack grid[9];
    GmNbtBlob tag = {0}, child = {0}, pattern = {0};
    const char *pages[] = {"{\"text\":\"A\"}"};
    memset(&runtime, 0, sizeof runtime);

    clear_grid(grid);
    for (int i = 0; i < 9; ++i) put(grid, i, i == 4 ? 358 : 339, 0);
    if (!gm_runtime_map_data_set(&runtime, 0, 1, 0)
            || !run(&runtime, "map_extend", 136, grid)
            || !gm_runtime_map_data_set(&runtime, 0, 4, 0)
            || !run_invalid(&runtime, "map_extend_scale4", 136, grid)
            || !gm_runtime_map_data_set(&runtime, 0, 1, 1)
            || !run_invalid(&runtime, "map_extend_mansion", 136, grid)
            || !run_invalid(&runtime, "map_extend_monument", 136, grid))
        return 1;
    put(grid, 4, 358, 30000);
    if (!run_invalid(&runtime, "map_extend_missing_data", 136, grid))
        return 1;

    clear_grid(grid); put(grid, 0, 299, 0); put(grid, 1, 351, 1);
    put(grid, 2, 351, 11);
    if (!gm_nbt_blob_make_empty(&tag)
            || !gm_nbt_blob_make_empty(&child)
            || !gm_nbt_blob_set_int(&child, "color", 0x204060)
            || !gm_nbt_blob_set_compound(&tag, "display", &child)
            || !tagged(&runtime, &grid[0], &tag)
            || !run(&runtime, "armor_dye", 329, grid)) return 1;
    gm_nbt_blob_clear(&child);

    clear_grid(grid); put(grid, 0, 299, 0); put(grid, 1, 351, 11);
    if (!run(&runtime, "armor_dye_uncolored", 329, grid)) return 1;

    for (int dye = 0; dye < 16; ++dye) {
        char name[32];
        clear_grid(grid); put(grid, 0, 299, 37); put(grid, 8, 351, dye);
        if (!gm_nbt_blob_make_empty(&tag)
                || !gm_nbt_blob_make_empty(&child)
                || !gm_nbt_blob_set_int(&child, "color", 0x204060)
                || !gm_nbt_blob_set_compound(&tag, "display", &child)
                || !tagged(&runtime, &grid[0], &tag)) return 1;
        gm_nbt_blob_clear(&child);
        snprintf(name, sizeof name, "armor_dye_%d", dye);
        if (!run(&runtime, name, 329, grid)) return 1;
    }

    clear_grid(grid); put(grid, 0, 339, 0); put(grid, 1, 289, 0);
    put(grid, 2, 289, 0);
    if (!run(&runtime, "firework", 330, grid)) return 1;

    clear_grid(grid); put(grid, 0, 289, 0); put(grid, 1, 351, 1);
    put(grid, 2, 264, 0); put(grid, 3, 264, 0);
    put(grid, 4, 348, 0); put(grid, 5, 348, 0);
    put(grid, 6, 288, 0);
    if (!run(&runtime, "firework_star_repeat_modifiers", 330, grid))
        return 1;

    {
        const int32_t red[] = {11743532};
        clear_grid(grid); put(grid, 0, 402, 0); put(grid, 8, 351, 4);
        if (!gm_nbt_blob_make_empty(&child)
                || !gm_nbt_blob_set_byte(&child, "Type", 3)
                || !gm_nbt_blob_set_int_array(&child, "Colors", red, 1)
                || !gm_nbt_blob_make_empty(&tag)
                || !gm_nbt_blob_set_compound(&tag, "Explosion", &child)
                || !tagged(&runtime, &grid[0], &tag)
                || !run(&runtime, "firework_fade", 330, grid)) return 1;
        gm_nbt_blob_clear(&child);

        clear_grid(grid); put(grid, 0, 339, 0);
        put(grid, 1, 289, 0); put(grid, 2, 289, 0);
        put(grid, 3, 289, 0); put(grid, 4, 402, 0);
        put(grid, 5, 402, 0);
        for (int star = 4; star <= 5; ++star) {
            if (!gm_nbt_blob_make_empty(&child)
                    || !gm_nbt_blob_set_byte(
                        &child, "Type", (int8_t)(star == 4 ? 3 : 4))
                    || !gm_nbt_blob_set_int_array(
                        &child, "Colors", red, 1)
                    || !gm_nbt_blob_make_empty(&tag)
                    || !gm_nbt_blob_set_compound(
                        &tag, "Explosion", &child)
                    || !tagged(&runtime, &grid[star], &tag)) return 1;
            gm_nbt_blob_clear(&child);
        }
        if (!run(&runtime, "firework_rocket_stars", 330, grid)) return 1;
    }

    for (int p = 0; p < (int)(sizeof gcs_banner_patterns
            / sizeof gcs_banner_patterns[0]); ++p) {
        char name[64];
        clear_grid(grid);
        if (gcs_banner_patterns[p].shape) {
            int banner_slot = -1;
            for (int i = 0; i < 9; ++i) {
                if (gcs_banner_patterns[p].shape[i] == '#')
                    put(grid, i, 351, 14);
                else if (banner_slot < 0)
                    banner_slot = i;
            }
            if (banner_slot < 0) return 1;
            put(grid, banner_slot, 425, 0);
        } else {
            put(grid, 0, 425, 0); put(grid, 1, 351, 14);
            put(grid, 2, gcs_banner_patterns[p].item,
                gcs_banner_patterns[p].meta);
        }
        snprintf(name, sizeof name, "banner_pattern_%s",
            gcs_banner_patterns[p].hash);
        if (!run(&runtime, name, 331, grid)) return 1;
    }

    clear_grid(grid);
    for (int i = 0; i < 9; ++i) put(grid, i, 262, 0);
    put(grid, 4, 441, 0);
    if (!gm_nbt_blob_make_empty(&tag)
            || !gm_nbt_blob_set_string(
                &tag, "Potion", "minecraft:strong_poison")
            || !tagged(&runtime, &grid[4], &tag)
            || !run(&runtime, "tipped_arrow", 332, grid)) return 1;

    clear_grid(grid); put(grid, 0, 358, 0); put(grid, 1, 395, 0);
    put(grid, 2, 395, 0);
    if (!run(&runtime, "map_clone", 333, grid)) return 1;

    clear_grid(grid); put(grid, 0, 387, 0); put(grid, 1, 386, 0);
    put(grid, 2, 386, 0);
    if (!gm_nbt_blob_make_empty(&tag)
            || !gm_nbt_blob_set_string(&tag, "title", "Parity")
            || !gm_nbt_blob_set_string(&tag, "author", "Oracle")
            || !gm_nbt_blob_set_int(&tag, "generation", 1)
            || !gm_nbt_blob_set_string_list(&tag, "pages", pages, 1)
            || !tagged(&runtime, &grid[0], &tag)
            || !run(&runtime, "book_clone", 334, grid)) return 1;

    clear_grid(grid); put(grid, 0, 267, 190); put(grid, 1, 267, 210);
    if (!run(&runtime, "repair", 335, grid)) return 1;

    for (int item = 1; item <= 2267; ++item) {
        int maximum = gcs_max_damage(item);
        char name[32];
        if (maximum <= 0) continue;
        clear_grid(grid);
        put(grid, 0, item, maximum / 3);
        put(grid, 8, item, maximum * 2 / 3);
        snprintf(name, sizeof name, "repair_%d", item);
        if (!run(&runtime, name, 335, grid)) return 1;
    }

    if (!gm_nbt_blob_make_empty(&pattern)
            || !gm_nbt_blob_set_string(&pattern, "Pattern", "cre")
            || !gm_nbt_blob_set_int(&pattern, "Color", 14)
            || !gm_nbt_blob_make_empty(&child)
            || !gm_nbt_blob_set_int(&child, "Base", 0)
            || !gm_nbt_blob_append_compound_list(
                &child, "Patterns", &pattern)
            || !gm_nbt_blob_make_empty(&tag)
            || !gm_nbt_blob_set_compound(&tag, "BlockEntityTag", &child))
        return 1;

    clear_grid(grid); put(grid, 0, 442, 0); put(grid, 1, 425, 0);
    if (!tagged(&runtime, &grid[1], &tag)
            || !run(&runtime, "shield_decor", 347, grid)) return 1;

    clear_grid(grid); put(grid, 0, 425, 0); put(grid, 1, 425, 0);
    if (!gm_nbt_blob_make_empty(&tag)
            || !gm_nbt_blob_set_compound(&tag, "BlockEntityTag", &child)
            || !tagged(&runtime, &grid[0], &tag)
            || !run(&runtime, "banner_duplicate", 348, grid)) return 1;

    for (int dye = 0; dye < 16; ++dye) {
        char name[32];
        clear_grid(grid); put(grid, 0, 219 + dye, 0);
        put(grid, 8, 351, dye);
        if (!gm_nbt_blob_make_empty(&child)
                || !gm_nbt_blob_set_string(
                    &child, "CustomName", "Parity Box")
                || !gm_nbt_blob_make_empty(&tag)
                || !gm_nbt_blob_set_compound(
                    &tag, "BlockEntityTag", &child)
                || !tagged(&runtime, &grid[0], &tag)) return 1;
        gm_nbt_blob_clear(&child);
        snprintf(name, sizeof name, "shulker_color_%d", dye);
        if (!run(&runtime, name, 399, grid)) return 1;
    }

    gm_nbt_blob_clear(&pattern); gm_nbt_blob_clear(&child);
    for (int i = 0; i < runtime.stack_tag_count; ++i)
        gm_nbt_blob_clear(&runtime.stack_tags[i].tag);
    free(runtime.stack_tags);
    return 0;
}
