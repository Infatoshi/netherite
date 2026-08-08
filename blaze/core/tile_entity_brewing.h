/* tile_entity_brewing: Minecraft 1.11.2 + Forge vanilla brewing registry.
 *
 * PORT: PotionHelper.init/doReaction, VanillaBrewingRecipe, and
 * TileEntityBrewingStand.update/brewPotions. PotionType's registry identity is
 * carried as a compact integer in stack meta; the mapping below is the exact
 * PotionTypes declaration order. This keeps the kernel POD and CPU == CUDA
 * while preserving the Java Potion NBT identity at the runtime boundary. */
#ifndef MC_TILE_ENTITY_BREWING_H
#define MC_TILE_ENTITY_BREWING_H

#include "mc.h"

enum {
    TB_AIR              = 0,
    TB_GUNPOWDER        = 289,
    TB_REDSTONE         = 331,
    TB_GLOWSTONE_DUST   = 348,
    TB_FISH             = 349,
    TB_SUGAR            = 353,
    TB_GHAST_TEAR       = 370,
    TB_NETHER_WART      = 372,
    TB_POTION           = 373,
    TB_GLASS_BOTTLE     = 374,
    TB_SPIDER_EYE       = 375,
    TB_FERMENTED_EYE    = 376,
    TB_BLAZE_POWDER     = 377,
    TB_MAGMA_CREAM      = 378,
    TB_SPECKLED_MELON   = 382,
    TB_GOLDEN_CARROT    = 396,
    TB_RABBIT_FOOT      = 414,
    TB_DRAGON_BREATH    = 437,
    TB_SPLASH_POTION    = 438,
    TB_LINGERING_POTION = 441,

    TB_BREW_TICKS  = 400,
    TB_FUEL_CHARGE = 20,

    TB_PT_EMPTY = 0,
    TB_PT_WATER,
    TB_PT_MUNDANE,
    TB_PT_THICK,
    TB_PT_AWKWARD,
    TB_PT_NIGHT_VISION,
    TB_PT_LONG_NIGHT_VISION,
    TB_PT_INVISIBILITY,
    TB_PT_LONG_INVISIBILITY,
    TB_PT_LEAPING,
    TB_PT_LONG_LEAPING,
    TB_PT_STRONG_LEAPING,
    TB_PT_FIRE_RESISTANCE,
    TB_PT_LONG_FIRE_RESISTANCE,
    TB_PT_SWIFTNESS,
    TB_PT_LONG_SWIFTNESS,
    TB_PT_STRONG_SWIFTNESS,
    TB_PT_SLOWNESS,
    TB_PT_LONG_SLOWNESS,
    TB_PT_WATER_BREATHING,
    TB_PT_LONG_WATER_BREATHING,
    TB_PT_HEALING,
    TB_PT_STRONG_HEALING,
    TB_PT_HARMING,
    TB_PT_STRONG_HARMING,
    TB_PT_POISON,
    TB_PT_LONG_POISON,
    TB_PT_STRONG_POISON,
    TB_PT_REGENERATION,
    TB_PT_LONG_REGENERATION,
    TB_PT_STRONG_REGENERATION,
    TB_PT_STRENGTH,
    TB_PT_LONG_STRENGTH,
    TB_PT_STRONG_STRENGTH,
    TB_PT_WEAKNESS,
    TB_PT_LONG_WEAKNESS,
    TB_PT_LUCK,
    TB_PT_COUNT
};

typedef struct {
    i32 item;
    i32 count;
    i32 meta;
} TbStack;

typedef struct {
    TbStack slots[5];             /* potion 0..2, ingredient 3, fuel 4 */
    i32 brew_time;
    i32 fuel;
    i32 ingredient_id;            /* Item identity, deliberately not saved */
    i32 bottle_bits;              /* BlockBrewingStand HAS_BOTTLE[0..2] */
    i32 brew_events;              /* World.playEvent(1035) count */
    i32 container_drops;          /* glass bottles spawned by stacked breath */
} TeBrewing;

enum {
    TB_TICK_CHANGED = 1,
    TB_TICK_BREWED = 2,
    TB_TICK_BOTTLES = 4
};

