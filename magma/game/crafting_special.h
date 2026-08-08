#ifndef MAGMA_CRAFTING_SPECIAL_H
#define MAGMA_CRAFTING_SPECIAL_H

#include "game/runtime.h"
#include "container_click.h"
#include "items_tools_armor.h"

#include <string.h>

typedef struct {
    int matched;
    ICStack output;
    ICStack remainder[9];
} GmCraftSpecial;

static int gcs_nonempty(const ICStack grid[9]) {
    int count = 0;
    for (int i = 0; i < 9; ++i) count += !cc_is_empty(&grid[i]);
    return count;
}

static int gcs_copy_tag(
        GmRuntime *r, const ICStack *source, GmNbtBlob *out) {
    const GmNbtBlob *tag;
    if (!source || source->tag_id == 0) return gm_nbt_blob_make_empty(out);
    tag = gm_runtime_stack_tag(r, source->tag_id);
    return tag && gm_nbt_blob_copy(out, tag);
}

static int gcs_finish_tag(
        GmRuntime *r, ICStack *output, GmNbtBlob *tag) {
    int id = gm_runtime_stack_tag_intern(r, tag->data, tag->len);
    gm_nbt_blob_clear(tag);
    if (!id) return 0;
    output->tag_id = id;
    return 1;
}

static int gcs_max_damage(int item) {
    ITAStack value = ita_mk(item, 0);
    int result = ita_stack_max_damage(&value);
    if (result > 0) return result;
    if (item == 261) return 384;
    if (item == 259) return 64;
    if (item == 442) return 336;
    return 0;
}

static int gcs_map_extend(
        GmRuntime *r, const ICStack grid[9], GmCraftSpecial *out) {
    int map_slot = -1;
    for (int i = 0; i < 9; ++i) {
        if (i == 4 && grid[i].item == 358 && grid[i].count > 0)
            map_slot = i;
        else if (grid[i].item != 339 || grid[i].count <= 0)
            return 0;
    }
    if (map_slot < 0) return 0;
    const GmRuntimeMapData *map = gm_runtime_map_data_ref(
        r, grid[map_slot].meta);
    if (!map || map->scale >= 4 || map->has_exploration_marker) return 0;
    GmNbtBlob tag = {0};
    out->output = grid[map_slot]; out->output.count = 1;
    if (!gcs_copy_tag(r, &grid[map_slot], &tag)
            || !gm_nbt_blob_set_int(&tag, "map_scale_direction", 1)
            || !gcs_finish_tag(r, &out->output, &tag)) {
        gm_nbt_blob_clear(&tag); return -1;
    }
    return 1;
}

static const float gcs_dye_rgb[16][3] = {
    {0.1F, 0.1F, 0.1F}, {0.6F, 0.2F, 0.2F},
    {0.4F, 0.5F, 0.2F}, {0.4F, 0.3F, 0.2F},
    {0.2F, 0.3F, 0.7F}, {0.5F, 0.25F, 0.7F},
    {0.3F, 0.5F, 0.6F}, {0.6F, 0.6F, 0.6F},
    {0.3F, 0.3F, 0.3F}, {0.95F, 0.5F, 0.65F},
    {0.5F, 0.8F, 0.1F}, {0.9F, 0.9F, 0.2F},
    {0.4F, 0.6F, 0.85F}, {0.7F, 0.3F, 0.85F},
    {0.85F, 0.5F, 0.2F}, {1.0F, 1.0F, 1.0F},
};

