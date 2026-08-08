/* loot_table: deterministic LootTable roll for a fixed embedded table set.
 *
 * PORT TARGETS (net/minecraft/world/storage/loot/*, vanilla 1.11.2):
 *   RandomValueRange.generateInt / generateFloat
 *   MathHelper.floor(float) + MathHelper.getInt + MathHelper.nextFloat
 *   LootEntry.getEffectiveWeight
 *   LootPool.createLootRoll (weight pick) + LootPool.generateLoot
 *   LootEntryItem.addLoot (ItemStack create + functions + max-stack split)
 *   functions.SetCount.apply
 *   functions.LootingEnchantBonus.apply (simplified context: has_killer + looting_level)
 *   LootTable.generateLootForPools (single-level pools only; no nested tables)
 *
 * EMBEDDED DATA (not JSON parse): 3 minimal tables with weight, count range, optional looting.
 * Conditions always true (ALWAYS_TRUE). Luck/looting come from LtContext. JavaRandom seeded
 * identically. Emit itemId+count+meta sequence for N rolls per table.
 *
 * CUT: NBT functions, entity-property conditions, nested LootEntryTable, empty entries,
 * quality beyond getEffectiveWeight, inventory fill/shuffle.
 *
 * PURE: __host__ __device__ MC_HD. CPU==CUDA bitwise. Build -ffp-contract=off / --fmad=false. */
#ifndef MC_LOOT_TABLE_H
#define MC_LOOT_TABLE_H

#include "mc.h"
#include "mc_math.h"
#include "mc_rng.h"
#include "enchant_table.h"

/* ---- item ids (legacy, same convention as items_core / crafting_recipes) ---- */
enum {
    LT_AIR         = 0,
    LT_APPLE       = 260,
    LT_BREAD       = 297,
    LT_COAL        = 263,
    LT_IRON_INGOT  = 265,
    LT_GOLD_INGOT  = 266,
    LT_DIAMOND     = 264,
    LT_STICK       = 280,
    LT_ROTTEN_FLESH= 367,
    LT_BONE        = 352,
    LT_MAX_STACK   = 64
};

/* ---- function kinds ---- */
enum {
    LT_FN_NONE            = 0,
    LT_FN_SET_COUNT       = 1,
    LT_FN_LOOTING         = 2,
    /* EnchantWithLevels: levels in count min/max; limit=1 means treasure. */
    LT_FN_ENCHANT_LEVELS  = 3,
    /* EnchantRandomly with an empty explicit list: registry-order choice. */
    LT_FN_ENCHANT_RANDOMLY = 4
};

#define LT_MAX_FUNCS   2
#define LT_MAX_ENTRIES 4
#define LT_MAX_POOLS   2
#define LT_MAX_STACKS  16   /* hard cap of stacks one generateLootForPools may emit */
#define LT_NUM_TABLES  3
#define LT_N_ROLLS     8    /* independent generateLootForPools calls per table */
#define LT_FIELDS_PER  (1 + LT_MAX_STACKS * 3)  /* n + (item,count,meta)*MAX */
#define LT_OUT         (LT_NUM_TABLES * LT_N_ROLLS * LT_FIELDS_PER)

typedef struct {
    float min, max;
} LtRange;

typedef struct {
    i32 kind;       /* LT_FN_* */
    LtRange count;
    i32 limit;      /* looting only; 0 = no limit */
} LtFunc;

typedef struct {
    i32 item;
    i32 meta;
    i32 weight;
    i32 quality;
    i32 n_funcs;
    LtFunc funcs[LT_MAX_FUNCS];
} LtEntry;

typedef struct {
    LtRange rolls;
    LtRange bonus_rolls;
    i32 n_entries;
    LtEntry entries[LT_MAX_ENTRIES];
} LtPool;

typedef struct {
    i32 n_pools;
    LtPool pools[LT_MAX_POOLS];
} LtTable;

typedef struct {
    float luck;
    i32 has_killer;     /* 1 => LootingEnchantBonus may run (EntityLivingBase killer) */
    i32 looting_level;  /* context.getLootingModifier() stand-in */
} LtContext;

enum { LT_MAX_ENCHANTS = 8 };
typedef struct {
    i32 item;
    i32 count;
    i32 meta;
    i32 n_enchants;
    i16 ench_id[LT_MAX_ENCHANTS];
    i16 ench_lvl[LT_MAX_ENCHANTS];
} LtStack;

