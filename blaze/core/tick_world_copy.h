/* tick_world_copy: Wave 14 phase 1 - mc_tick_env baseline (now->next copy + tick counter).
 *
 * INTERNAL verify (CPU==CUDA). Single-chunk world (16x256x16). Per-tick dump: tick number,
 * block-state hash, cur buffer index. READ-ONLY trunk: mc_tick.h, mc_world.h (do not edit).
 * Env/Entities layout copied from trunk headers (mc_entity.h forward-decl breaks nvcc C++). */
#ifndef MC_TICK_WORLD_COPY_H
#define MC_TICK_WORLD_COPY_H

#define MC_WORLD_R 0
#include "mc_world.h"
#include "mc_blocks.h"
#include "mc_rng.h"

#ifndef MC_MAX_ENTITIES
#define MC_MAX_ENTITIES 256
#endif

typedef struct {
    int    count;
    u8     type[MC_MAX_ENTITIES];
    u8     alive[MC_MAX_ENTITIES];
    double x[MC_MAX_ENTITIES], y[MC_MAX_ENTITIES], z[MC_MAX_ENTITIES];
    double vx[MC_MAX_ENTITIES], vy[MC_MAX_ENTITIES], vz[MC_MAX_ENTITIES];
    float  yaw[MC_MAX_ENTITIES], pitch[MC_MAX_ENTITIES];
    float  health[MC_MAX_ENTITIES];
    i32    age[MC_MAX_ENTITIES];
    u8     on_ground[MC_MAX_ENTITIES];
} Entities;

typedef struct {
    World    a, b;
    Entities ent;
    int      cur;
} Env;

MC_HD static inline World *twc_now(Env *e)  { return e->cur ? &e->b : &e->a; }
MC_HD static inline World *twc_next(Env *e) { return e->cur ? &e->a : &e->b; }
MC_HD static inline void   twc_swap(Env *e) { e->cur ^= 1; }

#define TWC_NTICKS 16

MC_HD static inline void twc_copy_chunk(Chunk *dst, const Chunk *src) {
    int i;
    for (i = 0; i < MC_CHUNK_VOL; ++i) {
        dst->blocks[i] = src->blocks[i];
        dst->light[i] = src->light[i];
    }
    for (i = 0; i < MC_COL_AREA; ++i)
        dst->biome[i] = src->biome[i];
    dst->cx = src->cx;
    dst->cz = src->cz;
}

MC_HD static inline void twc_copy_world(World *dst, const World *src) {
    int i;
    for (i = 0; i < MC_WORLD_CHUNKS; ++i)
        twc_copy_chunk(&dst->chunk[i], &src->chunk[i]);
    dst->seed = src->seed;
}

MC_HD static inline u64 twc_blocks_hash(const World *w) {
    u64 h = 0xcbf29ce484222325ULL;
    int ci, i;
    for (ci = 0; ci < MC_WORLD_CHUNKS; ++ci) {
        const Chunk *c = &w->chunk[ci];
        for (i = 0; i < MC_CHUNK_VOL; ++i) {
            h ^= (u64)c->blocks[i];
            h *= 0x100000001b3ULL;
        }
    }
    return h;
}

MC_HD static inline void twc_init_world(World *w, u64 seed) {
    Chunk *c = &w->chunk[0];
    u16 air = mc_state(BLK_AIR, 0);
    u16 stone = mc_state(BLK_STONE, 0);
    u16 dirt = mc_state(BLK_DIRT, 0);
    u16 grass = mc_state(BLK_GRASS, 0);
    int x, y, z, k;

    w->seed = seed;
    w->tick = 0;
    c->cx = 0;
    c->cz = 0;

    for (y = 0; y < MC_CY; ++y)
        for (z = 0; z < MC_CZ; ++z)
            for (x = 0; x < MC_CX; ++x) {
                u16 s = air;
                if (y < 62) s = stone;
                else if (y < 63) s = dirt;
                else if (y == 63) s = grass;
                mc_set(c, x, y, z, s);
                c->light[mc_idx(x, y, z)] = mc_light(15, 0);
            }

    for (z = 0; z < MC_CZ; ++z)
        for (x = 0; x < MC_CX; ++x)
            c->biome[z * MC_CX + x] = 1;

    for (k = 0; k < 32; ++k) {
        u64 hv = mc_hash_seed(seed, 0, k, 0, 0, 1);
        x = mc_hash_bound(hv, MC_CX);
        hv = mc_hash64(hv + 1ULL);
        y = 60 + mc_hash_bound(hv, 4);
        hv = mc_hash64(hv + 2ULL);
        z = mc_hash_bound(hv, MC_CZ);
        mc_set(c, x, y, z, air);
    }
}

MC_HD static inline void twc_init_env(Env *e, u64 seed) {
    e->cur = 0;
    e->ent.count = 0;
    twc_init_world(&e->a, seed);
    twc_copy_world(&e->b, &e->a);
    e->b.tick = 0;
}

/* Phase-1 mc_tick_env: copy now->next, bump tick, swap. No block mutations yet. */
MC_HD static inline void twc_tick_env(Env *e) {
    World *now = twc_now(e);
    World *next = twc_next(e);
    twc_copy_world(next, now);
    next->tick = now->tick + 1;
    twc_swap(e);
}

typedef void (*TwcEmitFn)(u64 tick_bits, u64 block_hash, u64 cur_bits, void *ctx);

MC_HD static inline void twc_run(Env *e, u64 seed, TwcEmitFn emit, void *ctx) {
    int t;
    twc_init_env(e, seed);
    for (t = 0; t < TWC_NTICKS; ++t) {
        World *now;
        u64 tick_bits, block_hash, cur_bits;
        twc_tick_env(e);
        now = twc_now(e);
        tick_bits = (u64)now->tick;
        block_hash = twc_blocks_hash(now);
        cur_bits = (u64)(u32)e->cur;
        if (emit) emit(tick_bits, block_hash, cur_bits, ctx);
    }
}

#endif /* MC_TICK_WORLD_COPY_H */