static int gcs_armor_dye(
        GmRuntime *r, const ICStack grid[9], GmCraftSpecial *out) {
    int armor_slot = -1, colors[3] = {0}, brightness = 0, count = 0;
    for (int i = 0; i < 9; ++i) {
        const ICStack *stack = &grid[i];
        if (cc_is_empty(stack)) continue;
        if (stack->item >= 298 && stack->item <= 301) {
            int32_t color;
            double raw;
            GmNbtBlob display = {0};
            if (armor_slot >= 0) return 0;
            armor_slot = i;
            if (stack->tag_id > 0) {
                const GmNbtBlob *tag = gm_runtime_stack_tag(r, stack->tag_id);
                if (tag && gm_nbt_blob_extract_compound(
                        tag, "display", &display)
                        && gm_nbt_blob_find_number(
                            &display, "color", &raw, NULL)) {
                    color = (int32_t)raw;
                    float rf = (float)((color >> 16) & 255) / 255.0F;
                    float gf = (float)((color >> 8) & 255) / 255.0F;
                    float bf = (float)(color & 255) / 255.0F;
                    int red = (int)(rf * 255.0F);
                    int green = (int)(gf * 255.0F);
                    int blue = (int)(bf * 255.0F);
                    colors[0] += red; colors[1] += green; colors[2] += blue;
                    float maximum = rf > gf
                        ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf);
                    brightness += (int)(maximum * 255.0F);
                    ++count;
                }
                gm_nbt_blob_clear(&display);
            }
        } else if (stack->item == 351 && stack->meta >= 0
                && stack->meta < 16) {
            int red = (int)(gcs_dye_rgb[stack->meta][0] * 255.0F);
            int green = (int)(gcs_dye_rgb[stack->meta][1] * 255.0F);
            int blue = (int)(gcs_dye_rgb[stack->meta][2] * 255.0F);
            colors[0] += red; colors[1] += green; colors[2] += blue;
            brightness += red > green
                ? (red > blue ? red : blue)
                : (green > blue ? green : blue);
            ++count;
        } else return 0;
    }
    /* j counts dyes plus the armor's existing color only when it has one.
     * An uncolored leather piece with one dye therefore has j==1. */
    if (armor_slot < 0 || count < 1) return 0;
    int red = colors[0] / count, green = colors[1] / count;
    int blue = colors[2] / count;
    float average = (float)brightness / (float)count;
    int maximum = red > green ? (red > blue ? red : blue)
                              : (green > blue ? green : blue);
    if (maximum > 0) {
        float maximum_f = (float)maximum;
        red = (int)((float)red * average / maximum_f);
        green = (int)((float)green * average / maximum_f);
        blue = (int)((float)blue * average / maximum_f);
    }
    GmNbtBlob tag = {0}, display = {0};
    out->output = grid[armor_slot]; out->output.count = 1;
    if (!gcs_copy_tag(r, &grid[armor_slot], &tag)
            || (!gm_nbt_blob_extract_compound(&tag, "display", &display)
                && !gm_nbt_blob_make_empty(&display))
            || !gm_nbt_blob_set_int(
                &display, "color", (red << 16) | (green << 8) | blue)
            || !gm_nbt_blob_set_compound(&tag, "display", &display)
            || !gcs_finish_tag(r, &out->output, &tag)) {
        gm_nbt_blob_clear(&display); gm_nbt_blob_clear(&tag); return -1;
    }
    gm_nbt_blob_clear(&display);
    return 1;
}

