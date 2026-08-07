/* CPU reference + unit test: vanilla scheduled-tick queue (scheduled_ticks.h) and the
 * canonical WorldServer.tick order driver (world_tick_vanilla.h).
 *
 * Host asserts exercise: compareTo ordering (time, then priority, then insertion id),
 * (pos, block) dedup incl. the drained-batch self-reschedule window, the 65536 per-tick
 * drain cap, the updateLCG sequence vs hand-computed int32 values, and an end-to-end
 * fire-before-water dispatch order (fire t+3, water t+5) through wt_vanilla_tick.
 * Emits the same hex lines as the CUDA driver for a CPU==CUDA diff. */
#define MC_WORLD_R 0            /* single-chunk world for the end-to-end scene */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../core/world_tick_vanilla.h"

#define WTV_TICKS 6

static u64 g_lines[256];
static int g_nlines;
static void emit(u64 v) { g_lines[g_nlines++] = v; }

/* hand-computed: lcg=123456789; lcg = lcg*3 + 1013904223 (int32 wrap); j = lcg >> 2. */
static const i32 k_lcg_seq[6] = {
    1384274590, 871760697, -665780982, -983438723, -1936411946, -500364319
};
static const int k_lcg_xzy[3][3] = { {7, 6, 0}, {14, 0, 13}, {2, 15, 4} };

static void test_lcg(void) {
    i32 lcg = 123456789;
    int i;
    for (i = 0; i < 6; ++i) {
        i32 j = wt_lcg_advance(&lcg);
        assert(lcg == k_lcg_seq[i]);
        if (i < 3) {
            assert((j & 15) == k_lcg_xzy[i][0]);
            assert(((j >> 8) & 15) == k_lcg_xzy[i][1]);
            assert(((j >> 16) & 15) == k_lcg_xzy[i][2]);
        }
        emit((u64)(u32)lcg);
        emit(((u64)(j & 15) << 16) | ((u64)((j >> 8) & 15) << 8) | (u64)((j >> 16) & 15));
    }
    /* World seeds updateLCG from rand.nextInt(); JDK golden for seed 0. */
    {
        JavaRandom r;
        jrand_set(&r, 0);
        assert(jrand_int(&r) == -1155484576);
    }
}

static void test_fire_tables(void) {
    assert(wt_fire_flammability(47) == 20);
    assert(wt_fire_encouragement(47) == 30);
    assert(wt_fire_flammability(173) == 5);
    assert(wt_fire_encouragement(173) == 5);
    assert(wt_fire_flammability(170) == 20);
    assert(wt_fire_encouragement(170) == 60);
    assert(wt_fire_flammability(171) == 20);
    assert(wt_fire_encouragement(171) == 60);
    emit(((u64)wt_fire_flammability(47) << 32)
        | (u64)wt_fire_encouragement(47));
    emit(((u64)wt_fire_flammability(173) << 32)
        | (u64)wt_fire_encouragement(173));
    emit(((u64)wt_fire_flammability(170) << 48)
        | ((u64)wt_fire_encouragement(170) << 32)
        | ((u64)wt_fire_flammability(171) << 16)
        | (u64)wt_fire_encouragement(171));
}

static void test_order_dedup_cap(McScheduledTicks *q) {
    /* ordering: time first, then priority, then insertion id */
    stq_init(q);
    stq_update_block_tick(q, 1, 0, 0, 1, 10, 0, 0, 0, 1);   /* A: t10 */
    stq_update_block_tick(q, 2, 0, 0, 1, 5, 0, 0, 0, 1);    /* B: t5 p0  */
    stq_update_block_tick(q, 3, 0, 0, 1, 5, -1, 0, 0, 1);   /* C: t5 p-1 id2 */
    stq_update_block_tick(q, 4, 0, 0, 1, 5, -1, 0, 0, 1);   /* D: t5 p-1 id3 */
    {
        i32 n = stq_begin_tick(q, 0, 1 /* tick_all */);
        assert(n == 4);
        assert(q->this_tick[0].x == 3);   /* C: earliest time, lowest priority, lower id */
        assert(q->this_tick[1].x == 4);   /* D: id tiebreak */
        assert(q->this_tick[2].x == 2);   /* B: priority 0 */
        assert(q->this_tick[3].x == 1);   /* A: latest time */
        emit((u64)q->this_tick[0].x << 24 | (u64)q->this_tick[1].x << 16 |
             (u64)q->this_tick[2].x << 8 | (u64)q->this_tick[3].x);
        /* self-reschedule during the batch: C left the hash set when drained */
        stq_update_block_tick(q, 3, 0, 0, 1, 7, 0, 0, 0, 1);
        assert(q->heap_n == 1);
        assert(stq_is_block_tick_pending(q, 3, 0, 0, 1));    /* still in this_tick */
        stq_end_tick(q);
    }

    /* dedup: same (pos, block) rejected; same pos different block kept */
    stq_init(q);
    stq_update_block_tick(q, 9, 9, 9, 1, 4, 0, 0, 0, 1);
    stq_update_block_tick(q, 9, 9, 9, 1, 9, 5, 0, 0, 1);    /* dup key: dropped */
    assert(q->heap_n == 1);
    assert(stq_is_update_scheduled(q, 9, 9, 9, 1));
    stq_update_block_tick(q, 9, 9, 9, 2, 4, 0, 0, 0, 1);    /* other block: kept */
    assert(q->heap_n == 2);
    /* unloaded pos: no insert (isBlockLoaded gate) */
    stq_update_block_tick(q, 9, 9, 8, 1, 4, 0, 0, 0, 0);
    assert(q->heap_n == 2);
    emit((u64)q->heap_n);

    /* 65536 per-tick drain cap */
    stq_init(q);
    {
        i32 i, n;
        for (i = 0; i < 70000; ++i)
            stq_update_block_tick(q, i, 0, 0, 1, 0, 0, 0, 0, 1);
        assert(q->heap_n == 70000 && q->overflow == 0);
        n = stq_begin_tick(q, 0, 0);
        assert(n == STQ_MAX_PER_TICK);
        assert(q->heap_n == 70000 - STQ_MAX_PER_TICK);
        stq_end_tick(q);
        emit((u64)n);
        n = stq_begin_tick(q, 0, 0);
        assert(n == 70000 - STQ_MAX_PER_TICK);
        stq_end_tick(q);
        emit((u64)n);
    }
}