/* ---- MathHelper helpers used by loot ---- */
MC_HD static inline i32 lt_math_get_int(JavaRandom *r, i32 minimum, i32 maximum) {
    /* MathHelper.getInt(Random, min, max) */
    return minimum >= maximum ? minimum : jrand_int_bound(r, maximum - minimum + 1) + minimum;
}

MC_HD static inline float lt_math_next_float(JavaRandom *r, float minimum, float maximum) {
    /* MathHelper.nextFloat */
    return minimum >= maximum ? minimum : jrand_float(r) * (maximum - minimum) + minimum;
}

MC_HD static inline i32 lt_math_round(float a) {
    /* Java Math.round(float) = (int)floor(a + 0.5f) */
    return mc_floorf(a + 0.5f);
}

/* ---- RandomValueRange ---- */
MC_HD static inline i32 lt_rvr_generate_int(JavaRandom *r, LtRange range) {
    /* RandomValueRange.generateInt: getInt(floor(min), floor(max)) */
    return lt_math_get_int(r, mc_floorf(range.min), mc_floorf(range.max));
}

MC_HD static inline float lt_rvr_generate_float(JavaRandom *r, LtRange range) {
    return lt_math_next_float(r, range.min, range.max);
}

/* ---- weight ---- */
MC_HD static inline i32 lt_effective_weight(i32 weight, i32 quality, float luck) {
    /* Math.max(MathHelper.floor(weight + quality * luck), 0) */
    i32 w = mc_floorf((float)weight + (float)quality * luck);
    return w > 0 ? w : 0;
}

/* ---- stack helpers ---- */
MC_HD static inline LtStack lt_stack_empty(void) {
    LtStack s;
    int i;
    s.item = LT_AIR; s.count = 0; s.meta = 0; s.n_enchants = 0;
    for (i = 0; i < LT_MAX_ENCHANTS; ++i) { s.ench_id[i] = 0; s.ench_lvl[i] = 0; }
    return s;
}

MC_HD static inline LtStack lt_stack_mk(i32 item, i32 count, i32 meta) {
    LtStack s = lt_stack_empty();
    s.item = item; s.count = count; s.meta = meta;
    return s;
}

MC_HD static inline int lt_stack_is_empty(LtStack s) {
    return s.item == LT_AIR || s.count <= 0;
}

/* ---- apply functions ---- */
/* Enchanted book: item 403 + StoredEnchantments-equivalent list (not packed meta). */
enum { LT_ENCHANTED_BOOK = 403, LT_BOOK = 340 };

MC_HD static inline LtStack lt_apply_func(LtStack stack, const LtFunc *fn, JavaRandom *r,
                                         const LtContext *ctx) {
    if (fn->kind == LT_FN_SET_COUNT) {
        /* SetCount.apply: stack.setCount(countRange.generateInt(rand)) */
        stack.count = lt_rvr_generate_int(r, fn->count);
        return stack;
    }
    if (fn->kind == LT_FN_LOOTING) {
        /* LootingEnchantBonus.apply (killer present + looting_level stand-in) */
        if (!ctx->has_killer)
            return stack;
        {
            i32 i = ctx->looting_level;
            if (i == 0)
                return stack;
            {
                float f = (float)i * lt_rvr_generate_float(r, fn->count);
                stack.count += lt_math_round(f);
                if (fn->limit != 0 && stack.count > fn->limit)
                    stack.count = fn->limit;
            }
        }
        return stack;
    }
    if (fn->kind == LT_FN_ENCHANT_LEVELS) {
        /* EnchantWithLevels -> EnchantmentHelper.addRandomEnchantment via
         * et_build_list (candidate datas, weighted pick, removeIncompatible).
         * Continues on the same JavaRandom; does not reseed or pack meta. */
        (void)ctx;
        {
            i32 level = lt_rvr_generate_int(r, fn->count);
            i32 allow_treasure = fn->limit != 0 ? 1 : 0;
            EtData list[ET_MAX_LIST];
            int item_kind = et_item_kind_from_id(stack.item);
            int n = et_build_list(r,
                                  item_kind >= 0 ? item_kind : ET_ITEM_BOOK,
                                  (int)level, allow_treasure,
                                  list, ET_MAX_LIST);
            int i;
            if (stack.item == LT_BOOK || stack.item == 0)
                stack.item = LT_ENCHANTED_BOOK;
            stack.count = 1;
            stack.meta = 0; /* identity is n_enchants list, not damage meta */
            stack.n_enchants = 0;
            for (i = 0; i < n && stack.n_enchants < LT_MAX_ENCHANTS; ++i) {
                stack.ench_id[stack.n_enchants] = (i16)list[i].id;
                stack.ench_lvl[stack.n_enchants] = (i16)list[i].level;
                stack.n_enchants++;
            }
        }
        return stack;
    }
    if (fn->kind == LT_FN_ENCHANT_RANDOMLY) {
        /* EnchantRandomly.apply: a book accepts every registered vanilla
         * enchantment, including treasure entries. Choose in registry order,
         * then choose uniformly between that enchantment's min/max levels. */
        int n_defs = 0;
        const EtDef *defs = et_defs(&n_defs);
        if (n_defs > 0) {
            const EtDef *d = &defs[jrand_int_bound(r, n_defs)];
            int level = lt_math_get_int(r, 1, d->max_level);
            if (stack.item == LT_BOOK || stack.item == 0)
                stack.item = LT_ENCHANTED_BOOK;
            stack.count = 1;
            stack.meta = 0;
            stack.n_enchants = 1;
            stack.ench_id[0] = (i16)d->id;
            stack.ench_lvl[0] = (i16)level;
        }
        return stack;
    }
    return stack;
}