static int gcs_firework(
        GmRuntime *r, const ICStack grid[9], GmCraftSpecial *out) {
    static const int32_t dye_colors[16] = {
        1973019, 11743532, 3887386, 5320730,
        2437522, 8073150, 2651799, 11250603,
        4408131, 14188952, 4312372, 14602026,
        6719955, 12801229, 15435844, 15790320,
    };
    int paper = 0, powder = 0, stars = 0, dyes = 0;
    int diamond = 0, glowstone = 0, shape = 0, shape_type = 0, other = 0;
    int star_slot = -1;
    int32_t colors[9];
    for (int i = 0; i < 9; ++i) if (!cc_is_empty(&grid[i])) {
        if (grid[i].item == 339) ++paper;
        else if (grid[i].item == 289) ++powder;
        else if (grid[i].item == 402) { ++stars; star_slot = i; }
        else if (grid[i].item == 351 && grid[i].meta >= 0
                && grid[i].meta < 16) colors[dyes++] = dye_colors[grid[i].meta];
        else if (grid[i].item == 264) ++diamond;
        else if (grid[i].item == 348) ++glowstone;
        else if (grid[i].item == 385) { ++shape; shape_type = 1; }
        else if (grid[i].item == 371) { ++shape; shape_type = 2; }
        else if (grid[i].item == 397) { ++shape; shape_type = 3; }
        else if (grid[i].item == 288) { ++shape; shape_type = 4; }
        else ++other;
    }
    /* RecipeFireworks accepts repeated diamond/glowstone modifiers; they
     * remain one boolean Trail/Flicker tag. Only shape ingredients are
     * mutually exclusive. */
    if (other || shape > 1) return 0;
    if (paper == 1 && powder >= 1 && powder <= 3 && dyes == 0
            && diamond == 0 && glowstone == 0 && shape == 0) {
        GmNbtBlob tag = {0}, fireworks = {0};
        out->output = ic_mk(401, 3, 0);
        if (!gm_nbt_blob_make_empty(&tag)
                || !gm_nbt_blob_make_empty(&fireworks)
                || !gm_nbt_blob_set_byte(
                    &fireworks, "Flight", (int8_t)powder)) {
            gm_nbt_blob_clear(&fireworks); gm_nbt_blob_clear(&tag); return -1;
        }
        for (int i = 0; i < 9; ++i) if (grid[i].item == 402) {
            const GmNbtBlob *star = gm_runtime_stack_tag(r, grid[i].tag_id);
            GmNbtBlob explosion = {0};
            int have_explosion = star && gm_nbt_blob_extract_compound(
                star, "Explosion", &explosion);
            if (!have_explosion && ic_firework_explosions(&grid[i]) > 0) {
                have_explosion = gm_nbt_blob_make_empty(&explosion)
                    && gm_nbt_blob_set_byte(&explosion, "Type",
                        (int8_t)(ic_firework_large(&grid[i]) ? 1 : 0))
                    && (!ic_firework_flicker(&grid[i])
                        || gm_nbt_blob_set_byte(
                            &explosion, "Flicker", 1));
            }
            if (have_explosion) {
                if (!gm_nbt_blob_append_compound_list(
                        &fireworks, "Explosions", &explosion)) {
                    gm_nbt_blob_clear(&explosion);
                    gm_nbt_blob_clear(&fireworks);
                    gm_nbt_blob_clear(&tag); return -1;
                }
                gm_nbt_blob_clear(&explosion);
            }
        }
        if (!gm_nbt_blob_set_compound(&tag, "Fireworks", &fireworks)
                || !gcs_finish_tag(r, &out->output, &tag)) {
            gm_nbt_blob_clear(&fireworks); gm_nbt_blob_clear(&tag); return -1;
        }
        gm_nbt_blob_clear(&fireworks);
        return 1;
    }
    if (paper == 0 && powder == 1 && stars == 0 && dyes > 0) {
        GmNbtBlob tag = {0}, explosion = {0};
        out->output = ic_mk(402, 1, 0);
        if (!gm_nbt_blob_make_empty(&tag)
                || !gm_nbt_blob_make_empty(&explosion)
                || !gm_nbt_blob_set_int_array(
                    &explosion, "Colors", colors, (size_t)dyes)
                || !gm_nbt_blob_set_byte(
                    &explosion, "Type", (int8_t)shape_type)
                || (diamond && !gm_nbt_blob_set_byte(
                    &explosion, "Trail", 1))
                || (glowstone && !gm_nbt_blob_set_byte(
                    &explosion, "Flicker", 1))
                || !gm_nbt_blob_set_compound(&tag, "Explosion", &explosion)
                || !gcs_finish_tag(r, &out->output, &tag)) {
            gm_nbt_blob_clear(&explosion); gm_nbt_blob_clear(&tag); return -1;
        }
        gm_nbt_blob_clear(&explosion);
        return 1;
    }
    if (paper == 0 && powder == 0 && stars == 1 && dyes > 0
            && diamond == 0 && glowstone == 0 && shape == 0
            && star_slot >= 0 && grid[star_slot].tag_id > 0) {
        const GmNbtBlob *source = gm_runtime_stack_tag(
            r, grid[star_slot].tag_id);
        GmNbtBlob tag = {0}, explosion = {0};
        if (!source || !gm_nbt_blob_copy(&tag, source)
                || !gm_nbt_blob_extract_compound(
                    &tag, "Explosion", &explosion)) {
            gm_nbt_blob_clear(&explosion); gm_nbt_blob_clear(&tag); return 0;
        }
        out->output = grid[star_slot]; out->output.count = 1;
        if (!gm_nbt_blob_set_int_array(
                    &explosion, "FadeColors", colors, (size_t)dyes)
                || !gm_nbt_blob_set_compound(&tag, "Explosion", &explosion)
                || !gcs_finish_tag(r, &out->output, &tag)) {
            gm_nbt_blob_clear(&explosion); gm_nbt_blob_clear(&tag); return -1;
        }
        gm_nbt_blob_clear(&explosion);
        return 1;
    }
    return 0;
}