static u64 wtv_blocks_hash(const World *w) {
    u64 h = 0xcbf29ce484222325ULL;
    int i;
    for (i = 0; i < MC_CHUNK_VOL; ++i) {
        h ^= (u64)w->chunk[0].blocks[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static void wtv_scene_init(World *w, u64 seed) {
    int x, y, z;
    Chunk *c = &w->chunk[0];
    w->seed = seed;
    w->tick = 0;
    c->cx = 0; c->cz = 0;
    for (y = 0; y < MC_CY; ++y)
        for (z = 0; z < MC_CZ; ++z)
            for (x = 0; x < MC_CX; ++x) {
                mc_set(c, x, y, z, mc_state(y < 5 ? BLK_STONE : BLK_AIR, 0));
                c->light[mc_idx(x, y, z)] = mc_light(15, 0);
            }
    for (z = 0; z < MC_CZ; ++z)
        for (x = 0; x < MC_CX; ++x)
            c->biome[z * MC_CX + x] = 1;                     /* plains: no freezing */
    /* fire on planks */
    mc_set(c, 5, 9, 5, mc_state(BLK_PLANKS, 0));
    mc_set(c, 5, 10, 5, mc_state(WT_BLK_FIRE, 0));
    /* dynamic source-level water hanging in the air (falls when its tick fires) */
    mc_set(c, 10, 20, 10, mc_state(BLK_FLOWING_WATER, 0));
}

static void test_end_to_end(World *w, WtvState *s) {
    McGameRules gr = mc_gamerules_default();
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    int t, fire_i = -1, water_i = -1, i;

    mc_sin_table_init(st);
    wtv_scene_init(w, 12345ULL);
    wt_vanilla_init(s, 12345ULL);

    /* schedule water at t+5, fire at t+3 (both while totalWorldTime == 0) */
    stq_update_block_tick(&s->stq, 10, 20, 10, BLK_FLOWING_WATER, 5, 0, s->totalWorldTime, 0, 1);
    stq_update_block_tick(&s->stq, 5, 10, 5, WT_BLK_FIRE, 3, 0, s->totalWorldTime, 0, 1);

    for (t = 0; t < WTV_TICKS; ++t) {
        wt_vanilla_tick(w, s, &gr, st, (MswScene *)0);
        wt_vanilla_update_entities(w, s);
        emit((u64)s->totalWorldTime);
        emit(wtv_blocks_hash(w));
        emit((u64)(u32)s->stq.heap_n);
    }
    for (i = 0; i < s->fired_n; ++i) {
        if (fire_i < 0 && s->fired[i].block == WT_BLK_FIRE) fire_i = i;
        if (water_i < 0 && s->fired[i].block == BLK_FLOWING_WATER) water_i = i;
        emit(((u64)(u32)s->fired[i].block << 32) | (u64)(u32)s->fired_at[i]);
    }
    /* fire (t+3) dispatched before water (t+5), at the scheduled world times */
    assert(fire_i >= 0 && water_i >= 0 && fire_i < water_i);
    assert(s->fired_at[fire_i] == 3 && s->fired_at[water_i] == 5);
    /* the water actually flowed: the cell below turned into falling flowing water */
    assert(mc_state_id(wt_get(w, 10, 19, 10)) == BLK_FLOWING_WATER);
    assert(s->stq.overflow == 0);
    free(st);
}

int main(void) {
    World *w = (World *)malloc(sizeof(World));
    WtvState *s = (WtvState *)malloc(sizeof(WtvState));
    int i;

    g_nlines = 0;
    test_lcg();
    test_fire_tables();
    test_order_dedup_cap(&s->stq);
    test_end_to_end(w, s);

    for (i = 0; i < g_nlines; ++i)
        printf("%016llx\n", (unsigned long long)g_lines[i]);
    fprintf(stderr, "world_tick_vanilla: all unit asserts passed (%d lines)\n", g_nlines);

    free(s); free(w);
    return 0;
}
