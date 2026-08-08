/* smelting_recipes: FurnaceRecipes.getSmeltingResult + TileEntityFurnace.getItemBurnTime.
 *
 * PORT TARGETS (net/minecraft/item/crafting/FurnaceRecipes.java,
 * net/minecraft/tileentity/TileEntityFurnace.getItemBurnTime):
 *   - addSmeltingRecipe / addSmeltingRecipeForBlock / addSmelting (32767 input wildcard)
 *   - getSmeltingResult + compareItemStacks
 *   - getItemBurnTime fuel branches used by survival fuels
 *
 * PURE LOGIC: no RNG, no floats. Deterministic => Java == CPU == CUDA (verify tier: vanilla bitwise).
 *
 * Recipe ids are the live Minecraft 1.11.2 registry ids exported by the Java
 * oracle. sr_build is the complete 51-row vanilla registry. The live furnace
 * uses the equivalent switch lookup so each tick does not build or scan a
 * per-furnace recipe array.
 *
 * DOCUMENTED DEVIATIONS:
 *   - FurnaceRecipes stores recipes in a HashMap (undefined iteration order). sr_build emits the
 *     stable oracle-export order. Inputs are non-overlapping, so order cannot change a result.
 *   - Forge-domain custom fuel hooks are outside the vanilla registry contract and return 0.
 *   - Player-smelted Forge events and achievements remain runtime/container concerns.
 */
#ifndef MC_SMELTING_RECIPES_H
#define MC_SMELTING_RECIPES_H

#include "mc.h"
#include "mc_blocks.h"

#define SR_WILDCARD 32767

typedef struct { i32 item; i32 count; i32 meta; } SRStack;