MC_HD static inline TbStack tb_empty(void) {
    TbStack s; s.item = 0; s.count = 0; s.meta = 0; return s;
}

MC_HD static inline TbStack tb_stack(i32 item, i32 count, i32 meta) {
    TbStack s;
    if (item <= 0 || count <= 0) return tb_empty();
    s.item = item; s.count = count; s.meta = meta; return s;
}

MC_HD static inline int tb_stack_empty(TbStack s) {
    return s.item <= 0 || s.count <= 0;
}

MC_HD static inline int tb_is_potion_item(i32 item) {
    return item == TB_POTION || item == TB_SPLASH_POTION
        || item == TB_LINGERING_POTION;
}

/* VanillaBrewingRecipe.isInput names glass bottles, but Forge's outer
 * BrewingRecipeRegistry.isValidInput first requires getMaxStackSize()==1.
 * Glass bottles stack to 64, so the effective accepted set is potion items. */
MC_HD static inline int tb_is_valid_input(TbStack stack) {
    return stack.count == 1 && tb_is_potion_item(stack.item)
        && stack.meta >= TB_PT_EMPTY && stack.meta < TB_PT_COUNT;
}

MC_HD static inline int tb_reagent_matches(
        TbStack reagent, i32 item, i32 meta) {
    return reagent.item == item && reagent.count > 0
        && (meta < 0 || reagent.meta == meta);
}

MC_HD static inline int tb_is_reagent(TbStack reagent) {
    if (reagent.count <= 0) return 0;
    switch (reagent.item) {
    case TB_NETHER_WART:
    case TB_GOLDEN_CARROT:
    case TB_REDSTONE:
    case TB_FERMENTED_EYE:
    case TB_RABBIT_FOOT:
    case TB_GLOWSTONE_DUST:
    case TB_MAGMA_CREAM:
    case TB_SUGAR:
    case TB_SPECKLED_MELON:
    case TB_SPIDER_EYE:
    case TB_GHAST_TEAR:
    case TB_BLAZE_POWDER:
    case TB_GUNPOWDER:
    case TB_DRAGON_BREATH:
        return 1;
    case TB_FISH:
        return reagent.meta == 3; /* ItemFishFood.FishType.PUFFERFISH */
    default:
        return 0;
    }
}

