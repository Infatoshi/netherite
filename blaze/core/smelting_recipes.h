/* smelting_recipes: FurnaceRecipes.getSmeltingResult + getSmeltingExperience
 * + TileEntityFurnace.getItemBurnTime. Numeric ids from Item.registerItems /
 * Block.registerBlocks (cite on each row).
 *
 * PORT: java/oracle-src/net/minecraft/item/crafting/FurnaceRecipes.java:31-91
 *       java/oracle-src/net/minecraft/tileentity/TileEntityFurnace.java:340-355
 * Derived table: verify/furnace_registry.py (run from make test).
 *
 * PURE LOGIC: no RNG. Deterministic => Java == CPU == CUDA.
 *
 * FurnaceRecipes HashMap iteration is undefined; we store registration order
 * in a fixed array. getSmeltingResult still walks the array (compareItemStacks).
 */
#ifndef MC_SMELTING_RECIPES_H
#define MC_SMELTING_RECIPES_H

#include "mc.h"
#include "mc_blocks.h"

#define SR_WILDCARD 32767

typedef struct { i32 item; i32 count; i32 meta; } SRStack;

/* Vanilla 1.11.2 ids used by live furnace + tests. */
enum {
    SR_AIR             = 0,
    SR_STONE           = 1,
    SR_PLANKS          = 5,    /* Block.java:2409 */
    SR_LOG             = 17,   /* Block.java:2421 */
    SR_SPONGE          = 19,   /* Block.java:2423 */
    SR_COAL            = 263,  /* Item.java:1504 */
    SR_DIAMOND         = 264,  /* Item.java:1505 */
    SR_IRON_INGOT      = 265,  /* Item.java:1506 */
    SR_GOLD_INGOT      = 266,  /* Item.java:1507 */
    SR_STICK           = 280,  /* Item.java:1521 */
    SR_BOWL            = 281,  /* Item.java:1522 */
    SR_BUCKET          = 325,  /* Item.java:1567 setMaxStackSize(16) */
    SR_WATER_BUCKET    = 326,  /* Item.java:1568 */
    SR_LAVA_BUCKET     = 327,  /* Item.java:1569 */
    SR_REDSTONE        = 331,  /* Item.java:1573 */
    SR_FISH            = 349,  /* Item.java:1591 */
    SR_COOKED_FISH     = 350,  /* Item.java:1592 */
    SR_BEEF            = 363,  /* Item.java:1605 */
    SR_COOKED_BEEF     = 364,  /* Item.java:1606 */
    SR_BLAZE_ROD       = 369,  /* Item.java:1611 */
    SR_POTATO          = 392,  /* Item.java:1635 */
    SR_BAKED_POTATO    = 393   /* Item.java:1636 */
};

MC_HD static inline SRStack sr_empty(void) { SRStack s = {SR_AIR, 0, 0}; return s; }
MC_HD static inline SRStack sr_mk(i32 item, i32 count, i32 meta) {
    SRStack s = {item, count, meta}; return s;
}
MC_HD static inline int sr_isEmpty(SRStack s) { return s.item == SR_AIR || s.count <= 0; }

typedef struct {
    SRStack input;   /* meta may be SR_WILDCARD */
    SRStack output;
    float   xp;      /* FurnaceRecipes.experienceList */
} SRRecipe;

/* compareItemStacks (FurnaceRecipes.java:138-141) */
MC_HD static inline int sr_compareItemStacks(SRStack a, SRStack b) {
    return a.item == b.item && (b.meta == SR_WILDCARD || b.meta == a.meta);
}

#define SR_NRECIPES 51

