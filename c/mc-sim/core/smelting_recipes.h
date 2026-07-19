/* smelting_recipes: FurnaceRecipes.getSmeltingResult + TileEntityFurnace.getItemBurnTime
 * over a KEEP ores/food recipe subset and a KEEP fuel subset.
 *
 * PORT TARGETS (net/minecraft/item/crafting/FurnaceRecipes.java,
 * net/minecraft/tileentity/TileEntityFurnace.getItemBurnTime):
 *   - addSmeltingRecipe / addSmeltingRecipeForBlock / addSmelting (32767 input wildcard)
 *   - getSmeltingResult + compareItemStacks
 *   - getItemBurnTime fuel branches used by survival fuels
 *
 * PURE LOGIC: no RNG, no floats. Deterministic => Java == CPU == CUDA (verify tier: vanilla bitwise).
 *
 * SANCTIONED REGISTRY SUBSTITUTION: Item/Block objects -> vanilla legacy integer ids (same table as
 * core/crafting_recipes.h: skip Items.AIR + spectral/tipped arrow slots => id = 256 + Items.java
 * registration index - 3). Blocks use mc_blocks.h ids. Matching algorithm + recipe DATA are verbatim
 * MC; only registry lookup is the integer-id shim. Golden runs the same shim.
 *
 * DOCUMENTED DEVIATIONS:
 *   - FurnaceRecipes stores recipes in a HashMap (undefined iteration order). We iterate in
 *     REGISTRATION order in a fixed array instead. Battery inputs each match at most one recipe.
 *   - getItemBurnTime's full Material/item-type tree is reduced to explicit KEEP fuel ids + the
 *     Material.WOOD rule for LOG/PLANKS (the only wood blocks in the fuel battery). CUT fuels
 *     (wool, carpet, boats, doors, ...) are not in the lookup table => 0, matching Java.
 *   - Experience (getSmeltingExperience) is CUT; output is (item,count,meta) + fuel burn ticks only.
 */
#ifndef MC_SMELTING_RECIPES_H
#define MC_SMELTING_RECIPES_H

#include "mc.h"
#include "mc_blocks.h"

#define SR_WILDCARD 32767

typedef struct { i32 item; i32 count; i32 meta; } SRStack;

/* item ids (crafting_recipes convention; see header comment) */
enum {
    SR_AIR                = 0,
    SR_PLANKS             = 5,
    SR_LOG                = 17,
    SR_COAL               = 263,
    SR_DIAMOND            = 264,
    SR_IRON_INGOT         = 265,
    SR_GOLD_INGOT         = 266,
    SR_STICK              = 280,
    SR_REDSTONE           = 336,
    SR_LAVA_BUCKET        = 332,
    SR_PORKCHOP           = 319,
    SR_COOKED_PORKCHOP    = 320,
    SR_FISH               = 359,
    SR_COOKED_FISH        = 360,
    SR_DYE                = 361,
    SR_BEEF               = 373,
    SR_COOKED_BEEF        = 374,
    SR_CHICKEN            = 375,
    SR_COOKED_CHICKEN     = 376,
    SR_MUTTON             = 377,
    SR_COOKED_MUTTON      = 378,
    SR_RABBIT             = 379,
    SR_COOKED_RABBIT      = 380,
    SR_ROTTEN_FLESH       = 384,
    SR_BLAZE_ROD          = 386,
    SR_EMERALD            = 408,
    SR_POTATO             = 412,
    SR_BAKED_POTATO       = 413,
    SR_QUARTZ             = 426,
    SR_CHORUS_FRUIT       = 454,
    SR_CHORUS_FRUIT_POPPED= 455,
    SR_DYE_BLUE           = 4   /* EnumDyeColor.BLUE.getDyeDamage() */
};

MC_HD static inline SRStack sr_empty(void) { SRStack s = {SR_AIR, 0, 0}; return s; }
MC_HD static inline SRStack sr_mk(i32 item, i32 count, i32 meta) {
    SRStack s = {item, count, meta}; return s;
}
MC_HD static inline int sr_isEmpty(SRStack s) { return s.item == SR_AIR || s.count <= 0; }

typedef struct {
    SRStack input;   /* meta may be SR_WILDCARD */
    SRStack output;
} SRRecipe;