/* PotionHelper POTION_TYPE_CONVERSIONS, in registration order. */
MC_HD static inline int tb_type_reaction(
        i32 input, TbStack reagent, i32 *output) {
#define TB_TYPE_RULE(IN, ITEM, META, OUT) \
    do { \
        if (input == (IN) \
                && tb_reagent_matches(reagent, (ITEM), (META))) { \
            if (output) { *output = (OUT); } \
            return 1; \
        } \
    } while (0)
    TB_TYPE_RULE(TB_PT_WATER, TB_SPECKLED_MELON, -1, TB_PT_MUNDANE);
    TB_TYPE_RULE(TB_PT_WATER, TB_GHAST_TEAR, -1, TB_PT_MUNDANE);
    TB_TYPE_RULE(TB_PT_WATER, TB_RABBIT_FOOT, -1, TB_PT_MUNDANE);
    TB_TYPE_RULE(TB_PT_WATER, TB_BLAZE_POWDER, -1, TB_PT_MUNDANE);
    TB_TYPE_RULE(TB_PT_WATER, TB_SPIDER_EYE, -1, TB_PT_MUNDANE);
    TB_TYPE_RULE(TB_PT_WATER, TB_SUGAR, -1, TB_PT_MUNDANE);
    TB_TYPE_RULE(TB_PT_WATER, TB_MAGMA_CREAM, -1, TB_PT_MUNDANE);
    TB_TYPE_RULE(TB_PT_WATER, TB_GLOWSTONE_DUST, -1, TB_PT_THICK);
    TB_TYPE_RULE(TB_PT_WATER, TB_REDSTONE, -1, TB_PT_MUNDANE);
    TB_TYPE_RULE(TB_PT_WATER, TB_NETHER_WART, -1, TB_PT_AWKWARD);
    TB_TYPE_RULE(TB_PT_AWKWARD, TB_GOLDEN_CARROT, -1, TB_PT_NIGHT_VISION);
    TB_TYPE_RULE(TB_PT_NIGHT_VISION, TB_REDSTONE, -1, TB_PT_LONG_NIGHT_VISION);
    TB_TYPE_RULE(TB_PT_NIGHT_VISION, TB_FERMENTED_EYE, -1, TB_PT_INVISIBILITY);
    TB_TYPE_RULE(TB_PT_LONG_NIGHT_VISION, TB_FERMENTED_EYE, -1, TB_PT_LONG_INVISIBILITY);
    TB_TYPE_RULE(TB_PT_INVISIBILITY, TB_REDSTONE, -1, TB_PT_LONG_INVISIBILITY);
    TB_TYPE_RULE(TB_PT_AWKWARD, TB_MAGMA_CREAM, -1, TB_PT_FIRE_RESISTANCE);
    TB_TYPE_RULE(TB_PT_FIRE_RESISTANCE, TB_REDSTONE, -1, TB_PT_LONG_FIRE_RESISTANCE);
    TB_TYPE_RULE(TB_PT_AWKWARD, TB_RABBIT_FOOT, -1, TB_PT_LEAPING);
    TB_TYPE_RULE(TB_PT_LEAPING, TB_REDSTONE, -1, TB_PT_LONG_LEAPING);
    TB_TYPE_RULE(TB_PT_LEAPING, TB_GLOWSTONE_DUST, -1, TB_PT_STRONG_LEAPING);
    TB_TYPE_RULE(TB_PT_LEAPING, TB_FERMENTED_EYE, -1, TB_PT_SLOWNESS);
    TB_TYPE_RULE(TB_PT_LONG_LEAPING, TB_FERMENTED_EYE, -1, TB_PT_LONG_SLOWNESS);
    TB_TYPE_RULE(TB_PT_SLOWNESS, TB_REDSTONE, -1, TB_PT_LONG_SLOWNESS);
    TB_TYPE_RULE(TB_PT_SWIFTNESS, TB_FERMENTED_EYE, -1, TB_PT_SLOWNESS);
    TB_TYPE_RULE(TB_PT_LONG_SWIFTNESS, TB_FERMENTED_EYE, -1, TB_PT_LONG_SLOWNESS);
    TB_TYPE_RULE(TB_PT_AWKWARD, TB_SUGAR, -1, TB_PT_SWIFTNESS);
    TB_TYPE_RULE(TB_PT_SWIFTNESS, TB_REDSTONE, -1, TB_PT_LONG_SWIFTNESS);
    TB_TYPE_RULE(TB_PT_SWIFTNESS, TB_GLOWSTONE_DUST, -1, TB_PT_STRONG_SWIFTNESS);
    TB_TYPE_RULE(TB_PT_AWKWARD, TB_FISH, 3, TB_PT_WATER_BREATHING);
    TB_TYPE_RULE(TB_PT_WATER_BREATHING, TB_REDSTONE, -1, TB_PT_LONG_WATER_BREATHING);
    TB_TYPE_RULE(TB_PT_AWKWARD, TB_SPECKLED_MELON, -1, TB_PT_HEALING);
    TB_TYPE_RULE(TB_PT_HEALING, TB_GLOWSTONE_DUST, -1, TB_PT_STRONG_HEALING);
    TB_TYPE_RULE(TB_PT_HEALING, TB_FERMENTED_EYE, -1, TB_PT_HARMING);
    TB_TYPE_RULE(TB_PT_STRONG_HEALING, TB_FERMENTED_EYE, -1, TB_PT_STRONG_HARMING);
    TB_TYPE_RULE(TB_PT_HARMING, TB_GLOWSTONE_DUST, -1, TB_PT_STRONG_HARMING);
    TB_TYPE_RULE(TB_PT_POISON, TB_FERMENTED_EYE, -1, TB_PT_HARMING);
    TB_TYPE_RULE(TB_PT_LONG_POISON, TB_FERMENTED_EYE, -1, TB_PT_HARMING);
    TB_TYPE_RULE(TB_PT_STRONG_POISON, TB_FERMENTED_EYE, -1, TB_PT_STRONG_HARMING);
    TB_TYPE_RULE(TB_PT_AWKWARD, TB_SPIDER_EYE, -1, TB_PT_POISON);
    TB_TYPE_RULE(TB_PT_POISON, TB_REDSTONE, -1, TB_PT_LONG_POISON);
    TB_TYPE_RULE(TB_PT_POISON, TB_GLOWSTONE_DUST, -1, TB_PT_STRONG_POISON);
    TB_TYPE_RULE(TB_PT_AWKWARD, TB_GHAST_TEAR, -1, TB_PT_REGENERATION);
    TB_TYPE_RULE(TB_PT_REGENERATION, TB_REDSTONE, -1, TB_PT_LONG_REGENERATION);
    TB_TYPE_RULE(TB_PT_REGENERATION, TB_GLOWSTONE_DUST, -1, TB_PT_STRONG_REGENERATION);
    TB_TYPE_RULE(TB_PT_AWKWARD, TB_BLAZE_POWDER, -1, TB_PT_STRENGTH);
    TB_TYPE_RULE(TB_PT_STRENGTH, TB_REDSTONE, -1, TB_PT_LONG_STRENGTH);
    TB_TYPE_RULE(TB_PT_STRENGTH, TB_GLOWSTONE_DUST, -1, TB_PT_STRONG_STRENGTH);
    TB_TYPE_RULE(TB_PT_WATER, TB_FERMENTED_EYE, -1, TB_PT_WEAKNESS);
    TB_TYPE_RULE(TB_PT_WEAKNESS, TB_REDSTONE, -1, TB_PT_LONG_WEAKNESS);
#undef TB_TYPE_RULE
    return 0;
}

