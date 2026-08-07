/* enchant_table: ContainerEnchantment offer RNG (MC 1.11.2).
 *
 * PORT TARGETS:
 *   inventory/ContainerEnchantment.java
 *     onCraftMatrixChanged (level roll + clue pick), getEnchantmentList
 *   enchantment/EnchantmentHelper.java
 *     calcItemStackEnchantability, buildEnchantmentList, getEnchantmentDatas,
 *     removeIncompatible
 *   enchantment/Enchantment*.java (min/max/level/weight/treasure/compat)
 *   util/WeightedRandom.java getRandomItem
 *   util/math/MathHelper.clamp + Java Math.round(float)
 *
 * INPUTS (battery): xpSeed, bookshelves power (0/5/15), item kind
 *   (book / diamond sword / iron pickaxe). The live path additionally maps
 *   every vanilla sword, tool, bow, fishing rod, and armor item to the same
 *   capability/enchantability representation.
 * OUTPUT: for each case, 3 slot levels + per-slot enchant list + clue id/level.
 *
 * CUT: world bookshelf scan (power is an input), Forge onEnchantmentLevelSet /
 * getEnchantPower hooks (vanilla passthrough), treasure enchants (allowTreasure
 * false), and container/player mutation. PURE: __host__ __device__ MC_HD.
 * CPU==CUDA bitwise.
 * Build -ffp-contract=off / --fmad=false. */
#ifndef MC_ENCHANT_TABLE_H
#define MC_ENCHANT_TABLE_H

#include "mc.h"
#include "mc_math.h"
#include "mc_rng.h"

/* ---- item kinds ---- */
enum {
    ET_ITEM_BOOK          = 0,
    ET_ITEM_DIAMOND_SWORD = 1,
    ET_ITEM_IRON_PICK     = 2,
    ET_N_ITEMS            = 3
};

/* Live-only compact item description. The low byte is Item enchantability;
 * capability bits occupy the upper bytes. Values 0..2 remain the stable
 * oracle battery kinds above. */
#define ET_ITEM_GENERIC_BASE 0x10000000
#define ET_ITEM_GENERIC(ench, caps) \
    (ET_ITEM_GENERIC_BASE | ((ench) & 255) | ((caps) << 8))

/* ---- type bits (EnumEnchantmentType applicability) ---- */
enum {
    ET_T_ARMOR       = 1 << 0,
    ET_T_ARMOR_FEET  = 1 << 1,
    ET_T_ARMOR_LEGS  = 1 << 2,
    ET_T_ARMOR_CHEST = 1 << 3,
    ET_T_ARMOR_HEAD  = 1 << 4,
    ET_T_WEAPON      = 1 << 5,
    ET_T_DIGGER      = 1 << 6,
    ET_T_FISHING     = 1 << 7,
    ET_T_BREAKABLE   = 1 << 8,
    ET_T_BOW         = 1 << 9,
    ET_T_WEARABLE    = 1 << 10,
    ET_T_ALL         = 1 << 11
};

/* item capability masks */
enum {
    ET_CAP_SWORD = ET_T_WEAPON | ET_T_BREAKABLE,
    ET_CAP_PICK  = ET_T_DIGGER | ET_T_BREAKABLE,
    ET_CAP_BOW   = ET_T_BOW | ET_T_BREAKABLE,
    ET_CAP_FISH  = ET_T_FISHING | ET_T_BREAKABLE,
    ET_CAP_HEAD  = ET_T_ARMOR | ET_T_ARMOR_HEAD | ET_T_WEARABLE | ET_T_BREAKABLE,
    ET_CAP_CHEST = ET_T_ARMOR | ET_T_ARMOR_CHEST | ET_T_WEARABLE | ET_T_BREAKABLE,
    ET_CAP_LEGS  = ET_T_ARMOR | ET_T_ARMOR_LEGS | ET_T_WEARABLE | ET_T_BREAKABLE,
    ET_CAP_FEET  = ET_T_ARMOR | ET_T_ARMOR_FEET | ET_T_WEARABLE | ET_T_BREAKABLE
    /* book: special-cased via isAllowedOnBooks */
};

