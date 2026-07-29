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
 * fillInventory (LootTable 1.11.2): getEmptySlotsRandomized first, then
 * shuffleItems multi-pass split (MathHelper.getInt + nextBoolean) + Collections.shuffle.
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

/* Collections.shuffle(List, Random): Fisher-Yates from the end via nextInt(i). */
MC_HD static inline void shl_shuffle_ints(i32 *a, i32 n, JavaRandom *r) {
    i32 i;
    for (i = n; i > 1; --i) {
        i32 j = jrand_int_bound(r, i);
        i32 tmp = a[i - 1];
        a[i - 1] = a[j];
        a[j] = tmp;
    }
}

/* ItemStack.splitStack: copy (incl. StoredEnchantments) then shrink source. */
MC_HD static inline LtStack shl_stack_split(LtStack *src, i32 amount) {
    LtStack out;
    i32 take;
    if (!src || amount <= 0 || lt_stack_is_empty(*src)) return lt_stack_empty();
    take = amount;
    if (take > src->count) take = src->count;
    out = *src;
    out.count = take;
    src->count -= take;
    if (src->count <= 0) *src = lt_stack_empty();
    return out;
}

/* Random.nextBoolean == next(1) != 0. */
MC_HD static inline int shl_next_boolean(JavaRandom *r) {
    return jrand_next(r, 1) != 0;
}

/* LootTable.shuffleItems VERBATIM (1.11.2 bytecode):
 *  1) strip empties; park count>1 stacks in a side list
 *  2) free = empty_slots - remaining_singles (gate only; not decremented in loop)
 *  3) while free>0 && multi non-empty:
 *       pick multi via MathHelper.getInt(0, size-1)
 *       split amount via MathHelper.getInt(1, count/2)
 *       each half: if count>1 && nextBoolean -> requeue multi, else finalize
 *  4) append leftover multi, Collections.shuffle
 * Enchants ride splitStack.copy(). work[] is scratch (multi list); stacks mutated. */
MC_HD static inline i32 shl_shuffle_items(LtStack *stacks, i32 n, i32 empty_slots,
                                          JavaRandom *r, LtStack *work, i32 work_cap) {
    i32 n_kept = 0;
    i32 n_multi = 0;
    i32 i;
    if (work_cap > SHL_MAX_STACKS) work_cap = SHL_MAX_STACKS;
    for (i = 0; i < n; ++i) {
        if (lt_stack_is_empty(stacks[i])) continue;
        if (stacks[i].count > 1) {
            if (n_multi < work_cap) work[n_multi++] = stacks[i];
        } else {
            if (n_kept < work_cap) stacks[n_kept++] = stacks[i];
        }
    }
    empty_slots = empty_slots - n_kept;
    while (empty_slots > 0 && n_multi > 0) {
        i32 pick = lt_math_get_int(r, 0, n_multi - 1);
        LtStack item2 = work[pick];
        i32 split_amt, j;
        LtStack item1;
        /* ArrayList.remove(pick): shift left, preserve relative order of tail. */
        for (j = pick; j < n_multi - 1; ++j) work[j] = work[j + 1];
        --n_multi;
        split_amt = lt_math_get_int(r, 1, item2.count / 2);
        item1 = shl_stack_split(&item2, split_amt);
        if (item2.count > 1 && shl_next_boolean(r)) {
            if (n_multi < work_cap) work[n_multi++] = item2;
        } else {
            if (n_kept < work_cap) stacks[n_kept++] = item2;
        }
        if (item1.count > 1 && shl_next_boolean(r)) {
            if (n_multi < work_cap) work[n_multi++] = item1;
        } else {
            if (n_kept < work_cap) stacks[n_kept++] = item1;
        }
    }
    for (i = 0; i < n_multi && n_kept < work_cap; ++i)
        stacks[n_kept++] = work[i];
    /* Collections.shuffle(stacks[0..n_kept), rand) */
    for (i = n_kept; i > 1; --i) {
        i32 j = jrand_int_bound(r, i);
        LtStack tmp = stacks[i - 1];
        stacks[i - 1] = stacks[j];
        stacks[j] = tmp;
    }
    return n_kept;
}

/* LootTable.fillInventory into a 27-slot TeChest.
 * Vanilla order: getEmptySlotsRandomized (shuffle empties) THEN shuffleItems. */
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

    shl_shuffle_ints(empty, n_empty, &rng);
    n = shl_shuffle_items(stacks, n, n_empty, &rng, work, SHL_MAX_STACKS);

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

/* ---- Oracle battery: seed-scoped fillInventory materialization ----
 * Verifies LootTable.fillInventory over fixed pre-rolled stacks (including
 * multi-enchant books) with JavaRandom loot seeds. This is the unopened-chest
 * materialization path once structure code has captured a loot nextLong.
 *
 * World-layout seed parity (C sh_place_blocks vs Java StructureStrongholdPieces)
 * remains OPEN — this battery scopes only the loot_seed -> chest-TE mapping.
 *
 * Emit per (seed,mark): 27 slots * (item,count,meta,n_ench,e0id,e0lvl,e1id,e1lvl)
 * + nonempty count. Seeds {0, 42, 12345}. Two stack-sets (plain + books). */
enum {
    SHL_BAT_SEEDS     = 3,
    SHL_BAT_SETS      = 2,
    SHL_BAT_SLOT_F    = 8,
    SHL_BAT_PER       = (TEC_SLOTS * SHL_BAT_SLOT_F + 1),
    SHL_BAT_OUT       = (SHL_BAT_SEEDS * SHL_BAT_SETS * SHL_BAT_PER)
};