/* Forge's vanilla registry rejects non-single inputs before invoking this. */
MC_HD static inline TbStack tb_get_output(TbStack input, TbStack reagent) {
    i32 type;
    if (!tb_is_valid_input(input) || !tb_is_reagent(reagent)
            || !tb_is_potion_item(input.item))
        return tb_empty();
    /* PotionHelper checks item conversions before type conversions. */
    if (input.item == TB_POTION && reagent.item == TB_GUNPOWDER)
        return tb_stack(TB_SPLASH_POTION, 1, input.meta);
    if (input.item == TB_SPLASH_POTION
            && reagent.item == TB_DRAGON_BREATH)
        return tb_stack(TB_LINGERING_POTION, 1, input.meta);
    if (tb_type_reaction(input.meta, reagent, &type))
        return tb_stack(input.item, 1, type);
    return tb_empty();
}

MC_HD static inline int tb_can_brew(const TeBrewing *b) {
    int i;
    if (!b || tb_stack_empty(b->slots[3])) return 0;
    for (i = 0; i < 3; ++i)
        if (!tb_stack_empty(tb_get_output(b->slots[i], b->slots[3])))
            return 1;
    return 0;
}

MC_HD static inline int tb_bottle_bits(const TeBrewing *b) {
    int bits = 0;
    if (!b) return 0;
    if (!tb_stack_empty(b->slots[0])) bits |= 1;
    if (!tb_stack_empty(b->slots[1])) bits |= 2;
    if (!tb_stack_empty(b->slots[2])) bits |= 4;
    return bits;
}

MC_HD static inline void tb_apply_brew(TeBrewing *b) {
    TbStack ingredient;
    int i;
    if (!b) return;
    ingredient = b->slots[3];
    for (i = 0; i < 3; ++i) {
        TbStack output = tb_get_output(b->slots[i], ingredient);
        if (!tb_stack_empty(output)) b->slots[i] = output;
    }
    b->slots[3].count--;
    if (b->slots[3].count <= 0) b->slots[3] = tb_empty();
    /* Items.DRAGON_BREATH has GLASS_BOTTLE as its container item. */
    if (ingredient.item == TB_DRAGON_BREATH) {
        if (tb_stack_empty(b->slots[3]))
            b->slots[3] = tb_stack(TB_GLASS_BOTTLE, 1, 0);
        else
            b->container_drops++;
    }
    b->brew_events++;
}