/* incompatibility groups (beyond same-id) */
enum {
    ET_G_NONE    = 0,
    ET_G_DAMAGE  = 1,  /* sharpness/smite/bane */
    ET_G_PROT    = 2,  /* protection variants; FALL special-cased */
    ET_G_SILK    = 3,
    ET_G_FORTUNE = 4,  /* digger fortune only vs silk; looting is LootBonus class */
    ET_G_LOOTBONUS = 5,/* any EnchantmentLootBonus: != SILK_TOUCH */
    ET_G_INFINITY = 6,
    ET_G_MENDING  = 7,
    ET_G_FROST    = 8,
    ET_G_DEPTH    = 9
};

/* protection subtypes for canApplyTogether FALL exception */
enum {
    ET_PROT_ALL = 0,
    ET_PROT_FIRE = 1,
    ET_PROT_FALL = 2,
    ET_PROT_BLAST = 3,
    ET_PROT_PROJ = 4
};

#define ET_MAX_ENCHANTS  32
#define ET_MAX_CAND      32
#define ET_MAX_LIST      16
#define ET_N_SEEDS       7
#define ET_N_SHELVES     3
#define ET_N_CASES       (ET_N_SEEDS * ET_N_SHELVES * ET_N_ITEMS)
/* per case: 3 levels + 3*(n + list[MAX]*2 + clue_id + clue_lvl) */
#define ET_SLOT_FIELDS   (1 + ET_MAX_LIST * 2 + 2)
#define ET_FIELDS_PER    (3 + 3 * ET_SLOT_FIELDS)
#define ET_OUT           (ET_N_CASES * ET_FIELDS_PER)

typedef struct {
    i32 id;
    i32 weight;       /* rarity weight */
    i32 max_level;
    i32 treasure;
    i32 type_bits;    /* EnumEnchantmentType */
    i32 group;
    i32 prot_type;    /* ET_PROT_* if group==PROT, else -1 */
} EtDef;

typedef struct {
    i32 id;
    i32 level;
    i32 weight;
} EtData;