/* compareItemStacks (verbatim FurnaceRecipes) */
MC_HD static inline int sr_compareItemStacks(SRStack a, SRStack b) {
    return a.item == b.item && (b.meta == SR_WILDCARD || b.meta == a.meta);
}

#define SR_NRECIPES 17

MC_HD static inline int sr_build(SRRecipe *R) {
    int n = 0;
    /* FurnaceRecipes constructor order, KEEP ores + food only */
    R[n].input = sr_mk(BLK_IRON_ORE, 1, SR_WILDCARD);     R[n].output = sr_mk(SR_IRON_INGOT, 1, 0); ++n;
    R[n].input = sr_mk(BLK_GOLD_ORE, 1, SR_WILDCARD);     R[n].output = sr_mk(SR_GOLD_INGOT, 1, 0); ++n;
    R[n].input = sr_mk(BLK_DIAMOND_ORE, 1, SR_WILDCARD);  R[n].output = sr_mk(SR_DIAMOND, 1, 0); ++n;
    R[n].input = sr_mk(BLK_COAL_ORE, 1, SR_WILDCARD);     R[n].output = sr_mk(SR_COAL, 1, 0); ++n;
    R[n].input = sr_mk(BLK_REDSTONE_ORE, 1, SR_WILDCARD); R[n].output = sr_mk(SR_REDSTONE, 1, 0); ++n;
    R[n].input = sr_mk(BLK_LAPIS_ORE, 1, SR_WILDCARD);    R[n].output = sr_mk(SR_DYE, 1, SR_DYE_BLUE); ++n;
    R[n].input = sr_mk(153, 1, SR_WILDCARD);              R[n].output = sr_mk(SR_QUARTZ, 1, 0); ++n; /* QUARTZ_ORE */
    R[n].input = sr_mk(BLK_EMERALD_ORE, 1, SR_WILDCARD); R[n].output = sr_mk(SR_EMERALD, 1, 0); ++n;
    R[n].input = sr_mk(SR_PORKCHOP, 1, SR_WILDCARD);      R[n].output = sr_mk(SR_COOKED_PORKCHOP, 1, 0); ++n;
    R[n].input = sr_mk(SR_BEEF, 1, SR_WILDCARD);          R[n].output = sr_mk(SR_COOKED_BEEF, 1, 0); ++n;
    R[n].input = sr_mk(SR_CHICKEN, 1, SR_WILDCARD);       R[n].output = sr_mk(SR_COOKED_CHICKEN, 1, 0); ++n;
    R[n].input = sr_mk(SR_RABBIT, 1, SR_WILDCARD);        R[n].output = sr_mk(SR_COOKED_RABBIT, 1, 0); ++n;
    R[n].input = sr_mk(SR_MUTTON, 1, SR_WILDCARD);        R[n].output = sr_mk(SR_COOKED_MUTTON, 1, 0); ++n;
    R[n].input = sr_mk(SR_POTATO, 1, SR_WILDCARD);        R[n].output = sr_mk(SR_BAKED_POTATO, 1, 0); ++n;
    R[n].input = sr_mk(SR_CHORUS_FRUIT, 1, SR_WILDCARD);  R[n].output = sr_mk(SR_CHORUS_FRUIT_POPPED, 1, 0); ++n;
    /* ItemFishFood.FishType: COD(0), SALMON(1) cookable */
    R[n].input = sr_mk(SR_FISH, 1, 0);                    R[n].output = sr_mk(SR_COOKED_FISH, 1, 0); ++n;
    R[n].input = sr_mk(SR_FISH, 1, 1);                     R[n].output = sr_mk(SR_COOKED_FISH, 1, 1); ++n;
    return n;
}

/* getSmeltingResult: first match in registration order; no match -> sentinel like crafting_recipes */
MC_HD static inline SRStack sr_getSmeltingResult(const SRRecipe *recipes, int n, SRStack in) {
    for (int i = 0; i < n; ++i) {
        if (sr_compareItemStacks(in, recipes[i].input))
            return recipes[i].output;
    }
    return sr_mk((i32)0xffffffff, 0, 0);
}