/* Live Minecraft 1.11.2 item registry ids. Block items share block ids. */
enum {
    SR_AIR                = 0,
    SR_PLANKS             = 5,
    SR_LOG                = 17,
    SR_COAL               = 263,
    SR_DIAMOND            = 264,
    SR_IRON_INGOT         = 265,
    SR_GOLD_INGOT         = 266,
    SR_STICK              = 280,
    SR_BUCKET             = 325,
    SR_WATER_BUCKET       = 326,
    SR_REDSTONE           = 331,
    SR_LAVA_BUCKET        = 327,
    SR_PORKCHOP           = 319,
    SR_COOKED_PORKCHOP    = 320,
    SR_FISH               = 349,
    SR_COOKED_FISH        = 350,
    SR_DYE                = 351,
    SR_BEEF               = 363,
    SR_COOKED_BEEF        = 364,
    SR_CHICKEN            = 365,
    SR_COOKED_CHICKEN     = 366,
    SR_MUTTON             = 423,
    SR_COOKED_MUTTON      = 424,
    SR_RABBIT             = 411,
    SR_COOKED_RABBIT      = 412,
    SR_ROTTEN_FLESH       = 367,
    SR_BLAZE_ROD          = 369,
    SR_EMERALD            = 388,
    SR_POTATO             = 392,
    SR_BAKED_POTATO       = 393,
    SR_QUARTZ             = 406,
    SR_CHORUS_FRUIT       = 432,
    SR_CHORUS_FRUIT_POPPED= 433,
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

#define SR_NRECIPES 51

MC_HD static inline int sr_build(SRRecipe *R) {
    int n = 0;
    /* Stable order from the initialized real-Java registry export. */
    R[n].input = sr_mk(4, 1, SR_WILDCARD);   R[n].output = sr_mk(1, 1, 0); ++n;
    R[n].input = sr_mk(12, 1, SR_WILDCARD);  R[n].output = sr_mk(20, 1, 0); ++n;
    R[n].input = sr_mk(14, 1, SR_WILDCARD);  R[n].output = sr_mk(266, 1, 0); ++n;
    R[n].input = sr_mk(15, 1, SR_WILDCARD);  R[n].output = sr_mk(265, 1, 0); ++n;
    R[n].input = sr_mk(16, 1, SR_WILDCARD);  R[n].output = sr_mk(263, 1, 0); ++n;
    R[n].input = sr_mk(17, 1, SR_WILDCARD);  R[n].output = sr_mk(263, 1, 1); ++n;
    R[n].input = sr_mk(19, 1, 1);            R[n].output = sr_mk(19, 1, 0); ++n;
    R[n].input = sr_mk(21, 1, SR_WILDCARD);  R[n].output = sr_mk(351, 1, 4); ++n;
    R[n].input = sr_mk(56, 1, SR_WILDCARD);  R[n].output = sr_mk(264, 1, 0); ++n;
    R[n].input = sr_mk(73, 1, SR_WILDCARD);  R[n].output = sr_mk(331, 1, 0); ++n;
    R[n].input = sr_mk(81, 1, SR_WILDCARD);  R[n].output = sr_mk(351, 1, 2); ++n;
    R[n].input = sr_mk(82, 1, SR_WILDCARD);  R[n].output = sr_mk(172, 1, 0); ++n;
    R[n].input = sr_mk(87, 1, SR_WILDCARD);  R[n].output = sr_mk(405, 1, 0); ++n;
    R[n].input = sr_mk(98, 1, 0);            R[n].output = sr_mk(98, 1, 2); ++n;
    R[n].input = sr_mk(129, 1, SR_WILDCARD); R[n].output = sr_mk(388, 1, 0); ++n;
    R[n].input = sr_mk(153, 1, SR_WILDCARD); R[n].output = sr_mk(406, 1, 0); ++n;
    R[n].input = sr_mk(162, 1, SR_WILDCARD); R[n].output = sr_mk(263, 1, 1); ++n;
    R[n].input = sr_mk(256, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(257, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(258, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(267, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(283, 1, SR_WILDCARD); R[n].output = sr_mk(371, 1, 0); ++n;
    R[n].input = sr_mk(284, 1, SR_WILDCARD); R[n].output = sr_mk(371, 1, 0); ++n;
    R[n].input = sr_mk(285, 1, SR_WILDCARD); R[n].output = sr_mk(371, 1, 0); ++n;
    R[n].input = sr_mk(286, 1, SR_WILDCARD); R[n].output = sr_mk(371, 1, 0); ++n;
    R[n].input = sr_mk(292, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(294, 1, SR_WILDCARD); R[n].output = sr_mk(371, 1, 0); ++n;
    R[n].input = sr_mk(302, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(303, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(304, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(305, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(306, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(307, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(308, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(309, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(314, 1, SR_WILDCARD); R[n].output = sr_mk(371, 1, 0); ++n;
    R[n].input = sr_mk(315, 1, SR_WILDCARD); R[n].output = sr_mk(371, 1, 0); ++n;
    R[n].input = sr_mk(316, 1, SR_WILDCARD); R[n].output = sr_mk(371, 1, 0); ++n;
    R[n].input = sr_mk(317, 1, SR_WILDCARD); R[n].output = sr_mk(371, 1, 0); ++n;
    R[n].input = sr_mk(319, 1, SR_WILDCARD); R[n].output = sr_mk(320, 1, 0); ++n;
    R[n].input = sr_mk(337, 1, SR_WILDCARD); R[n].output = sr_mk(336, 1, 0); ++n;
    R[n].input = sr_mk(349, 1, 0);           R[n].output = sr_mk(350, 1, 0); ++n;
    R[n].input = sr_mk(349, 1, 1);           R[n].output = sr_mk(350, 1, 1); ++n;
    R[n].input = sr_mk(363, 1, SR_WILDCARD); R[n].output = sr_mk(364, 1, 0); ++n;
    R[n].input = sr_mk(365, 1, SR_WILDCARD); R[n].output = sr_mk(366, 1, 0); ++n;
    R[n].input = sr_mk(392, 1, SR_WILDCARD); R[n].output = sr_mk(393, 1, 0); ++n;
    R[n].input = sr_mk(411, 1, SR_WILDCARD); R[n].output = sr_mk(412, 1, 0); ++n;
    R[n].input = sr_mk(417, 1, SR_WILDCARD); R[n].output = sr_mk(452, 1, 0); ++n;
    R[n].input = sr_mk(418, 1, SR_WILDCARD); R[n].output = sr_mk(371, 1, 0); ++n;
    R[n].input = sr_mk(423, 1, SR_WILDCARD); R[n].output = sr_mk(424, 1, 0); ++n;
    R[n].input = sr_mk(432, 1, SR_WILDCARD); R[n].output = sr_mk(433, 1, 0); ++n;
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

/* Complete registry lookup for the live CPU/CUDA furnace hot path. */
MC_HD static inline SRStack sr_getSmeltingResultBuiltin(SRStack in) {
    switch (in.item) {
        case 4:   return sr_mk(1, 1, 0);
        case 12:  return sr_mk(20, 1, 0);
        case 14:  return sr_mk(266, 1, 0);
        case 15:  return sr_mk(265, 1, 0);
        case 16:  return sr_mk(263, 1, 0);
        case 17:  return sr_mk(263, 1, 1);
        case 19:  if (in.meta == 1) return sr_mk(19, 1, 0); break;
        case 21:  return sr_mk(351, 1, 4);
        case 56:  return sr_mk(264, 1, 0);
        case 73:  return sr_mk(331, 1, 0);
        case 81:  return sr_mk(351, 1, 2);
        case 82:  return sr_mk(172, 1, 0);
        case 87:  return sr_mk(405, 1, 0);
        case 98:  if (in.meta == 0) return sr_mk(98, 1, 2); break;
        case 129: return sr_mk(388, 1, 0);
        case 153: return sr_mk(406, 1, 0);
        case 162: return sr_mk(263, 1, 1);
        case 256: case 257: case 258: case 267: case 292:
        case 302: case 303: case 304: case 305: case 306: case 307:
        case 308: case 309: case 417:
            return sr_mk(452, 1, 0);
        case 283: case 284: case 285: case 286: case 294:
        case 314: case 315: case 316: case 317: case 418:
            return sr_mk(371, 1, 0);
        case 319: return sr_mk(320, 1, 0);
        case 337: return sr_mk(336, 1, 0);
        case 349:
            if (in.meta == 0) return sr_mk(350, 1, 0);
            if (in.meta == 1) return sr_mk(350, 1, 1);
            break;
        case 363: return sr_mk(364, 1, 0);
        case 365: return sr_mk(366, 1, 0);
        case 392: return sr_mk(393, 1, 0);
        case 411: return sr_mk(412, 1, 0);
        case 423: return sr_mk(424, 1, 0);
        case 432: return sr_mk(433, 1, 0);
        default: break;
    }
    return sr_mk((i32)0xffffffff, 0, 0);
}

/* FurnaceRecipes.getSmeltingExperience over every registered output stack. */
MC_HD static inline float sr_getSmeltingExperience(SRStack out) {
    switch (out.item) {
        case 265: return out.meta == 0 ? 0.7F : 0.0F;
        case 266: case 264: case 388:
            return out.meta == 0 ? 1.0F : 0.0F;
        case 263:
            return out.meta == 0 ? 0.1F
                 : out.meta == 1 ? 0.15F : 0.0F;
        case 19: return out.meta == 0 ? 0.15F : 0.0F;
        case 351:
            return out.meta == 2 || out.meta == 4 ? 0.2F : 0.0F;
        case 172: return out.meta == 0 ? 0.35F : 0.0F;
        case 331: return out.meta == 0 ? 0.7F : 0.0F;
        case 406: return out.meta == 0 ? 0.2F : 0.0F;
        case 336: return out.meta == 0 ? 0.3F : 0.0F;
        case 350:
            return out.meta == 0 || out.meta == 1 ? 0.35F : 0.0F;
        case 320: case 364: case 366: case 393: case 412: case 424:
            return out.meta == 0 ? 0.35F : 0.0F;
        case 1: case 20: case 405: case 433:
        case 452: case 371:
            return out.meta == 0 ? 0.1F : 0.0F;
        case 98: return out.meta == 2 ? 0.1F : 0.0F;
        default:
            return 0.0F;
    }
}

/* Complete vanilla Item.REGISTRY image of TileEntityFurnace.getItemBurnTime. */
MC_HD static inline i32 sr_getItemBurnTime(SRStack stack) {
    if (sr_isEmpty(stack)) return 0;
    switch (stack.item) {
        case 171:
            return 67;
        case 6: case 35: case 143: case 280: case 281:
            return 100;
        case 126:
            return 150;
        case 268: case 269: case 270: case 271: case 290: case 323:
        case 324: case 427: case 428: case 429: case 430: case 431:
            return 200;
        case 5: case 17: case 25: case 47: case 53: case 54: case 58:
        case 65: case 72: case 84: case 85: case 96: case 99: case 100:
        case 107: case 134: case 135: case 136: case 146: case 151:
        case 162: case 163: case 164: case 183: case 184: case 185:
        case 186: case 187: case 188: case 189: case 190: case 191:
        case 192: case 261: case 346: case 425:
            return 300;
        case 333: case 444: case 445: case 446: case 447: case 448:
            return 400;
        case 263:
            return 1600;
        case 369:
            return 2400;
        case 173:
            return 16000;
        case 327:
            return 20000;
        default:
            return 0;
    }
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
    SRStack smelt_in[SR_NSMELT], fuel_in[SR_NFUEL];
    sr_smelt_battery(smelt_in);
    sr_fuel_battery(fuel_in);
    int o = 0;
    for (int i = 0; i < SR_NSMELT; ++i) {
        SRStack r = sr_getSmeltingResultBuiltin(smelt_in[i]);
        out[o++] = (u32)r.item;
        out[o++] = (u32)r.count;
        out[o++] = (u32)r.meta;
    }
    for (int i = 0; i < SR_NFUEL; ++i)
        out[o++] = (u32)sr_getItemBurnTime(fuel_in[i]);
}

#endif /* MC_SMELTING_RECIPES_H */