/* ---- registry in vanilla registerEnchantments order ---- */
MC_HD static inline const EtDef *et_defs(int *n_out) {
    /* Static table rebuilt each call is fine for HD; constexpr-like constants. */
    static const EtDef T[ET_MAX_ENCHANTS] = {
        /* id, w, maxL, treas, type, group, prot */
        { 0, 10, 4, 0, ET_T_ARMOR,       ET_G_PROT,     ET_PROT_ALL },
        { 1,  5, 4, 0, ET_T_ARMOR,       ET_G_PROT,     ET_PROT_FIRE },
        { 2,  5, 4, 0, ET_T_ARMOR_FEET,  ET_G_PROT,     ET_PROT_FALL },
        { 3,  2, 4, 0, ET_T_ARMOR,       ET_G_PROT,     ET_PROT_BLAST },
        { 4,  5, 4, 0, ET_T_ARMOR,       ET_G_PROT,     ET_PROT_PROJ },
        { 5,  2, 3, 0, ET_T_ARMOR_HEAD,  ET_G_NONE,     -1 },
        { 6,  2, 1, 0, ET_T_ARMOR_HEAD,  ET_G_NONE,     -1 },
        { 7,  1, 3, 0, ET_T_ARMOR_CHEST, ET_G_NONE,     -1 },
        { 8,  2, 3, 0, ET_T_ARMOR_FEET,  ET_G_DEPTH,    -1 },
        { 9,  2, 2, 1, ET_T_ARMOR_FEET,  ET_G_FROST,    -1 },
        {10,  1, 1, 1, ET_T_WEARABLE,    ET_G_NONE,     -1 },
        {16, 10, 5, 0, ET_T_WEAPON,      ET_G_DAMAGE,   -1 },
        {17,  5, 5, 0, ET_T_WEAPON,      ET_G_DAMAGE,   -1 },
        {18,  5, 5, 0, ET_T_WEAPON,      ET_G_DAMAGE,   -1 },
        {19,  5, 2, 0, ET_T_WEAPON,      ET_G_NONE,     -1 },
        {20,  2, 2, 0, ET_T_WEAPON,      ET_G_NONE,     -1 },
        {21,  2, 3, 0, ET_T_WEAPON,      ET_G_LOOTBONUS,-1 },
        {22,  2, 3, 0, ET_T_WEAPON,      ET_G_NONE,     -1 },
        {32, 10, 5, 0, ET_T_DIGGER,      ET_G_NONE,     -1 },
        {33,  1, 1, 0, ET_T_DIGGER,      ET_G_SILK,     -1 },
        {34,  5, 3, 0, ET_T_BREAKABLE,   ET_G_NONE,     -1 },
        {35,  2, 3, 0, ET_T_DIGGER,      ET_G_LOOTBONUS,-1 }, /* fortune: LootBonus */
        {48, 10, 5, 0, ET_T_BOW,         ET_G_NONE,     -1 },
        {49,  2, 2, 0, ET_T_BOW,         ET_G_NONE,     -1 },
        {50,  2, 1, 0, ET_T_BOW,         ET_G_NONE,     -1 },
        {51,  1, 1, 0, ET_T_BOW,         ET_G_INFINITY, -1 },
        {61,  2, 3, 0, ET_T_FISHING,     ET_G_LOOTBONUS,-1 },
        {62,  2, 3, 0, ET_T_FISHING,     ET_G_NONE,     -1 },
        {70,  2, 1, 1, ET_T_BREAKABLE,   ET_G_MENDING,  -1 },
        {71,  1, 1, 1, ET_T_ALL,         ET_G_NONE,     -1 },
    };
    if (n_out) *n_out = 30;
    return T;
}

/* base Enchantment.getMinEnchantability = 1 + level*10 (used by several maxes) */
MC_HD static inline int et_base_min(int level) {
    return 1 + level * 10;
}

/* per-enchant min/max formulas (verbatim subclass overrides) */
MC_HD static inline int et_min_ench(int id, int level) {
    switch (id) {
    /* protection: min = type.min + (level-1)*type.levelCost */
    case 0: return 1 + (level - 1) * 11;   /* ALL */
    case 1: return 10 + (level - 1) * 8;   /* FIRE */
    case 2: return 5 + (level - 1) * 6;    /* FALL */
    case 3: return 5 + (level - 1) * 8;    /* BLAST */
    case 4: return 3 + (level - 1) * 6;    /* PROJECTILE */
    case 5: return 10 * level;             /* respiration */
    case 6: return 1;                      /* aqua affinity */
    case 7: return 10 + 20 * (level - 1);  /* thorns */
    case 8: return level * 10;             /* depth strider */
    case 9: return level * 10;             /* frost walker */
    case 10: return 25;                    /* binding curse */
    /* damage: base + (level-1)*step ; bases 1,5,5 steps 11,8,8 */
    case 16: return 1 + (level - 1) * 11;
    case 17: return 5 + (level - 1) * 8;
    case 18: return 5 + (level - 1) * 8;
    case 19: return 5 + 20 * (level - 1);  /* knockback */
    case 20: return 10 + 20 * (level - 1); /* fire aspect */
    case 21: return 15 + (level - 1) * 9;  /* looting */
    case 22: return 5 + (level - 1) * 9;   /* sweeping */
    case 32: return 1 + 10 * (level - 1);  /* efficiency */
    case 33: return 15;                    /* silk touch */
    case 34: return 5 + (level - 1) * 8;   /* unbreaking */
    case 35: return 15 + (level - 1) * 9;  /* fortune */
    case 48: return 1 + (level - 1) * 10;  /* power */
    case 49: return 12 + (level - 1) * 20; /* punch */
    case 50: return 20;                    /* flame */
    case 51: return 20;                    /* infinity */
    case 61: return 15 + (level - 1) * 9;  /* luck of the sea */
    case 62: return 15 + (level - 1) * 9;  /* lure */
    case 70: return level * 25;            /* mending */
    case 71: return 25;                    /* vanishing */
    default: return 1 + level * 10;
    }
}