MC_HD static inline i64 shl_bat_seed(int i) {
    static const i64 s[3] = { 0, 42, 12345 };
    return (i >= 0 && i < 3) ? s[i] : 0;
}

/* Fixed pre-rolled stacks for set 0 (no books) and set 1 (StoredEnchantments). */
MC_HD static inline i32 shl_bat_stacks(int set, LtStack *out, i32 cap) {
    i32 n = 0;
    if (set == 0) {
        if (n < cap) out[n++] = lt_stack_mk(SHL_APPLE, 20, 0);
        if (n < cap) out[n++] = lt_stack_mk(SHL_BREAD, 5, 0);
        if (n < cap) out[n++] = lt_stack_mk(SHL_IRON_INGOT, 3, 0);
        if (n < cap) out[n++] = lt_stack_mk(SHL_COAL, 12, 0);
        if (n < cap) out[n++] = lt_stack_mk(SHL_DIAMOND, 1, 0);
    } else {
        LtStack multi, sharp;
        multi = lt_stack_mk(SHL_ENCHANTED_BOOK, 1, 0);
        multi.n_enchants = 2;
        multi.ench_id[0] = 16; multi.ench_lvl[0] = 3;
        multi.ench_id[1] = 34; multi.ench_lvl[1] = 1;
        sharp = lt_stack_mk(SHL_ENCHANTED_BOOK, 1, 0);
        sharp.n_enchants = 1;
        sharp.ench_id[0] = 16; sharp.ench_lvl[0] = 5;
        if (n < cap) out[n++] = multi;
        if (n < cap) out[n++] = sharp;
        if (n < cap) out[n++] = multi; /* second equal multi: stays separate (max 1) */
        if (n < cap) out[n++] = lt_stack_mk(SHL_BOOK, 2, 0);
        if (n < cap) out[n++] = lt_stack_mk(SHL_PAPER, 7, 0);
        if (n < cap) out[n++] = lt_stack_mk(SHL_COMPASS, 1, 0);
    }
    return n;
}

/* LootTable.fillInventory over a fixed stack list (no generateLootForPools).
 * Order matches vanilla: shuffle empty slots first, then shuffleItems. */
MC_HD static inline void shl_fill_from_stacks(TeChest *chest, LtStack *stacks, i32 n,
                                              i64 loot_seed) {
    JavaRandom rng;
    LtStack work[SHL_MAX_STACKS];
    i32 empty[TEC_SLOTS];
    i32 n_empty = 0, i;
    if (!chest) return;
    tec_init(chest);
    jrand_set(&rng, loot_seed);
    for (i = 0; i < TEC_SLOTS; ++i) empty[n_empty++] = i;
    shl_shuffle_ints(empty, n_empty, &rng);
    n = shl_shuffle_items(stacks, n, n_empty, &rng, work, SHL_MAX_STACKS);
    for (i = 0; i < n && n_empty > 0; ++i) {
        if (lt_stack_is_empty(stacks[i])) { --n_empty; continue; }
        {
            i32 slot = empty[--n_empty];
            TecStack ts = tec_mk(stacks[i].item, stacks[i].count, stacks[i].meta);
            int e, ne = stacks[i].n_enchants;
            if (ne > TEC_MAX_ENCHANTS) ne = TEC_MAX_ENCHANTS;
            ts.n_enchants = ne;
            for (e = 0; e < ne; ++e) {
                ts.enchants[e].id = stacks[i].ench_id[e];
                ts.enchants[e].level = stacks[i].ench_lvl[e];
            }
            tec_set_slot(chest, slot, ts);
        }
    }
}

MC_HD static inline void shl_emit_chest(const TeChest *c, u32 *out, int *o) {
    int i, nonempty = 0;
    for (i = 0; i < TEC_SLOTS; ++i) {
        const TecStack *s = &c->slots[i];
        out[(*o)++] = (u32)s->item;
        out[(*o)++] = (u32)s->count;
        out[(*o)++] = (u32)s->meta;
        out[(*o)++] = (u32)s->n_enchants;
        out[(*o)++] = (u32)(s->n_enchants > 0 ? (u16)s->enchants[0].id : 0);
        out[(*o)++] = (u32)(s->n_enchants > 0 ? (u16)s->enchants[0].level : 0);
        out[(*o)++] = (u32)(s->n_enchants > 1 ? (u16)s->enchants[1].id : 0);
        out[(*o)++] = (u32)(s->n_enchants > 1 ? (u16)s->enchants[1].level : 0);
        if (!tec_is_empty(s)) nonempty++;
    }
    out[(*o)++] = (u32)nonempty;
}

MC_HD static inline void shl_run_battery(u32 *out) {
    int si, set, o = 0;
    for (si = 0; si < SHL_BAT_SEEDS; ++si) {
        for (set = 0; set < SHL_BAT_SETS; ++set) {
            TeChest chest;
            LtStack stacks[SHL_MAX_STACKS];
            LtStack copy[SHL_MAX_STACKS];
            i32 n = shl_bat_stacks(set, stacks, SHL_MAX_STACKS);
            i32 i;
            /* shuffle mutates via work; keep source stable by copying */
            for (i = 0; i < n; ++i) copy[i] = stacks[i];
            shl_fill_from_stacks(&chest, copy, n, shl_bat_seed(si));
            shl_emit_chest(&chest, out, &o);
        }
    }
}

#endif /* MC_STRONGHOLD_LOOT_H */