/* ---- LootEntryItem.addLoot ---- */
MC_HD static inline void lt_add_loot_entry(const LtEntry *e, JavaRandom *r, const LtContext *ctx,
                                          LtStack *out, i32 *n_out, i32 max_out) {
    LtStack stack = lt_stack_mk(e->item, 1, e->meta);
    i32 fi;
    for (fi = 0; fi < e->n_funcs; ++fi)
        stack = lt_apply_func(stack, &e->funcs[fi], r, ctx);

    if (lt_stack_is_empty(stack))
        return;

    /* if count < itemStackLimit (64 for subset) add once; else split into max-size stacks.
     * Enchant lists only attach to count-1 books; splits of plain stacks drop enchants. */
    if (stack.count < LT_MAX_STACK) {
        if (*n_out < max_out)
            out[(*n_out)++] = stack;
    } else {
        i32 i = stack.count;
        while (i > 0 && *n_out < max_out) {
            i32 take = i < LT_MAX_STACK ? i : LT_MAX_STACK;
            out[(*n_out)++] = lt_stack_mk(stack.item, take, stack.meta);
            i -= take;
        }
    }
}

/* ---- LootPool.createLootRoll (weight pick) ---- */
MC_HD static inline void lt_create_loot_roll(const LtPool *pool, JavaRandom *r, const LtContext *ctx,
                                            LtStack *out, i32 *n_out, i32 max_out) {
    /* conditions always true; build effective-weight list in entry order */
    i32 weights[LT_MAX_ENTRIES];
    i32 total = 0;
    i32 ei;
    for (ei = 0; ei < pool->n_entries; ++ei) {
        i32 w = lt_effective_weight(pool->entries[ei].weight, pool->entries[ei].quality, ctx->luck);
        weights[ei] = w;
        if (w > 0)
            total += w;
    }
    if (total == 0)
        return;

    {
        i32 k = jrand_int_bound(r, total);
        for (ei = 0; ei < pool->n_entries; ++ei) {
            if (weights[ei] <= 0)
                continue;
            k -= weights[ei];
            if (k < 0) {
                lt_add_loot_entry(&pool->entries[ei], r, ctx, out, n_out, max_out);
                return;
            }
        }
    }
}

/* ---- LootPool.generateLoot ---- */
MC_HD static inline void lt_pool_generate(const LtPool *pool, JavaRandom *r, const LtContext *ctx,
                                         LtStack *out, i32 *n_out, i32 max_out) {
    /* pool conditions always true */
    i32 n_rolls = lt_rvr_generate_int(r, pool->rolls)
        + mc_floorf(lt_rvr_generate_float(r, pool->bonus_rolls) * ctx->luck);
    i32 j;
    for (j = 0; j < n_rolls; ++j)
        lt_create_loot_roll(pool, r, ctx, out, n_out, max_out);
}