MC_HD static inline int et_max_ench(int id, int level) {
    switch (id) {
    /* protection max = min + levelCost (not levelCostSpan) */
    case 0: return et_min_ench(id, level) + 11;
    case 1: return et_min_ench(id, level) + 8;
    case 2: return et_min_ench(id, level) + 6;
    case 3: return et_min_ench(id, level) + 8;
    case 4: return et_min_ench(id, level) + 6;
    case 5: return et_min_ench(id, level) + 30;
    case 6: return et_min_ench(id, level) + 40;
    /* thorns/knockback/fire/loot/digging/durability/silk/fishing use super.min + 50 */
    case 7: case 19: case 20: case 21: case 32: case 33: case 34: case 35:
    case 61: case 62:
        return et_base_min(level) + 50;
    case 8: return et_min_ench(id, level) + 15;
    case 9: return et_min_ench(id, level) + 15;
    case 10: return 50;
    /* damage threshold 20 */
    case 16: case 17: case 18:
        return et_min_ench(id, level) + 20;
    case 22: return et_min_ench(id, level) + 15; /* sweeping */
    case 48: return et_min_ench(id, level) + 15; /* power */
    case 49: return et_min_ench(id, level) + 25; /* punch */
    case 50: return 50;
    case 51: return 50;
    case 70: return et_min_ench(id, level) + 50;
    case 71: return 50;
    default: return et_min_ench(id, level) + 5;
    }
}

MC_HD static inline int et_item_enchantability(int item_kind) {
    /* book=1; diamond sword ToolMaterial.DIAMOND=10; iron pick IRON=14 */
    if (item_kind == ET_ITEM_BOOK) return 1;
    if (item_kind == ET_ITEM_DIAMOND_SWORD) return 10;
    if (item_kind == ET_ITEM_IRON_PICK) return 14;
    if ((item_kind & 0xff000000) == ET_ITEM_GENERIC_BASE)
        return item_kind & 255;
    return 0;
}

MC_HD static inline int et_item_caps(int item_kind) {
    if (item_kind == ET_ITEM_DIAMOND_SWORD) return ET_CAP_SWORD;
    if (item_kind == ET_ITEM_IRON_PICK) return ET_CAP_PICK;
    if ((item_kind & 0xff000000) == ET_ITEM_GENERIC_BASE)
        return (item_kind >> 8) & 0xffff;
    return 0; /* book handled separately */
}

MC_HD static inline int et_item_is_book(int item_kind) {
    return item_kind == ET_ITEM_BOOK;
}

/* Numeric item ids are stable for 1.11.2. Returns -1 when the item cannot
 * receive any table offer. ItemHoe has no enchantability override in 1.11.2
 * and therefore correctly remains unsupported here. */
