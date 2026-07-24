/* stronghold_loot: vanilla 1.11.2 CHESTS_STRONGHOLD_{CORRIDOR,LIBRARY,CROSSING}
 * tables embedded for LootTable.generateLootForPools + fillInventory.
 *
 * Source: assets/minecraft/loot_tables/chests/stronghold_*.json (MC 1.11.2 jar).
 * Engine: loot_table.h (SetCount, weight pick, EnchantWithLevels via et_build_list).
 * enchant_with_levels entries emit Items.ENCHANTED_BOOK (403) with a full
 * StoredEnchantments-equivalent list on TecStack (not packed into meta).
 *
 * Structure loot seeds: captured from the real place_blocks JavaRandom stream
 * at each placed chest (sh_record_chest / gm_stronghold_chest_info).
 *
 * fillInventory: shuffle empty slots + multi-stack split (LootTable.fillInventory).
 * PURE host/device. Does not mutate the loot_table.h battery goldens. */
#ifndef MC_STRONGHOLD_LOOT_H
#define MC_STRONGHOLD_LOOT_H

#include "loot_table.h"
#include "tile_entity_chest.h"

enum {
    SHL_CORRIDOR = 0,
    SHL_LIBRARY  = 1,
    SHL_CROSSING = 2,
    SHL_NUM_TABLES = 3,
    SHL_MAX_STACKS = 64
};

/* Legacy item ids matching items_core / 1.11.2. */
enum {
    SHL_ENDER_PEARL      = 368,
    SHL_DIAMOND          = 264,
    SHL_IRON_INGOT       = 265,
    SHL_GOLD_INGOT       = 266,
    SHL_REDSTONE         = 331,
    SHL_BREAD            = 297,
    SHL_APPLE            = 260,
    SHL_IRON_PICKAXE     = 257,
    SHL_IRON_SWORD       = 267,
    SHL_IRON_CHESTPLATE  = 307,
    SHL_IRON_HELMET      = 306,
    SHL_IRON_LEGGINGS    = 308,
    SHL_IRON_BOOTS       = 309,
    SHL_GOLDEN_APPLE     = 322,
    SHL_SADDLE           = 329,
    SHL_IRON_HORSE_ARMOR = 417,
    SHL_GOLD_HORSE_ARMOR = 418,
    SHL_DIAMOND_HORSE_ARMOR = 419,
    SHL_BOOK             = 340,
    SHL_ENCHANTED_BOOK   = 403,
    SHL_PAPER            = 339,
    SHL_MAP              = 395, /* empty map */
    SHL_COMPASS          = 345,
    SHL_COAL             = 263,
    SHL_RED_MUSHROOM     = 40,
    SHL_BROWN_MUSHROOM   = 39,
    SHL_IRON_AXE         = 258,
    SHL_GOLDEN_PICKAXE   = 285,
    SHL_DIAMOND_PICKAXE  = 278
};

MC_HD static inline void shl_entry_count(LtEntry *e, i32 item, i32 weight,
                                         float cmin, float cmax) {
    e->item = item; e->meta = 0; e->weight = weight; e->quality = 0;
    e->n_funcs = 1;
    e->funcs[0].kind = LT_FN_SET_COUNT;
    e->funcs[0].count.min = cmin; e->funcs[0].count.max = cmax;
    e->funcs[0].limit = 0;
}

MC_HD static inline void shl_entry_one(LtEntry *e, i32 item, i32 weight) {
    e->item = item; e->meta = 0; e->weight = weight; e->quality = 0;
    e->n_funcs = 0;
}

/* enchant_with_levels: levels fixed range, treasure flag. Starts as book;
 * LT_FN_ENCHANT_LEVELS converts to enchanted_book (403) + StoredEnchantments list. */
MC_HD static inline void shl_entry_enchant_book(LtEntry *e, i32 weight,
                                                float level_min, float level_max,
                                                int treasure) {
    e->item = SHL_BOOK; e->meta = 0; e->weight = weight; e->quality = 0;
    e->n_funcs = 1;
    e->funcs[0].kind = LT_FN_ENCHANT_LEVELS;
    e->funcs[0].count.min = level_min;
    e->funcs[0].count.max = level_max;
    e->funcs[0].limit = treasure ? 1 : 0;
}

