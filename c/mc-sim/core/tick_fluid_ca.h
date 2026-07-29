/* tick_fluid_ca: Wave 14 - fluid_flow CA on a chunk slice each tick (double-buffer env).
 *
 * INTERNAL verify (CPU==CUDA). Composes tick_world_copy (now->next copy + tick++) with one
 * ff_ca_step per tick on a 17x4x17 world slice at y=62..65. Per-tick dump: tick, block FNV
 * hash, cur buffer index (16 ticks). READ-ONLY deps: tick_world_copy.h, fluid_flow.h. */
#ifndef MC_TICK_FLUID_CA_H
#define MC_TICK_FLUID_CA_H

#include "tick_world_copy.h"
#include "fluid_flow.h"

#define TFC_SLICE_OX 0
#define TFC_SLICE_OY 62
#define TFC_SLICE_OZ 0
#define TFC_SLICE_NX FF_DIM_WB_X
#define TFC_SLICE_NY FF_DIM_WB_Y
#define TFC_SLICE_NZ FF_DIM_WB_Z
#define TFC_SLICE_VOL (TFC_SLICE_NX * TFC_SLICE_NY * TFC_SLICE_NZ)

MC_HD static inline void tfc_extract_slice(const World *w, u16 *buf) {
    const Chunk *c = &w->chunk[0];
    int x, y, z;
    for (y = 0; y < TFC_SLICE_NY; ++y)
        for (z = 0; z < TFC_SLICE_NZ; ++z)
            for (x = 0; x < TFC_SLICE_NX; ++x)
                ff_set(buf, TFC_SLICE_NX, TFC_SLICE_NY, TFC_SLICE_NZ, x, y, z,
                       mc_get(c, TFC_SLICE_OX + x, TFC_SLICE_OY + y, TFC_SLICE_OZ + z));
}

MC_HD static inline void tfc_merge_slice(World *w, const u16 *buf) {
    Chunk *c = &w->chunk[0];
    int x, y, z;
    for (y = 0; y < TFC_SLICE_NY; ++y)
        for (z = 0; z < TFC_SLICE_NZ; ++z)
            for (x = 0; x < TFC_SLICE_NX; ++x)
                mc_set(c, TFC_SLICE_OX + x, TFC_SLICE_OY + y, TFC_SLICE_OZ + z,
                       ff_get(buf, TFC_SLICE_NX, TFC_SLICE_NY, TFC_SLICE_NZ, x, y, z));
}

MC_HD static inline void tfc_init_fluids(World *w, u64 seed) {
    Chunk *c = &w->chunk[0];
    u16 stone = mc_state(FF_BLK_STONE, 0);
    u16 air = mc_state(FF_BLK_AIR, 0);
    int x, z;

    for (z = 0; z < TFC_SLICE_NZ; ++z)
        for (x = 0; x < TFC_SLICE_NX; ++x) {
            mc_set(c, TFC_SLICE_OX + x, TFC_SLICE_OY + 0, TFC_SLICE_OZ + z, air);
            mc_set(c, TFC_SLICE_OX + x, TFC_SLICE_OY + 1, TFC_SLICE_OZ + z, stone);
            mc_set(c, TFC_SLICE_OX + x, TFC_SLICE_OY + 2, TFC_SLICE_OZ + z, air);
            mc_set(c, TFC_SLICE_OX + x, TFC_SLICE_OY + 3, TFC_SLICE_OZ + z, air);
        }

    {
        u64 hv = mc_hash_seed(seed, 1, 0, 0, 0, 2);
        int sx = 4 + (int)mc_hash_bound(hv, TFC_SLICE_NX - 8);
        hv = mc_hash64(hv + 1ULL);
        int sz = 4 + (int)mc_hash_bound(hv, TFC_SLICE_NZ - 8);
        mc_set(c, sx, TFC_SLICE_OY + 1, sz, mc_state(FF_BLK_LAVA, 0));
        mc_set(c, sx, TFC_SLICE_OY + 2, sz, mc_state(FF_BLK_WATER, 0));
    }
}

MC_HD static inline void tfc_init_env(Env *e, u64 seed) {
    twc_init_env(e, seed);
    tfc_init_fluids(twc_now(e), seed);
    twc_copy_world(twc_next(e), twc_now(e));
}

/* Read fluid slice from now; write CA result into next (after baseline copy). */
MC_HD static inline void tfc_tick_env(Env *e, u16 *cur, u16 *tmp) {
    World *now = twc_now(e);
    World *next = twc_next(e);
    twc_copy_world(next, now);
    tfc_extract_slice(now, cur);
    ff_ca_step(cur, tmp, TFC_SLICE_NX, TFC_SLICE_NY, TFC_SLICE_NZ);
    tfc_merge_slice(next, tmp);
    next->tick = now->tick + 1;
    twc_swap(e);
}

typedef TwcEmitFn TfcEmitFn;

MC_HD static inline void tfc_run(Env *e, u64 seed, u16 *cur, u16 *tmp, TfcEmitFn emit, void *ctx) {
    int t;
    tfc_init_env(e, seed);
    for (t = 0; t < TWC_NTICKS; ++t) {
        World *now;
        u64 tick_bits, block_hash, cur_bits;
        tfc_tick_env(e, cur, tmp);
        now = twc_now(e);
        tick_bits = (u64)now->tick;
        block_hash = twc_blocks_hash(now);
        cur_bits = (u64)(u32)e->cur;
        if (emit) emit(tick_bits, block_hash, cur_bits, ctx);
    }
}

#endif /* MC_TICK_FLUID_CA_H */