MC_HD static inline int et_item_kind_from_id(int item_id) {
    int ench = 0, caps = 0;
    if (item_id == 340) return ET_ITEM_BOOK;
    if (item_id == 261) return ET_ITEM_GENERIC(1, ET_CAP_BOW);
    if (item_id == 346) return ET_ITEM_GENERIC(1, ET_CAP_FISH);
    if (item_id == 267 || item_id == 256 || item_id == 257 || item_id == 258
            || item_id == 292) ench = 14; /* iron tools; hoe maps to no caps below */
    if (item_id == 268 || item_id == 269 || item_id == 270 || item_id == 271)
        ench = 15;
    if (item_id == 272 || item_id == 273 || item_id == 274 || item_id == 275)
        ench = 5;
    if (item_id == 276 || item_id == 277 || item_id == 278 || item_id == 279)
        ench = 10;
    if (item_id == 283 || item_id == 284 || item_id == 285 || item_id == 286)
        ench = 22;
    if (item_id == 267 || item_id == 268 || item_id == 272
            || item_id == 276 || item_id == 283)
        caps = ET_CAP_SWORD;
    else if ((item_id >= 256 && item_id <= 258)
            || (item_id >= 269 && item_id <= 271)
            || (item_id >= 273 && item_id <= 275)
            || (item_id >= 277 && item_id <= 279)
            || (item_id >= 284 && item_id <= 286))
        caps = ET_CAP_PICK;
    if (caps && ench) return ET_ITEM_GENERIC(ench, caps);

    /* Armor ids are four consecutive head/chest/legs/feet pieces. */
    if (item_id >= 298 && item_id <= 317) {
        int material = (item_id - 298) / 4;
        int slot = (item_id - 298) & 3;
        static const int armor_ench[5] = {15, 12, 9, 10, 25};
        /* Registry order is leather, chain, iron, diamond, gold. */
        ench = armor_ench[material];
        caps = slot == 0 ? ET_CAP_HEAD
             : slot == 1 ? ET_CAP_CHEST
             : slot == 2 ? ET_CAP_LEGS : ET_CAP_FEET;
        return ET_ITEM_GENERIC(ench, caps);
    }
    return -1;
}

/* EnumEnchantmentType.canEnchantItem for our item subset */
MC_HD static inline int et_type_matches(int type_bits, int item_caps) {
    if (type_bits & ET_T_ALL) {
        /* ALL (vanishing): EnumEnchantmentType.ALL walks every other type.
         * Our items only need weapon/digger/breakable caps. */
        return item_caps != 0;
    }
    return (type_bits & item_caps) != 0;
}

MC_HD static inline int et_can_apply_at_table(const EtDef *d, int item_kind) {
    if (et_item_is_book(item_kind))
        return 1; /* isAllowedOnBooks default true for all vanilla */
    return et_type_matches(d->type_bits, et_item_caps(item_kind));
}

/* bidirectional canApplyTogether (func_191560_c) */
MC_HD static inline int et_can_apply_together(const EtDef *a, const EtDef *b) {
    if (a->id == b->id) return 0;
    /* protection group */
    if (a->group == ET_G_PROT && b->group == ET_G_PROT) {
        if (a->prot_type == b->prot_type) return 0;
        if (a->prot_type == ET_PROT_FALL || b->prot_type == ET_PROT_FALL) return 1;
        return 0;
    }
    /* damage group */
    if (a->group == ET_G_DAMAGE && b->group == ET_G_DAMAGE) return 0;
    /* silk touch vs fortune (explicit) + any LootBonus vs silk */
    if ((a->group == ET_G_SILK && b->group == ET_G_LOOTBONUS) ||
        (b->group == ET_G_SILK && a->group == ET_G_LOOTBONUS))
        return 0;
    if ((a->group == ET_G_SILK && b->id == 35) || (b->group == ET_G_SILK && a->id == 35))
        return 0;
    /* infinity vs mending */
    if ((a->group == ET_G_INFINITY && b->group == ET_G_MENDING) ||
        (b->group == ET_G_INFINITY && a->group == ET_G_MENDING))
        return 0;
    /* frost vs depth */
    if ((a->group == ET_G_FROST && b->group == ET_G_DEPTH) ||
        (b->group == ET_G_FROST && a->group == ET_G_DEPTH))
        return 0;
    return 1;
}

MC_HD static inline int et_find_def(int id, const EtDef *defs, int n) {
    int i;
    for (i = 0; i < n; ++i)
        if (defs[i].id == id) return i;
    return -1;
}

/* MathHelper.clamp(int) */
MC_HD static inline int et_clamp_i(int num, int minv, int maxv) {
    return num < minv ? minv : (num > maxv ? maxv : num);
}

/* Java Math.round(float) */
MC_HD static inline int et_round_f(float a) {
    return mc_floorf(a + 0.5f);
}