/* Expand LT_MAX_ENTRIES for stronghold tables (corridor has 19 entries). */
#define SHL_MAX_ENTRIES 24
#define SHL_MAX_POOLS   2

typedef struct {
    LtRange rolls;
    LtRange bonus_rolls;
    i32 n_entries;
    LtEntry entries[SHL_MAX_ENTRIES];
} ShlPool;

typedef struct {
    i32 n_pools;
    ShlPool pools[SHL_MAX_POOLS];
} ShlTable;

MC_HD static inline void shl_table_get(int table_id, ShlTable *t) {
    int i, j;
    t->n_pools = 0;
    for (i = 0; i < SHL_MAX_POOLS; ++i) {
        t->pools[i].rolls.min = 0; t->pools[i].rolls.max = 0;
        t->pools[i].bonus_rolls.min = 0; t->pools[i].bonus_rolls.max = 0;
        t->pools[i].n_entries = 0;
        for (j = 0; j < SHL_MAX_ENTRIES; ++j) {
            t->pools[i].entries[j].item = LT_AIR;
            t->pools[i].entries[j].meta = 0;
            t->pools[i].entries[j].weight = 0;
            t->pools[i].entries[j].quality = 0;
            t->pools[i].entries[j].n_funcs = 0;
        }
    }

    if (table_id == SHL_CORRIDOR) {
        /* chests/stronghold_corridor.json: rolls 2..3, 19 entries */
        ShlPool *p = &t->pools[0];
        t->n_pools = 1;
        p->rolls.min = 2.0f; p->rolls.max = 3.0f;
        p->bonus_rolls.min = 0.0f; p->bonus_rolls.max = 0.0f;
        p->n_entries = 19;
        shl_entry_one(&p->entries[0], SHL_ENDER_PEARL, 10);
        shl_entry_count(&p->entries[1], SHL_DIAMOND, 3, 1, 3);
        shl_entry_count(&p->entries[2], SHL_IRON_INGOT, 10, 1, 5);
        shl_entry_count(&p->entries[3], SHL_GOLD_INGOT, 5, 1, 3);
        shl_entry_count(&p->entries[4], SHL_REDSTONE, 5, 4, 9);
        shl_entry_count(&p->entries[5], SHL_BREAD, 15, 1, 3);
        shl_entry_count(&p->entries[6], SHL_APPLE, 15, 1, 3);
        shl_entry_one(&p->entries[7], SHL_IRON_PICKAXE, 5);
        shl_entry_one(&p->entries[8], SHL_IRON_SWORD, 5);
        shl_entry_one(&p->entries[9], SHL_IRON_CHESTPLATE, 5);
        shl_entry_one(&p->entries[10], SHL_IRON_HELMET, 5);
        shl_entry_one(&p->entries[11], SHL_IRON_LEGGINGS, 5);
        shl_entry_one(&p->entries[12], SHL_IRON_BOOTS, 5);
        shl_entry_one(&p->entries[13], SHL_GOLDEN_APPLE, 1);
        shl_entry_one(&p->entries[14], SHL_SADDLE, 1);
        shl_entry_one(&p->entries[15], SHL_IRON_HORSE_ARMOR, 1);
        shl_entry_one(&p->entries[16], SHL_GOLD_HORSE_ARMOR, 1);
        shl_entry_one(&p->entries[17], SHL_DIAMOND_HORSE_ARMOR, 1);
        /* enchant_with_levels levels=30 treasure -> enchanted_book */
        shl_entry_enchant_book(&p->entries[18], 1, 30.0f, 30.0f, 1);
    } else if (table_id == SHL_LIBRARY) {
        /* chests/stronghold_library.json: rolls 2..10 */
        ShlPool *p = &t->pools[0];
        t->n_pools = 1;
        p->rolls.min = 2.0f; p->rolls.max = 10.0f;
        p->bonus_rolls.min = 0.0f; p->bonus_rolls.max = 0.0f;
        p->n_entries = 5;
        shl_entry_count(&p->entries[0], SHL_BOOK, 20, 1, 3);
        shl_entry_count(&p->entries[1], SHL_PAPER, 20, 2, 7);
        shl_entry_one(&p->entries[2], SHL_MAP, 1);
        shl_entry_one(&p->entries[3], SHL_COMPASS, 1);
        /* enchant_with_levels levels=30 treasure weight 10 */
        shl_entry_enchant_book(&p->entries[4], 10, 30.0f, 30.0f, 1);
    } else if (table_id == SHL_CROSSING) {
        /* chests/stronghold_crossing.json: rolls 1..4 */
        ShlPool *p = &t->pools[0];
        t->n_pools = 1;
        p->rolls.min = 1.0f; p->rolls.max = 4.0f;
        p->bonus_rolls.min = 0.0f; p->bonus_rolls.max = 0.0f;
        p->n_entries = 8;
        shl_entry_count(&p->entries[0], SHL_IRON_INGOT, 10, 1, 5);
        shl_entry_count(&p->entries[1], SHL_GOLD_INGOT, 5, 1, 3);
        shl_entry_count(&p->entries[2], SHL_REDSTONE, 5, 4, 9);
        shl_entry_count(&p->entries[3], SHL_COAL, 10, 3, 8);
        shl_entry_count(&p->entries[4], SHL_BREAD, 15, 1, 3);
        shl_entry_count(&p->entries[5], SHL_APPLE, 15, 1, 3);
        shl_entry_one(&p->entries[6], SHL_IRON_PICKAXE, 1);
        shl_entry_enchant_book(&p->entries[7], 1, 30.0f, 30.0f, 1);
    }
}