/* ---- LootTable.generateLootForPools ---- */
MC_HD static inline i32 lt_generate(const LtTable *table, JavaRandom *r, const LtContext *ctx,
                                   LtStack *out, i32 max_out) {
    i32 n = 0;
    i32 p;
    for (p = 0; p < table->n_pools; ++p)
        lt_pool_generate(&table->pools[p], r, ctx, out, &n, max_out);
    return n;
}

/* ---- embedded minimal tables ---- */
MC_HD static inline void lt_table_get(int table_id, LtTable *t) {
    LtTable z;
    int i, j;
    /* zero */
    z.n_pools = 0;
    for (i = 0; i < LT_MAX_POOLS; ++i) {
        z.pools[i].rolls.min = 0; z.pools[i].rolls.max = 0;
        z.pools[i].bonus_rolls.min = 0; z.pools[i].bonus_rolls.max = 0;
        z.pools[i].n_entries = 0;
        for (j = 0; j < LT_MAX_ENTRIES; ++j) {
            z.pools[i].entries[j].item = LT_AIR;
            z.pools[i].entries[j].meta = 0;
            z.pools[i].entries[j].weight = 0;
            z.pools[i].entries[j].quality = 0;
            z.pools[i].entries[j].n_funcs = 0;
        }
    }

    if (table_id == 0) {
        /* Table 0: one pool, rolls=1; apple w=1 SetCount(1,1); bread w=3 SetCount(1,2) */
        LtPool *p = &z.pools[0];
        LtEntry *e;
        z.n_pools = 1;
        p->rolls.min = 1.0f; p->rolls.max = 1.0f;
        p->bonus_rolls.min = 0.0f; p->bonus_rolls.max = 0.0f;
        p->n_entries = 2;
        e = &p->entries[0];
        e->item = LT_APPLE; e->meta = 0; e->weight = 1; e->quality = 0; e->n_funcs = 1;
        e->funcs[0].kind = LT_FN_SET_COUNT;
        e->funcs[0].count.min = 1.0f; e->funcs[0].count.max = 1.0f; e->funcs[0].limit = 0;
        e = &p->entries[1];
        e->item = LT_BREAD; e->meta = 0; e->weight = 3; e->quality = 0; e->n_funcs = 1;
        e->funcs[0].kind = LT_FN_SET_COUNT;
        e->funcs[0].count.min = 1.0f; e->funcs[0].count.max = 2.0f; e->funcs[0].limit = 0;
    } else if (table_id == 1) {
        /* Table 1: rolls 2..4; iron w=2 SetCount(1,3); gold w=1 SetCount(2,2); coal w=5 no fn */
        LtPool *p = &z.pools[0];
        LtEntry *e;
        z.n_pools = 1;
        p->rolls.min = 2.0f; p->rolls.max = 4.0f;
        p->bonus_rolls.min = 0.0f; p->bonus_rolls.max = 0.0f;
        p->n_entries = 3;
        e = &p->entries[0];
        e->item = LT_IRON_INGOT; e->meta = 0; e->weight = 2; e->quality = 0; e->n_funcs = 1;
        e->funcs[0].kind = LT_FN_SET_COUNT;
        e->funcs[0].count.min = 1.0f; e->funcs[0].count.max = 3.0f; e->funcs[0].limit = 0;
        e = &p->entries[1];
        e->item = LT_GOLD_INGOT; e->meta = 0; e->weight = 1; e->quality = 0; e->n_funcs = 1;
        e->funcs[0].kind = LT_FN_SET_COUNT;
        e->funcs[0].count.min = 2.0f; e->funcs[0].count.max = 2.0f; e->funcs[0].limit = 0;
        e = &p->entries[2];
        e->item = LT_COAL; e->meta = 0; e->weight = 5; e->quality = 0; e->n_funcs = 0;
    } else if (table_id == 2) {
        /* Table 2: two pools.
         * Pool0 rolls=1: diamond w=1 q=2 SetCount(1,1); stick w=10 q=0 SetCount(1,4)
         * Pool1 rolls=1: rotten_flesh w=1 SetCount(0,2) + Looting(count 0..1, limit 0);
         *               bone w=1 SetCount(1,1) */
        LtPool *p;
        LtEntry *e;
        z.n_pools = 2;
        p = &z.pools[0];
        p->rolls.min = 1.0f; p->rolls.max = 1.0f;
        p->bonus_rolls.min = 0.0f; p->bonus_rolls.max = 0.0f;
        p->n_entries = 2;
        e = &p->entries[0];
        e->item = LT_DIAMOND; e->meta = 0; e->weight = 1; e->quality = 2; e->n_funcs = 1;
        e->funcs[0].kind = LT_FN_SET_COUNT;
        e->funcs[0].count.min = 1.0f; e->funcs[0].count.max = 1.0f; e->funcs[0].limit = 0;
        e = &p->entries[1];
        e->item = LT_STICK; e->meta = 0; e->weight = 10; e->quality = 0; e->n_funcs = 1;
        e->funcs[0].kind = LT_FN_SET_COUNT;
        e->funcs[0].count.min = 1.0f; e->funcs[0].count.max = 4.0f; e->funcs[0].limit = 0;
        p = &z.pools[1];
        p->rolls.min = 1.0f; p->rolls.max = 1.0f;
        p->bonus_rolls.min = 0.0f; p->bonus_rolls.max = 0.0f;
        p->n_entries = 2;
        e = &p->entries[0];
        e->item = LT_ROTTEN_FLESH; e->meta = 0; e->weight = 1; e->quality = 0; e->n_funcs = 2;
        e->funcs[0].kind = LT_FN_SET_COUNT;
        e->funcs[0].count.min = 0.0f; e->funcs[0].count.max = 2.0f; e->funcs[0].limit = 0;
        e->funcs[1].kind = LT_FN_LOOTING;
        e->funcs[1].count.min = 0.0f; e->funcs[1].count.max = 1.0f; e->funcs[1].limit = 0;
        e = &p->entries[1];
        e->item = LT_BONE; e->meta = 0; e->weight = 1; e->quality = 0; e->n_funcs = 1;
        e->funcs[0].kind = LT_FN_SET_COUNT;
        e->funcs[0].count.min = 1.0f; e->funcs[0].count.max = 1.0f; e->funcs[0].limit = 0;
    }

    *t = z;
}