static int gcs_tipped_arrow(
        GmRuntime *r, const ICStack grid[9], GmCraftSpecial *out) {
    (void)r;
    if (grid[4].item != 441 || grid[4].count <= 0) return 0;
    for (int i = 0; i < 9; ++i)
        if (i != 4 && (grid[i].item != 262 || grid[i].count <= 0))
            return 0;
    out->output = grid[4]; out->output.item = 440; out->output.count = 8;
    return 1;
}

static int gcs_map_clone(const ICStack grid[9], GmCraftSpecial *out) {
    int map = -1, blanks = 0;
    for (int i = 0; i < 9; ++i) if (!cc_is_empty(&grid[i])) {
        if (grid[i].item == 358 && map < 0) map = i;
        else if (grid[i].item == 395) blanks += grid[i].count > 0;
        else return 0;
    }
    if (map < 0 || blanks < 1) return 0;
    out->output = grid[map]; out->output.count = blanks + 1;
    return 1;
}

static int gcs_book_clone(
        GmRuntime *r, const ICStack grid[9], GmCraftSpecial *out) {
    int book = -1, blanks = 0;
    for (int i = 0; i < 9; ++i) if (!cc_is_empty(&grid[i])) {
        if (grid[i].item == 387 && book < 0) book = i;
        else if (grid[i].item == 386) ++blanks;
        else return 0;
    }
    if (book < 0 || blanks < 1 || grid[book].tag_id <= 0) return 0;
    const GmNbtBlob *source = gm_runtime_stack_tag(r, grid[book].tag_id);
    double generation = 0.0;
    (void)gm_nbt_blob_find_number(source, "generation", &generation, NULL);
    if ((int)generation >= 2) return 0;
    GmNbtBlob tag = {0};
    out->output = grid[book]; out->output.count = blanks;
    out->remainder[book] = grid[book]; out->remainder[book].count = 1;
    if (!gm_nbt_blob_copy(&tag, source)
            || !gm_nbt_blob_set_int(
                &tag, "generation", (int32_t)generation + 1)
            || !gcs_finish_tag(r, &out->output, &tag)) {
        gm_nbt_blob_clear(&tag); return -1;
    }
    return 1;
}

static int gcs_repair(const ICStack grid[9], GmCraftSpecial *out) {
    int slots[2], count = 0;
    for (int i = 0; i < 9; ++i) if (!cc_is_empty(&grid[i])) {
        if (count >= 2) return 0;
        slots[count++] = i;
    }
    if (count != 2 || grid[slots[0]].count != 1
            || grid[slots[1]].count != 1
            || grid[slots[0]].item != grid[slots[1]].item)
        return 0;
    int maximum = gcs_max_damage(grid[slots[0]].item);
    if (maximum <= 0) return 0;
    int remaining = maximum - grid[slots[0]].meta
        + maximum - grid[slots[1]].meta + maximum * 5 / 100;
    int damage = maximum - remaining;
    if (damage < 0) damage = 0;
    out->output = ic_mk(grid[slots[0]].item, 1, damage);
    return 1;
}