/* LootPool.createLootRoll / generateLoot over ShlPool (same as lt_*). */
MC_HD static inline void shl_create_loot_roll(const ShlPool *pool, JavaRandom *r,
                                              const LtContext *ctx,
                                              LtStack *out, i32 *n_out, i32 max_out) {
    i32 weights[SHL_MAX_ENTRIES];
    i32 total = 0;
    i32 ei;
    for (ei = 0; ei < pool->n_entries; ++ei) {
        i32 w = lt_effective_weight(pool->entries[ei].weight,
                                    pool->entries[ei].quality, ctx->luck);
        weights[ei] = w;
        if (w > 0) total += w;
    }
    if (total == 0) return;
    {
        i32 k = jrand_int_bound(r, total);
        for (ei = 0; ei < pool->n_entries; ++ei) {
            if (weights[ei] <= 0) continue;
            k -= weights[ei];
            if (k < 0) {
                lt_add_loot_entry(&pool->entries[ei], r, ctx, out, n_out, max_out);
                return;
            }
        }
    }
}

MC_HD static inline void shl_pool_generate(const ShlPool *pool, JavaRandom *r,
                                           const LtContext *ctx,
                                           LtStack *out, i32 *n_out, i32 max_out) {
    i32 n_rolls = lt_rvr_generate_int(r, pool->rolls)
        + mc_floorf(lt_rvr_generate_float(r, pool->bonus_rolls) * ctx->luck);
    i32 j;
    for (j = 0; j < n_rolls; ++j)
        shl_create_loot_roll(pool, r, ctx, out, n_out, max_out);
}

MC_HD static inline i32 shl_generate(const ShlTable *table, JavaRandom *r,
                                     const LtContext *ctx,
                                     LtStack *out, i32 max_out) {
    i32 n = 0;
    i32 p;
    for (p = 0; p < table->n_pools; ++p)
        shl_pool_generate(&table->pools[p], r, ctx, out, &n, max_out);
    return n;
}

/* LootTable.getEmptySlotsRandomized + Collections.shuffle (Fisher-Yates via
 * nextInt). */
MC_HD static inline void shl_shuffle_ints(i32 *a, i32 n, JavaRandom *r) {
    i32 i;
    for (i = n; i > 1; --i) {
        i32 j = jrand_int_bound(r, i);
        i32 tmp = a[i - 1];
        a[i - 1] = a[j];
        a[j] = tmp;
    }
}