MC_HD static inline void tb_init_empty(TeBrewing *b) {
    int i;
    if (!b) return;
    for (i = 0; i < 5; ++i) b->slots[i] = tb_empty();
    b->brew_time = 0;
    b->fuel = 0;
    b->ingredient_id = 0;
    b->bottle_bits = 0;
    b->brew_events = 0;
    b->container_drops = 0;
}

MC_HD static inline int tb_tick(TeBrewing *b) {
    int flags = 0;
    int old_bits;
    int can_brew;
    TbStack ingredient;
    if (!b) return 0;
    old_bits = b->bottle_bits;
    if (b->fuel <= 0 && b->slots[4].item == TB_BLAZE_POWDER
            && b->slots[4].count > 0) {
        b->fuel = TB_FUEL_CHARGE;
        b->slots[4].count--;
        if (b->slots[4].count <= 0) b->slots[4] = tb_empty();
        flags |= TB_TICK_CHANGED;
    }

    can_brew = tb_can_brew(b);
    ingredient = b->slots[3];
    if (b->brew_time > 0) {
        b->brew_time--;
        if (b->brew_time == 0 && can_brew) {
            tb_apply_brew(b);
            flags |= TB_TICK_CHANGED | TB_TICK_BREWED;
        } else if (!can_brew) {
            b->brew_time = 0;
            flags |= TB_TICK_CHANGED;
        } else if (b->ingredient_id != ingredient.item) {
            b->brew_time = 0;
            flags |= TB_TICK_CHANGED;
        }
    } else if (can_brew && b->fuel > 0) {
        b->fuel--;
        b->brew_time = TB_BREW_TICKS;
        b->ingredient_id = ingredient.item;
        flags |= TB_TICK_CHANGED;
    }

    b->bottle_bits = tb_bottle_bits(b);
    if (b->bottle_bits != old_bits)
        flags |= TB_TICK_CHANGED | TB_TICK_BOTTLES;
    return flags;
}

/* Complete graph plus lifecycle/cancellation/container-item battery. */
typedef struct { i32 item, count, type, reagent, reagent_meta; } TbCase;

#define TB_GRAPH_CASES 56
#define TB_DUMP_FIELDS 21
#define TB_LIFECYCLE_MARKS 6
#define TB_OUT (TB_GRAPH_CASES * 3 + (TB_LIFECYCLE_MARKS + 4) * TB_DUMP_FIELDS)

MC_HD static inline void tb_emit_state(const TeBrewing *b, u64 *out, int *o) {
    int i;
    for (i = 0; i < 5; ++i) {
        out[(*o)++] = (u64)(u32)b->slots[i].item;
        out[(*o)++] = (u64)(u32)b->slots[i].count;
        out[(*o)++] = (u64)(u32)b->slots[i].meta;
    }
    out[(*o)++] = (u64)(u32)b->brew_time;
    out[(*o)++] = (u64)(u32)b->fuel;
    out[(*o)++] = (u64)(u32)b->ingredient_id;
    out[(*o)++] = (u64)(u32)b->bottle_bits;
    out[(*o)++] = (u64)(u32)b->brew_events;
    out[(*o)++] = (u64)(u32)b->container_drops;
}