typedef struct {
    const char *hash;
    const char *shape;
    int item, meta;
} GcsBannerPatternRecipe;

static const GcsBannerPatternRecipe gcs_banner_patterns[] = {
        {"bl", "      #  ", 0, 0}, {"br", "        #", 0, 0},
        {"tl", "#        ", 0, 0}, {"tr", "  #      ", 0, 0},
        {"bs", "      ###", 0, 0}, {"ts", "###      ", 0, 0},
        {"ls", "#  #  #  ", 0, 0}, {"rs", "  #  #  #", 0, 0},
        {"cs", " #  #  # ", 0, 0}, {"ms", "   ###   ", 0, 0},
        {"drs", "#   #   #", 0, 0}, {"dls", "  # # #  ", 0, 0},
        {"ss", "# ## #   ", 0, 0}, {"cr", "# # # # #", 0, 0},
        {"sc", " # ### # ", 0, 0}, {"bt", "    # # #", 0, 0},
        {"tt", "# # #    ", 0, 0}, {"bts", "   # # # ", 0, 0},
        {"tts", " # # #   ", 0, 0}, {"ld", "## #     ", 0, 0},
        {"rd", "     # ##", 0, 0}, {"lud", "   #  ## ", 0, 0},
        {"rud", " ##  #   ", 0, 0}, {"mc", "    #    ", 0, 0},
        {"mr", " # # # # ", 0, 0}, {"vh", "## ## ## ", 0, 0},
        {"hh", "######   ", 0, 0}, {"vhr", " ## ## ##", 0, 0},
        {"hhb", "   ######", 0, 0}, {"bo", "#### ####", 0, 0},
        {"cbo", NULL, 106, 0}, {"cre", NULL, 397, 4},
        {"gra", "# # #  # ", 0, 0}, {"gru", " #  # # #", 0, 0},
        {"bri", NULL, 45, 0}, {"sku", NULL, 397, 1},
        {"flo", NULL, 38, 8}, {"moj", NULL, 322, 1},
};