/* calcItemStackEnchantability */
MC_HD static inline int et_calc_level(JavaRandom *rand, int enchant_num, int power,
                                        int item_kind) {
    int i = et_item_enchantability(item_kind);
    int j;
    if (i <= 0) return 0;
    if (power > 15) power = 15;
    j = jrand_int_bound(rand, 8) + 1 + (power >> 1) + jrand_int_bound(rand, power + 1);
    if (enchant_num == 0) {
        int v = j / 3;
        return v > 1 ? v : 1;
    }
    if (enchant_num == 1)
        return j * 2 / 3 + 1;
    {
        int v = power * 2;
        return j > v ? j : v;
    }
}

/* getEnchantmentDatas: candidates at modified cost */
MC_HD static inline int et_get_enchantment_datas(int cost, int item_kind, int allow_treasure,
                                                  EtData *out, int out_cap) {
    int n_defs = 0;
    const EtDef *defs = et_defs(&n_defs);
    int n = 0;
    int di;
    for (di = 0; di < n_defs; ++di) {
        const EtDef *d = &defs[di];
        int lvl;
        if (d->treasure && !allow_treasure) continue;
        if (!et_can_apply_at_table(d, item_kind)) continue;
        for (lvl = d->max_level; lvl >= 1; --lvl) {
            if (cost >= et_min_ench(d->id, lvl) && cost <= et_max_ench(d->id, lvl)) {
                if (n < out_cap) {
                    out[n].id = d->id;
                    out[n].level = lvl;
                    out[n].weight = d->weight;
                    n++;
                }
                break;
            }
        }
    }
    return n;
}

/* WeightedRandom.getTotalWeight + getRandomItem */
MC_HD static inline int et_weighted_pick(JavaRandom *rand, EtData *list, int n) {
    int total = 0;
    int i, w;
    for (i = 0; i < n; ++i) total += list[i].weight;
    if (total <= 0) return -1;
    w = jrand_int_bound(rand, total);
    for (i = 0; i < n; ++i) {
        w -= list[i].weight;
        if (w < 0) return i;
    }
    return -1;
}

/* removeIncompatible: drop candidates that cannot apply with last chosen */
MC_HD static inline int et_remove_incompatible(EtData *list, int n, int chosen_id) {
    int n_defs = 0;
    const EtDef *defs = et_defs(&n_defs);
    int ci = et_find_def(chosen_id, defs, n_defs);
    int w = 0, r;
    if (ci < 0) return n;
    for (r = 0; r < n; ++r) {
        int oi = et_find_def(list[r].id, defs, n_defs);
        if (oi >= 0 && et_can_apply_together(&defs[ci], &defs[oi])) {
            list[w++] = list[r];
        }
    }
    return w;
}

/* buildEnchantmentList */
MC_HD static inline int et_build_list(JavaRandom *rand, int item_kind, int level,
                                        int allow_treasure, EtData *out, int out_cap) {
    int ench = et_item_enchantability(item_kind);
    int cost, n_out = 0;
    float f;
    EtData cand[ET_MAX_CAND];
    int n_cand;
    if (ench <= 0) return 0;
    cost = level + 1
         + jrand_int_bound(rand, ench / 4 + 1)
         + jrand_int_bound(rand, ench / 4 + 1);
    f = (jrand_float(rand) + jrand_float(rand) - 1.0f) * 0.15f;
    cost = et_clamp_i(et_round_f((float)cost + (float)cost * f), 1, 0x7fffffff);
    n_cand = et_get_enchantment_datas(cost, item_kind, allow_treasure, cand, ET_MAX_CAND);
    if (n_cand > 0) {
        int pick = et_weighted_pick(rand, cand, n_cand);
        if (pick >= 0 && n_out < out_cap) {
            out[n_out++] = cand[pick];
        }
        while (jrand_int_bound(rand, 50) <= cost) {
            if (n_out > 0)
                n_cand = et_remove_incompatible(cand, n_cand, out[n_out - 1].id);
            if (n_cand <= 0) break;
            pick = et_weighted_pick(rand, cand, n_cand);
            if (pick < 0) break;
            if (n_out < out_cap) out[n_out++] = cand[pick];
            cost /= 2;
        }
    }
    return n_out;
}