/* ---- scenario context per (table, roll_idx) ---- */
MC_HD static inline LtContext lt_scenario_ctx(int table_id, int roll_idx) {
    LtContext c;
    c.luck = 0.0f;
    c.has_killer = 0;
    c.looting_level = 0;
    if (table_id == 2) {
        /* exercise quality*luck on pool0 and looting on pool1 */
        c.luck = (roll_idx & 1) ? 5.0f : 0.0f;
        c.has_killer = 1;
        c.looting_level = (roll_idx % 4); /* 0,1,2,3 */
    }
    return c;
}

MC_HD static inline i64 lt_scenario_seed(int table_id, int roll_idx) {
    /* Distinct JavaRandom seeds; fixed, reproducible. */
    return 0x4C4F4F54LL + (i64)table_id * 10007LL + (i64)roll_idx * 17LL;
}

/* ---- emit one generateLootForPools into fixed fields ---- */
MC_HD static inline void lt_run_one(int table_id, int roll_idx, u32 *out) {
    LtTable table;
    LtContext ctx;
    JavaRandom rng;
    LtStack stacks[LT_MAX_STACKS];
    i32 n, i, base;

    lt_table_get(table_id, &table);
    ctx = lt_scenario_ctx(table_id, roll_idx);
    jrand_set(&rng, lt_scenario_seed(table_id, roll_idx));
    n = lt_generate(&table, &rng, &ctx, stacks, LT_MAX_STACKS);

    base = 0;
    out[base++] = (u32)n;
    for (i = 0; i < LT_MAX_STACKS; ++i) {
        if (i < n) {
            out[base++] = (u32)stacks[i].item;
            out[base++] = (u32)stacks[i].count;
            out[base++] = (u32)stacks[i].meta;
        } else {
            out[base++] = 0;
            out[base++] = 0;
            out[base++] = 0;
        }
    }
}

MC_HD static inline void lt_run_battery(u32 *out) {
    int t, r;
    for (t = 0; t < LT_NUM_TABLES; ++t) {
        for (r = 0; r < LT_N_ROLLS; ++r) {
            int off = (t * LT_N_ROLLS + r) * LT_FIELDS_PER;
            lt_run_one(t, r, out + off);
        }
    }
}

#endif /* MC_LOOT_TABLE_H */