MC_HD static inline int sr_build(SRRecipe *R) {
    int n = 0;
    R[n].input = sr_mk(15, 1, 32767); R[n].output = sr_mk(265, 1, 0); R[n].xp = 0.7f; ++n; /* FurnaceRecipes.java:33 (Block.java:2419 iron_ore -> Item.java:1506 iron_ingot) */
    R[n].input = sr_mk(14, 1, 32767); R[n].output = sr_mk(266, 1, 0); R[n].xp = 1.0f; ++n; /* FurnaceRecipes.java:34 (Block.java:2418 gold_ore -> Item.java:1507 gold_ingot) */
    R[n].input = sr_mk(56, 1, 32767); R[n].output = sr_mk(264, 1, 0); R[n].xp = 1.0f; ++n; /* FurnaceRecipes.java:35 (Block.java:2464 diamond_ore -> Item.java:1505 diamond) */
    R[n].input = sr_mk(12, 1, 32767); R[n].output = sr_mk(20, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:36 (Block.java:2416 sand -> Block.java:2424 glass) */
    R[n].input = sr_mk(319, 1, 32767); R[n].output = sr_mk(320, 1, 0); R[n].xp = 0.35f; ++n; /* FurnaceRecipes.java:37 (Item.java:1560 porkchop -> Item.java:1561 cooked_porkchop) */
    R[n].input = sr_mk(363, 1, 32767); R[n].output = sr_mk(364, 1, 0); R[n].xp = 0.35f; ++n; /* FurnaceRecipes.java:38 (Item.java:1605 beef -> Item.java:1606 cooked_beef) */
    R[n].input = sr_mk(365, 1, 32767); R[n].output = sr_mk(366, 1, 0); R[n].xp = 0.35f; ++n; /* FurnaceRecipes.java:39 (Item.java:1607 chicken -> Item.java:1608 cooked_chicken) */
    R[n].input = sr_mk(411, 1, 32767); R[n].output = sr_mk(412, 1, 0); R[n].xp = 0.35f; ++n; /* FurnaceRecipes.java:40 (Item.java:1654 rabbit -> Item.java:1655 cooked_rabbit) */
    R[n].input = sr_mk(423, 1, 32767); R[n].output = sr_mk(424, 1, 0); R[n].xp = 0.35f; ++n; /* FurnaceRecipes.java:41 (Item.java:1666 mutton -> Item.java:1667 cooked_mutton) */
    R[n].input = sr_mk(4, 1, 32767); R[n].output = sr_mk(1, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:42 (Block.java:2407 cobblestone -> Block.java:2403 stone) */
    R[n].input = sr_mk(98, 1, 0); R[n].output = sr_mk(98, 1, 2); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:43 (Block.java:2509 stonebrick -> Block.java:2509 stonebrick) */
    R[n].input = sr_mk(337, 1, 32767); R[n].output = sr_mk(336, 1, 0); R[n].xp = 0.3f; ++n; /* FurnaceRecipes.java:44 (Item.java:1579 clay_ball -> Item.java:1578 brick) */
    R[n].input = sr_mk(82, 1, 32767); R[n].output = sr_mk(172, 1, 0); R[n].xp = 0.35f; ++n; /* FurnaceRecipes.java:45 (Block.java:2491 clay -> Block.java:2586 hardened_clay) */
    R[n].input = sr_mk(81, 1, 32767); R[n].output = sr_mk(351, 1, 2); R[n].xp = 0.2f; ++n; /* FurnaceRecipes.java:46 (Block.java:2490 cactus -> Item.java:1593 dye) */
    R[n].input = sr_mk(17, 1, 32767); R[n].output = sr_mk(263, 1, 1); R[n].xp = 0.15f; ++n; /* FurnaceRecipes.java:47 (Block.java:2421 log -> Item.java:1504 coal) */
    R[n].input = sr_mk(162, 1, 32767); R[n].output = sr_mk(263, 1, 1); R[n].xp = 0.15f; ++n; /* FurnaceRecipes.java:48 (Block.java:2576 log2 -> Item.java:1504 coal) */
    R[n].input = sr_mk(129, 1, 32767); R[n].output = sr_mk(388, 1, 0); R[n].xp = 1.0f; ++n; /* FurnaceRecipes.java:49 (Block.java:2542 emerald_ore -> Item.java:1631 emerald) */
    R[n].input = sr_mk(392, 1, 32767); R[n].output = sr_mk(393, 1, 0); R[n].xp = 0.35f; ++n; /* FurnaceRecipes.java:50 (Item.java:1635 potato -> Item.java:1636 baked_potato) */
    R[n].input = sr_mk(87, 1, 32767); R[n].output = sr_mk(405, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:51 (Block.java:2497 netherrack -> Item.java:1648 netherbrick) */
    R[n].input = sr_mk(19, 1, 1); R[n].output = sr_mk(19, 1, 0); R[n].xp = 0.15f; ++n; /* FurnaceRecipes.java:52 (Block.java:2423 sponge -> Block.java:2423 sponge) */
    R[n].input = sr_mk(432, 1, 32767); R[n].output = sr_mk(433, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:53 (Item.java:1675 chorus_fruit -> Item.java:1676 chorus_fruit_popped) */
    R[n].input = sr_mk(349, 1, 0); R[n].output = sr_mk(350, 1, 0); R[n].xp = 0.35f; ++n; /* FurnaceRecipes.java:55-61 ItemFishFood.FishType.COD */
    R[n].input = sr_mk(349, 1, 1); R[n].output = sr_mk(350, 1, 1); R[n].xp = 0.35f; ++n; /* FurnaceRecipes.java:55-61 ItemFishFood.FishType.SALMON */
    R[n].input = sr_mk(16, 1, 32767); R[n].output = sr_mk(263, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:63 (Block.java:2420 coal_ore -> Item.java:1504 coal) */
    R[n].input = sr_mk(73, 1, 32767); R[n].output = sr_mk(331, 1, 0); R[n].xp = 0.7f; ++n; /* FurnaceRecipes.java:64 (Block.java:2482 redstone_ore -> Item.java:1573 redstone) */
    R[n].input = sr_mk(21, 1, 32767); R[n].output = sr_mk(351, 1, 4); R[n].xp = 0.2f; ++n; /* FurnaceRecipes.java:65 (Block.java:2425 lapis_ore -> Item.java:1593 dye) */
    R[n].input = sr_mk(153, 1, 32767); R[n].output = sr_mk(406, 1, 0); R[n].xp = 0.2f; ++n; /* FurnaceRecipes.java:66 (Block.java:2566 quartz_ore -> Item.java:1649 quartz) */
    R[n].input = sr_mk(302, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:67 (Item.java:1543 chainmail_helmet -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(303, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:68 (Item.java:1544 chainmail_chestplate -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(304, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:69 (Item.java:1545 chainmail_leggings -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(305, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:70 (Item.java:1546 chainmail_boots -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(257, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:71 (Item.java:1498 iron_pickaxe -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(256, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:72 (Item.java:1497 iron_shovel -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(258, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:73 (Item.java:1499 iron_axe -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(292, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:74 (Item.java:1533 iron_hoe -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(267, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:75 (Item.java:1508 iron_sword -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(306, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:76 (Item.java:1547 iron_helmet -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(307, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:77 (Item.java:1548 iron_chestplate -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(308, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:78 (Item.java:1549 iron_leggings -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(309, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:79 (Item.java:1550 iron_boots -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(417, 1, 32767); R[n].output = sr_mk(452, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:80 (Item.java:1660 iron_horse_armor -> Item.java:1694 iron_nugget) */
    R[n].input = sr_mk(285, 1, 32767); R[n].output = sr_mk(371, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:81 (Item.java:1526 golden_pickaxe -> Item.java:1613 gold_nugget) */
    R[n].input = sr_mk(284, 1, 32767); R[n].output = sr_mk(371, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:82 (Item.java:1525 golden_shovel -> Item.java:1613 gold_nugget) */
    R[n].input = sr_mk(286, 1, 32767); R[n].output = sr_mk(371, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:83 (Item.java:1527 golden_axe -> Item.java:1613 gold_nugget) */
    R[n].input = sr_mk(294, 1, 32767); R[n].output = sr_mk(371, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:84 (Item.java:1535 golden_hoe -> Item.java:1613 gold_nugget) */
    R[n].input = sr_mk(283, 1, 32767); R[n].output = sr_mk(371, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:85 (Item.java:1524 golden_sword -> Item.java:1613 gold_nugget) */
    R[n].input = sr_mk(314, 1, 32767); R[n].output = sr_mk(371, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:86 (Item.java:1555 golden_helmet -> Item.java:1613 gold_nugget) */
    R[n].input = sr_mk(315, 1, 32767); R[n].output = sr_mk(371, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:87 (Item.java:1556 golden_chestplate -> Item.java:1613 gold_nugget) */
    R[n].input = sr_mk(316, 1, 32767); R[n].output = sr_mk(371, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:88 (Item.java:1557 golden_leggings -> Item.java:1613 gold_nugget) */
    R[n].input = sr_mk(317, 1, 32767); R[n].output = sr_mk(371, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:89 (Item.java:1558 golden_boots -> Item.java:1613 gold_nugget) */
    R[n].input = sr_mk(418, 1, 32767); R[n].output = sr_mk(371, 1, 0); R[n].xp = 0.1f; ++n; /* FurnaceRecipes.java:90 (Item.java:1661 golden_horse_armor -> Item.java:1613 gold_nugget) */
    return n;
}

/* getSmeltingResult: first match in registration order (FurnaceRecipes.java:122-133) */
MC_HD static inline SRStack sr_getSmeltingResult(const SRRecipe *recipes, int n, SRStack in) {
    int i;
    for (i = 0; i < n; ++i) {
        if (sr_compareItemStacks(in, recipes[i].input))
            return recipes[i].output;
    }
    return sr_mk((i32)0xffffffff, 0, 0);
}

MC_HD static inline float sr_getSmeltingExperience(const SRRecipe *recipes, int n, SRStack out) {
    int i;
    for (i = 0; i < n; ++i) {
        if (sr_compareItemStacks(out, recipes[i].output))
            return recipes[i].xp;
    }
    return 0.0f;
}

/* Material.WOOD blocks (TileEntityFurnace.java:354 material check).
 * Wooden slab 126 is WOOD but returns 150 before this test. */
MC_HD static inline int sr_is_wood_material_block(i32 id) {
    switch (id) {
    case 5:
    case 17:
    case 25:
    case 47:
    case 53:
    case 54:
    case 58:
    case 63:
    case 64:
    case 68:
    case 72:
    case 84:
    case 85:
    case 96:
    case 99:
    case 100:
    case 107:
    case 125:
    case 126:
    case 134:
    case 135:
    case 136:
    case 146:
    case 151:
    case 162:
    case 163:
    case 164:
    case 176:
    case 177:
    case 178:
    case 183:
    case 184:
    case 185:
    case 186:
    case 187:
    case 188:
    case 189:
    case 190:
    case 191:
    case 192:
    case 193:
    case 194:
    case 195:
    case 196:
    case 197:
        return 1;
    default: return 0;
    }
}
/* TileEntityFurnace.getItemBurnTime (TileEntityFurnace.java:340-355). */
MC_HD static inline i32 sr_getItemBurnTime(SRStack stack) {
    i32 id;
    if (sr_isEmpty(stack)) return 0;
    id = stack.item;
    /* nested ternary, left to right */
    if (id == 126) return 150;                 /* WOODEN_SLAB Block.java:2539 */
    if (id == 35) return 100;                  /* WOOL Block.java:2440 */
    if (id == 171) return 67;                  /* CARPET Block.java:2585 */
    if (id == 65) return 300;                  /* LADDER Block.java:2474 */
    if (id == 143) return 100;                 /* WOODEN_BUTTON Block.java:2556 */
    if (sr_is_wood_material_block(id)) return 300;
    if (id == 173) return 16000;               /* COAL_BLOCK Block.java:2587 */
    if (id == 269 || id == 270 || id == 271) return 200; /* wooden shovel/pick/axe Item.java:1510-1512 */
    if (id == 268) return 200;                 /* wooden sword Item.java:1509 */
    if (id == 290) return 200;                 /* wooden hoe Item.java:1531 */
    if (id == 280) return 100;                 /* STICK Item.java:1521 */
    if (id == 261 || id == 346) return 300;    /* BOW / FISHING_ROD Item.java:1502,1588 */
    if (id == 323) return 200;                 /* SIGN Item.java:1564 */
    if (id == 263) return 1600;                /* COAL Item.java:1504 */
    if (id == 327) return 20000;               /* LAVA_BUCKET Item.java:1569 */
    if (id == 6 || id == 281) return 100;      /* SAPLING Block.java:2410 / BOWL Item.java:1522 */
    if (id == 369) return 2400;                /* BLAZE_ROD Item.java:1611 */
    if (id == 324 || id == 427 || id == 428 || id == 429 || id == 430 || id == 431)
        return 200;                            /* ItemDoor except iron Item.java:1565,1670-1674 */
    if (id == 333 || id == 444 || id == 445 || id == 446 || id == 447 || id == 448)
        return 400;                            /* ItemBoat Item.java:1575,1687-1691 */
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
        sr_mk(319, 1, 0),   /* porkchop */
        sr_mk(SR_BEEF, 1, 0),
        sr_mk(365, 1, 0),   /* chicken */
        sr_mk(411, 1, 0),   /* rabbit */
        sr_mk(423, 1, 0),   /* mutton */
        sr_mk(SR_POTATO, 1, 0),
        sr_mk(432, 1, 0),   /* chorus fruit */
        sr_mk(SR_FISH, 1, 0),
        sr_mk(SR_FISH, 1, 1),
        sr_mk(SR_FISH, 1, 2),
        sr_mk(SR_FISH, 1, 3),
        sr_mk(SR_IRON_INGOT, 1, 0),
        sr_mk(367, 1, 0),   /* rotten flesh */
        sr_mk(SR_COAL, 1, 0),
        sr_mk(BLK_IRON_ORE, 1, 7),
        sr_mk(SR_FISH, 1, 0),
        sr_empty(),
    };
    int i;
    for (i = 0; i < SR_NSMELT; ++i) out[i] = t[i];
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
    int i;
    for (i = 0; i < SR_NFUEL; ++i) out[i] = t[i];
}

MC_HD static inline void sr_run_dump(u32 *out) {
    SRRecipe R[SR_NRECIPES];
    int n = sr_build(R);
    SRStack smelt_in[SR_NSMELT], fuel_in[SR_NFUEL];
    int i, o = 0;
    sr_smelt_battery(smelt_in);
    sr_fuel_battery(fuel_in);
    for (i = 0; i < SR_NSMELT; ++i) {
        SRStack r = sr_getSmeltingResult(R, n, smelt_in[i]);
        out[o++] = (u32)r.item;
        out[o++] = (u32)r.count;
        out[o++] = (u32)r.meta;
    }
    for (i = 0; i < SR_NFUEL; ++i)
        out[o++] = (u32)sr_getItemBurnTime(fuel_in[i]);
}

#endif /* MC_SMELTING_RECIPES_H */