/* getEnchantmentList: reseed xpSeed+slot, build, book may drop one */
MC_HD static inline int et_get_enchantment_list(JavaRandom *rand, i32 xp_seed, int slot,
                                                 int level, int item_kind,
                                                 EtData *out, int out_cap) {
    int n;
    jrand_set(rand, (i64)(xp_seed + slot));
    n = et_build_list(rand, item_kind, level, 0, out, out_cap);
    if (et_item_is_book(item_kind) && n > 1) {
        int rem = jrand_int_bound(rand, n);
        int i;
        for (i = rem; i < n - 1; ++i) out[i] = out[i + 1];
        n--;
    }
    return n;
}

/* Full offer generation mirroring onCraftMatrixChanged (power given). */
typedef struct {
    i32 levels[3];
    i32 clue_id[3];
    i32 clue_lvl[3];
    i32 n_list[3];
    EtData lists[3][ET_MAX_LIST];
} EtOffer;

MC_HD static inline void et_compute_offers(i32 xp_seed, int power, int item_kind,
                                             EtOffer *o) {
    JavaRandom rand;
    int i1, j1;
    jrand_set(&rand, (i64)xp_seed);
    for (i1 = 0; i1 < 3; ++i1) {
        o->levels[i1] = et_calc_level(&rand, i1, power, item_kind);
        o->clue_id[i1] = -1;
        o->clue_lvl[i1] = -1;
        o->n_list[i1] = 0;
        if (o->levels[i1] < i1 + 1)
            o->levels[i1] = 0;
        /* Forge onEnchantmentLevelSet: vanilla identity */
    }
    for (j1 = 0; j1 < 3; ++j1) {
        if (o->levels[j1] > 0) {
            int n = et_get_enchantment_list(&rand, xp_seed, j1, o->levels[j1],
                                             item_kind, o->lists[j1], ET_MAX_LIST);
            o->n_list[j1] = n;
            if (n > 0) {
                int pick = jrand_int_bound(&rand, n);
                o->clue_id[j1] = o->lists[j1][pick].id;
                o->clue_lvl[j1] = o->lists[j1][pick].level;
            }
        }
    }
}

MC_HD static inline void et_emit_offer(const EtOffer *o, u32 *out, int *k) {
    int s, i;
    for (s = 0; s < 3; ++s)
        out[(*k)++] = (u32)o->levels[s];
    for (s = 0; s < 3; ++s) {
        out[(*k)++] = (u32)o->n_list[s];
        for (i = 0; i < ET_MAX_LIST; ++i) {
            if (i < o->n_list[s]) {
                out[(*k)++] = (u32)o->lists[s][i].id;
                out[(*k)++] = (u32)o->lists[s][i].level;
            } else {
                out[(*k)++] = 0;
                out[(*k)++] = 0;
            }
        }
        out[(*k)++] = (u32)o->clue_id[s];
        out[(*k)++] = (u32)o->clue_lvl[s];
    }
}

MC_HD static inline void et_run_battery(u32 *out) {
    static const i32 seeds[ET_N_SEEDS] = {
        0, 1, 42, 12345, 0x12345678, -1, 999999
    };
    static const int shelves[ET_N_SHELVES] = { 0, 5, 15 };
    int k = 0;
    int si, sh, it;
    for (si = 0; si < ET_N_SEEDS; ++si) {
        for (sh = 0; sh < ET_N_SHELVES; ++sh) {
            for (it = 0; it < ET_N_ITEMS; ++it) {
                EtOffer o;
                et_compute_offers(seeds[si], shelves[sh], it, &o);
                et_emit_offer(&o, out, &k);
            }
        }
    }
}

#endif /* MC_ENCHANT_TABLE_H */