static int gcs_banner_add(
        GmRuntime *r, const ICStack grid[9], GmCraftSpecial *out) {
    int banner = -1, dye = -1, matched = -1;
    for (int i = 0; i < 9; ++i) if (!cc_is_empty(&grid[i])) {
        if (grid[i].item == 425 && banner < 0) banner = i;
        else if (grid[i].item == 425) return 0;
    }
    if (banner < 0) return 0;
    if (grid[banner].tag_id > 0) {
        const GmNbtBlob *tag = gm_runtime_stack_tag(
            r, grid[banner].tag_id);
        GmNbtBlob block = {0}; size_t count = 0; int type = 0;
        if (tag && gm_nbt_blob_extract_compound(
                tag, "BlockEntityTag", &block)
                && gm_nbt_blob_find_list_info(
                    &block, "Patterns", &count, &type)
                && type == 10 && count >= 6) {
            gm_nbt_blob_clear(&block);
            return 0;
        }
        gm_nbt_blob_clear(&block);
    }
    for (int p = 0; p < (int)(sizeof gcs_banner_patterns
            / sizeof gcs_banner_patterns[0]); ++p) {
        int candidate_dye = -1, candidate_item = -1, ok = 1;
        if (gcs_banner_patterns[p].shape) {
            for (int i = 0; i < 9; ++i) {
                const ICStack *stack = &grid[i];
                if (i == banner || cc_is_empty(stack)) {
                    if (gcs_banner_patterns[p].shape[i] == '#') ok = 0;
                    continue;
                }
                if (stack->item != 351 || stack->meta < 0
                        || stack->meta >= 16
                        || gcs_banner_patterns[p].shape[i] != '#'
                        || (candidate_dye >= 0
                            && grid[candidate_dye].meta != stack->meta)) {
                    ok = 0; break;
                }
                candidate_dye = i;
            }
            if (candidate_dye < 0) ok = 0;
        } else {
            for (int i = 0; i < 9; ++i) {
                if (i == banner || cc_is_empty(&grid[i])) continue;
                if (grid[i].item == 351 && grid[i].meta >= 0
                        && grid[i].meta < 16 && candidate_dye < 0)
                    candidate_dye = i;
                else if (grid[i].item == gcs_banner_patterns[p].item
                        && grid[i].meta == gcs_banner_patterns[p].meta
                        && candidate_item < 0)
                    candidate_item = i;
                else { ok = 0; break; }
            }
            if (candidate_dye < 0 || candidate_item < 0) ok = 0;
        }
        if (ok) { matched = p; dye = candidate_dye; break; }
    }
    if (matched < 0 || dye < 0) return 0;
    GmNbtBlob tag = {0}, block = {0}, pattern = {0};
    out->output = grid[banner]; out->output.count = 1;
    if (!gcs_copy_tag(r, &grid[banner], &tag)
            || (!gm_nbt_blob_extract_compound(&tag, "BlockEntityTag", &block)
                && !gm_nbt_blob_make_empty(&block))
            || !gm_nbt_blob_make_empty(&pattern)
            || !gm_nbt_blob_set_string(
                &pattern, "Pattern", gcs_banner_patterns[matched].hash)
            || !gm_nbt_blob_set_int(&pattern, "Color", grid[dye].meta)
            || !gm_nbt_blob_append_compound_list(
                &block, "Patterns", &pattern)
            || !gm_nbt_blob_set_compound(&tag, "BlockEntityTag", &block)
            || !gcs_finish_tag(r, &out->output, &tag)) {
        gm_nbt_blob_clear(&pattern); gm_nbt_blob_clear(&block);
        gm_nbt_blob_clear(&tag); return -1;
    }
    gm_nbt_blob_clear(&pattern); gm_nbt_blob_clear(&block);
    return 1;
}

static int gcs_shield(
        GmRuntime *r, const ICStack grid[9], GmCraftSpecial *out) {
    int shield = -1, banner = -1;
    for (int i = 0; i < 9; ++i) if (!cc_is_empty(&grid[i])) {
        if (grid[i].item == 442 && shield < 0) shield = i;
        else if (grid[i].item == 425 && banner < 0) banner = i;
        else return 0;
    }
    if (shield < 0 || banner < 0 || grid[shield].tag_id != 0) return 0;
    GmNbtBlob tag = {0}, block = {0};
    if (!gcs_copy_tag(r, &grid[banner], &tag)
            || !gm_nbt_blob_extract_compound(&tag, "BlockEntityTag", &block)) {
        gm_nbt_blob_clear(&block); gm_nbt_blob_clear(&tag); return 0;
    }
    out->output = grid[shield]; out->output.count = 1;
    if (!gm_nbt_blob_set_int(&block, "Base", grid[banner].meta)
            || !gm_nbt_blob_make_empty(&tag)
            || !gm_nbt_blob_set_compound(&tag, "BlockEntityTag", &block)
            || !gcs_finish_tag(r, &out->output, &tag)) {
        gm_nbt_blob_clear(&block); gm_nbt_blob_clear(&tag); return -1;
    }
    gm_nbt_blob_clear(&block);
    return 1;
}