/* getItemBurnTime: KEEP fuel subset + Material.WOOD blocks (LOG, PLANKS) */
MC_HD static inline i32 sr_getItemBurnTime(SRStack stack) {
    if (sr_isEmpty(stack)) return 0;
    i32 id = stack.item;
    if (id == SR_COAL) return 1600;
    if (id == SR_STICK) return 100;
    if (id == SR_LAVA_BUCKET) return 20000;
    if (id == SR_BLAZE_ROD) return 2400;
    if (id == SR_LOG || id == SR_PLANKS) return 300; /* Material.WOOD */
    return 0;
}

#define SR_NSMELT 25
#define SR_NFUEL  8
#define SR_OUT    (SR_NSMELT * 3 + SR_NFUEL)

MC_HD static inline void sr_smelt_battery(SRStack out[SR_NSMELT]) {
    SRStack t[SR_NSMELT] = {
        sr_mk(BLK_IRON_ORE, 1, 0),
        sr_mk(BLK_GOLD_ORE, 1, 0),
        sr_mk(BLK_DIAMOND_ORE, 1, 0),
        sr_mk(BLK_COAL_ORE, 1, 0),
        sr_mk(BLK_REDSTONE_ORE, 1, 0),
        sr_mk(BLK_LAPIS_ORE, 1, 0),
        sr_mk(153, 1, 0),
        sr_mk(BLK_EMERALD_ORE, 1, 0),
        sr_mk(SR_PORKCHOP, 1, 0),
        sr_mk(SR_BEEF, 1, 0),
        sr_mk(SR_CHICKEN, 1, 0),
        sr_mk(SR_RABBIT, 1, 0),
        sr_mk(SR_MUTTON, 1, 0),
        sr_mk(SR_POTATO, 1, 0),
        sr_mk(SR_CHORUS_FRUIT, 1, 0),
        sr_mk(SR_FISH, 1, 0),
        sr_mk(SR_FISH, 1, 1),
        sr_mk(SR_FISH, 1, 2),          /* clownfish: not cookable */
        sr_mk(SR_FISH, 1, 3),          /* pufferfish: not cookable */
        sr_mk(SR_IRON_INGOT, 1, 0),    /* no remelt */
        sr_mk(SR_ROTTEN_FLESH, 1, 0),
        sr_mk(SR_COAL, 1, 0),          /* coal item: no smelt recipe */
        sr_mk(BLK_IRON_ORE, 1, 7),     /* wildcard meta still matches ore */
        sr_mk(SR_FISH, 1, 0),          /* duplicate cod (stable output) */
        sr_empty(),
    };
    for (int i = 0; i < SR_NSMELT; ++i) out[i] = t[i];
}

MC_HD static inline void sr_fuel_battery(SRStack out[SR_NFUEL]) {
    SRStack t[SR_NFUEL] = {
        sr_mk(SR_COAL, 1, 0),
        sr_mk(SR_STICK, 1, 0),
        sr_mk(SR_LOG, 1, 0),
        sr_mk(SR_PLANKS, 1, 0),
        sr_mk(SR_LAVA_BUCKET, 1, 0),
        sr_mk(SR_BLAZE_ROD, 1, 0),
        sr_mk(SR_DIAMOND, 1, 0),
        sr_mk(SR_IRON_INGOT, 1, 0),
    };
    for (int i = 0; i < SR_NFUEL; ++i) out[i] = t[i];
}

MC_HD static inline void sr_run_dump(u32 *out) {
    SRRecipe R[SR_NRECIPES];
    int n = sr_build(R);
    SRStack smelt_in[SR_NSMELT], fuel_in[SR_NFUEL];
    sr_smelt_battery(smelt_in);
    sr_fuel_battery(fuel_in);
    int o = 0;
    for (int i = 0; i < SR_NSMELT; ++i) {
        SRStack r = sr_getSmeltingResult(R, n, smelt_in[i]);
        out[o++] = (u32)r.item;
        out[o++] = (u32)r.count;
        out[o++] = (u32)r.meta;
    }
    for (int i = 0; i < SR_NFUEL; ++i)
        out[o++] = (u32)sr_getItemBurnTime(fuel_in[i]);
}

#endif /* MC_SMELTING_RECIPES_H */