/* Simplified shuffleItems: split oversized stacks then Fisher-Yates. Enough
 * for structure chests; not every edge of the Java multi-split path. */
MC_HD static inline i32 shl_shuffle_items(LtStack *stacks, i32 n, i32 empty_slots,
                                          JavaRandom *r, LtStack *work, i32 work_cap) {
    i32 wn = 0;
    i32 i;
    (void)empty_slots;
    for (i = 0; i < n && wn < work_cap; ++i) {
        if (lt_stack_is_empty(stacks[i])) continue;
        if (stacks[i].count > 1 && wn + 1 < work_cap && jrand_int_bound(r, 2) == 0) {
            i32 half = stacks[i].count / 2;
            if (half < 1) half = 1;
            if (half >= stacks[i].count) half = stacks[i].count - 1;
            work[wn++] = lt_stack_mk(stacks[i].item, stacks[i].count - half, stacks[i].meta);
            work[wn++] = lt_stack_mk(stacks[i].item, half, stacks[i].meta);
        } else {
            work[wn++] = stacks[i];
        }
    }
    /* Fisher-Yates on work stacks via index array */
    {
        i32 idx[SHL_MAX_STACKS];
        i32 k;
        if (wn > SHL_MAX_STACKS) wn = SHL_MAX_STACKS;
        for (k = 0; k < wn; ++k) idx[k] = k;
        for (k = wn; k > 1; --k) {
            i32 j = jrand_int_bound(r, k);
            i32 tmp = idx[k - 1];
            idx[k - 1] = idx[j];
            idx[j] = tmp;
        }
        for (k = 0; k < wn; ++k) stacks[k] = work[idx[k]];
    }
    return wn;
}

/* LootTable.fillInventory into a 27-slot TeChest. */
MC_HD static inline void shl_fill_chest(TeChest *chest, int table_id, i64 loot_seed) {
    ShlTable table;
    LtContext ctx;
    JavaRandom rng;
    LtStack stacks[SHL_MAX_STACKS];
    LtStack work[SHL_MAX_STACKS];
    i32 empty[TEC_SLOTS];
    i32 n_empty = 0, n, i;

    if (!chest || table_id < 0 || table_id >= SHL_NUM_TABLES) return;

    shl_table_get(table_id, &table);
    ctx.luck = 0.0f;
    ctx.has_killer = 0;
    ctx.looting_level = 0;
    jrand_set(&rng, loot_seed);
    n = shl_generate(&table, &rng, &ctx, stacks, SHL_MAX_STACKS);

    for (i = 0; i < TEC_SLOTS; ++i) {
        if (tec_is_empty(&chest->slots[i])) empty[n_empty++] = i;
    }
    if (n_empty <= 0) return;

    n = shl_shuffle_items(stacks, n, n_empty, &rng, work, SHL_MAX_STACKS);
    shl_shuffle_ints(empty, n_empty, &rng);

    for (i = 0; i < n && n_empty > 0; ++i) {
        if (lt_stack_is_empty(stacks[i])) {
            --n_empty;
            continue;
        }
        {
            i32 slot = empty[--n_empty];
            TecStack ts = tec_mk(stacks[i].item, stacks[i].count, stacks[i].meta);
            {
                int e, ne = stacks[i].n_enchants;
                if (ne > TEC_MAX_ENCHANTS) ne = TEC_MAX_ENCHANTS;
                ts.n_enchants = ne;
                for (e = 0; e < ne; ++e) {
                    ts.enchants[e].id = stacks[i].ench_id[e];
                    ts.enchants[e].level = stacks[i].ench_lvl[e];
                }
            }
            tec_set_slot(chest, slot, ts);
        }
    }
}

/* Legacy position-mix seed (tests may still call; not used for structure chests). */
MC_HD static inline i64 shl_pos_loot_seed(i64 world_seed, int x, int y, int z) {
    return world_seed
         ^ ((i64)(i32)x * 3129871LL)
         ^ ((i64)(i32)z * 116129781LL)
         ^ ((i64)(i32)y * 42317861LL);
}

#endif /* MC_STRONGHOLD_LOOT_H */