static int gcs_banner_duplicate(
        GmRuntime *r, const ICStack grid[9], GmCraftSpecial *out) {
    int patterned = -1, blank = -1;
    for (int i = 0; i < 9; ++i) if (!cc_is_empty(&grid[i])) {
        if (grid[i].item != 425) return 0;
        const GmNbtBlob *tag = gm_runtime_stack_tag(r, grid[i].tag_id);
        GmNbtBlob block = {0}; size_t n = 0; int type = 0;
        int has = tag && gm_nbt_blob_extract_compound(
            tag, "BlockEntityTag", &block)
            && gm_nbt_blob_find_list_info(&block, "Patterns", &n, &type)
            && type == 10 && n > 0;
        gm_nbt_blob_clear(&block);
        if (has && patterned < 0) patterned = i;
        else if (!has && blank < 0) blank = i;
        else return 0;
    }
    if (patterned < 0 || blank < 0
            || grid[patterned].meta != grid[blank].meta) return 0;
    out->output = grid[patterned]; out->output.count = 1;
    out->remainder[patterned] = out->output;
    return 1;
}

static int gcs_shulker(const ICStack grid[9], GmCraftSpecial *out) {
    int box = -1, dye = -1;
    for (int i = 0; i < 9; ++i) if (!cc_is_empty(&grid[i])) {
        if (grid[i].item >= 219 && grid[i].item <= 234 && box < 0) box = i;
        else if (grid[i].item == 351 && grid[i].meta >= 0
                && grid[i].meta < 16 && dye < 0) dye = i;
        else return 0;
    }
    if (box < 0 || dye < 0) return 0;
    out->output = grid[box]; out->output.item = 219 + 15 - grid[dye].meta;
    out->output.count = 1;
    return 1;
}

static int gm_crafting_special_match(
        GmRuntime *r, const ICStack grid[9], GmCraftSpecial *out) {
    int result;
    if (!r || !grid || !out) return 0;
    memset(out, 0, sizeof *out); out->output = ic_empty();
    for (int i = 0; i < 9; ++i) out->remainder[i] = ic_empty();
#define GCS_TRY(fn) do { result = fn; if (result != 0) { \
    out->matched = result > 0; return result; } } while (0)
    GCS_TRY(gcs_map_extend(r, grid, out));
    GCS_TRY(gcs_armor_dye(r, grid, out));
    GCS_TRY(gcs_firework(r, grid, out));
    GCS_TRY(gcs_banner_add(r, grid, out));
    GCS_TRY(gcs_tipped_arrow(r, grid, out));
    GCS_TRY(gcs_map_clone(grid, out));
    GCS_TRY(gcs_book_clone(r, grid, out));
    GCS_TRY(gcs_repair(grid, out));
    GCS_TRY(gcs_shield(r, grid, out));
    GCS_TRY(gcs_banner_duplicate(r, grid, out));
    GCS_TRY(gcs_shulker(grid, out));
#undef GCS_TRY
    (void)gcs_nonempty;
    return 0;
}

/* Test and live-container target dispatch for the eleven initialized special
 * recipe entries. This keeps negative controls from being masked by a
 * different ordinary or special recipe elsewhere in the registry. */
static int gm_crafting_special_match_registry(
        GmRuntime *r, int registry, const ICStack grid[9],
        GmCraftSpecial *out) {
    if (!r || !grid || !out) return 0;
    memset(out, 0, sizeof *out);
    out->output = ic_empty();
    for (int i = 0; i < 9; ++i) out->remainder[i] = ic_empty();
    switch (registry) {
    case 136: return gcs_map_extend(r, grid, out);
    case 329: return gcs_armor_dye(r, grid, out);
    case 330: return gcs_firework(r, grid, out);
    case 331: return gcs_banner_add(r, grid, out);
    case 332: return gcs_tipped_arrow(r, grid, out);
    case 333: return gcs_map_clone(grid, out);
    case 334: return gcs_book_clone(r, grid, out);
    case 335: return gcs_repair(grid, out);
    case 347: return gcs_shield(r, grid, out);
    case 348: return gcs_banner_duplicate(r, grid, out);
    case 399: return gcs_shulker(grid, out);
    default: return 0;
    }
}

#endif