MC_HD static inline void tb_run_dump(TeBrewing *b, u64 *out) {
    static const TbCase cases[TB_GRAPH_CASES] = {
        {TB_POTION,1,TB_PT_AWKWARD,TB_GUNPOWDER,0},
        {TB_SPLASH_POTION,1,TB_PT_AWKWARD,TB_DRAGON_BREATH,0},
        {TB_POTION,1,TB_PT_WATER,TB_SPECKLED_MELON,0},
        {TB_POTION,1,TB_PT_WATER,TB_GHAST_TEAR,0},
        {TB_POTION,1,TB_PT_WATER,TB_RABBIT_FOOT,0},
        {TB_POTION,1,TB_PT_WATER,TB_BLAZE_POWDER,0},
        {TB_POTION,1,TB_PT_WATER,TB_SPIDER_EYE,0},
        {TB_POTION,1,TB_PT_WATER,TB_SUGAR,0},
        {TB_POTION,1,TB_PT_WATER,TB_MAGMA_CREAM,0},
        {TB_POTION,1,TB_PT_WATER,TB_GLOWSTONE_DUST,0},
        {TB_POTION,1,TB_PT_WATER,TB_REDSTONE,0},
        {TB_POTION,1,TB_PT_WATER,TB_NETHER_WART,0},
        {TB_POTION,1,TB_PT_AWKWARD,TB_GOLDEN_CARROT,0},
        {TB_POTION,1,TB_PT_NIGHT_VISION,TB_REDSTONE,0},
        {TB_POTION,1,TB_PT_NIGHT_VISION,TB_FERMENTED_EYE,0},
        {TB_POTION,1,TB_PT_LONG_NIGHT_VISION,TB_FERMENTED_EYE,0},
        {TB_POTION,1,TB_PT_INVISIBILITY,TB_REDSTONE,0},
        {TB_POTION,1,TB_PT_AWKWARD,TB_MAGMA_CREAM,0},
        {TB_POTION,1,TB_PT_FIRE_RESISTANCE,TB_REDSTONE,0},
        {TB_POTION,1,TB_PT_AWKWARD,TB_RABBIT_FOOT,0},
        {TB_POTION,1,TB_PT_LEAPING,TB_REDSTONE,0},
        {TB_POTION,1,TB_PT_LEAPING,TB_GLOWSTONE_DUST,0},
        {TB_POTION,1,TB_PT_LEAPING,TB_FERMENTED_EYE,0},
        {TB_POTION,1,TB_PT_LONG_LEAPING,TB_FERMENTED_EYE,0},
        {TB_POTION,1,TB_PT_SLOWNESS,TB_REDSTONE,0},
        {TB_POTION,1,TB_PT_SWIFTNESS,TB_FERMENTED_EYE,0},
        {TB_POTION,1,TB_PT_LONG_SWIFTNESS,TB_FERMENTED_EYE,0},
        {TB_POTION,1,TB_PT_AWKWARD,TB_SUGAR,0},
        {TB_POTION,1,TB_PT_SWIFTNESS,TB_REDSTONE,0},
        {TB_POTION,1,TB_PT_SWIFTNESS,TB_GLOWSTONE_DUST,0},
        {TB_POTION,1,TB_PT_AWKWARD,TB_FISH,3},
        {TB_POTION,1,TB_PT_WATER_BREATHING,TB_REDSTONE,0},
        {TB_POTION,1,TB_PT_AWKWARD,TB_SPECKLED_MELON,0},
        {TB_POTION,1,TB_PT_HEALING,TB_GLOWSTONE_DUST,0},
        {TB_POTION,1,TB_PT_HEALING,TB_FERMENTED_EYE,0},
        {TB_POTION,1,TB_PT_STRONG_HEALING,TB_FERMENTED_EYE,0},
        {TB_POTION,1,TB_PT_HARMING,TB_GLOWSTONE_DUST,0},
        {TB_POTION,1,TB_PT_POISON,TB_FERMENTED_EYE,0},
        {TB_POTION,1,TB_PT_LONG_POISON,TB_FERMENTED_EYE,0},
        {TB_POTION,1,TB_PT_STRONG_POISON,TB_FERMENTED_EYE,0},
        {TB_POTION,1,TB_PT_AWKWARD,TB_SPIDER_EYE,0},
        {TB_POTION,1,TB_PT_POISON,TB_REDSTONE,0},
        {TB_POTION,1,TB_PT_POISON,TB_GLOWSTONE_DUST,0},
        {TB_POTION,1,TB_PT_AWKWARD,TB_GHAST_TEAR,0},
        {TB_POTION,1,TB_PT_REGENERATION,TB_REDSTONE,0},
        {TB_POTION,1,TB_PT_REGENERATION,TB_GLOWSTONE_DUST,0},
        {TB_POTION,1,TB_PT_AWKWARD,TB_BLAZE_POWDER,0},
        {TB_POTION,1,TB_PT_STRENGTH,TB_REDSTONE,0},
        {TB_POTION,1,TB_PT_STRENGTH,TB_GLOWSTONE_DUST,0},
        {TB_POTION,1,TB_PT_WATER,TB_FERMENTED_EYE,0},
        {TB_POTION,1,TB_PT_WEAKNESS,TB_REDSTONE,0},
        {TB_GLASS_BOTTLE,1,TB_PT_EMPTY,TB_NETHER_WART,0},
        {TB_POTION,2,TB_PT_WATER,TB_NETHER_WART,0},
        {TB_POTION,1,TB_PT_AWKWARD,TB_FISH,0},
        {1,1,TB_PT_WATER,TB_NETHER_WART,0},
        {TB_POTION,1,TB_PT_WATER,TB_AIR,0}
    };
    static const int marks[TB_LIFECYCLE_MARKS] = {0, 1, 2, 400, 401, 402};
    int i, o = 0, cur = 0;
    for (i = 0; i < TB_GRAPH_CASES; ++i) {
        TbStack result = tb_get_output(
            tb_stack(cases[i].item, cases[i].count, cases[i].type),
            tb_stack(cases[i].reagent, 1, cases[i].reagent_meta));
        out[o++] = (u64)(u32)result.item;
        out[o++] = (u64)(u32)result.count;
        out[o++] = (u64)(u32)result.meta;
    }

    tb_init_empty(b);
    b->slots[0] = tb_stack(TB_POTION, 1, TB_PT_WATER);
    b->slots[1] = tb_stack(TB_SPLASH_POTION, 1, TB_PT_WATER);
    b->slots[2] = tb_stack(TB_LINGERING_POTION, 1, TB_PT_WATER);
    b->slots[3] = tb_stack(TB_NETHER_WART, 2, 0);
    b->slots[4] = tb_stack(TB_BLAZE_POWDER, 2, 0);
    b->bottle_bits = tb_bottle_bits(b);
    for (i = 0; i < TB_LIFECYCLE_MARKS; ++i) {
        while (cur < marks[i]) { tb_tick(b); cur++; }
        tb_emit_state(b, out, &o);
    }

    /* A different but still-valid ingredient cancels after the decrement. */
    tb_init_empty(b);
    b->slots[0] = tb_stack(TB_POTION, 1, TB_PT_WATER);
    b->slots[3] = tb_stack(TB_NETHER_WART, 1, 0);
    b->slots[4] = tb_stack(TB_BLAZE_POWDER, 1, 0);
    tb_tick(b);
    b->slots[3] = tb_stack(TB_SUGAR, 1, 0);
    tb_tick(b);
    tb_emit_state(b, out, &o);

    /* Removing the ingredient cancels immediately. */
    tb_init_empty(b);
    b->slots[0] = tb_stack(TB_POTION, 1, TB_PT_WATER);
    b->slots[3] = tb_stack(TB_NETHER_WART, 1, 0);
    b->slots[4] = tb_stack(TB_BLAZE_POWDER, 1, 0);
    tb_tick(b);
    b->slots[3] = tb_empty();
    tb_tick(b);
    tb_emit_state(b, out, &o);

    /* Stacked dragon breath leaves one breath and spawns a bottle. */
    tb_init_empty(b);
    b->slots[0] = tb_stack(TB_SPLASH_POTION, 1, TB_PT_AWKWARD);
    b->slots[3] = tb_stack(TB_DRAGON_BREATH, 2, 0);
    b->slots[4] = tb_stack(TB_BLAZE_POWDER, 1, 0);
    for (i = 0; i < 401; ++i) tb_tick(b);
    tb_emit_state(b, out, &o);

    /* A final dragon breath is replaced by its glass-bottle container. */
    tb_init_empty(b);
    b->slots[0] = tb_stack(TB_SPLASH_POTION, 1, TB_PT_AWKWARD);
    b->slots[3] = tb_stack(TB_DRAGON_BREATH, 1, 0);
    b->slots[4] = tb_stack(TB_BLAZE_POWDER, 1, 0);
    for (i = 0; i < 401; ++i) tb_tick(b);
    tb_emit_state(b, out, &o);
}

#endif /* MC_TILE_ENTITY_BREWING_H */
