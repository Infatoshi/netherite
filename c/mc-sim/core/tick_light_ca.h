/* tick_light_ca: Wave 14 - light_propagation fixpoint CA on a chunk slice each tick.
 *
 * INTERNAL verify (CPU==CUDA). Composes tick_world_copy (now->next copy + tick++) with
 * lp_propagate fixpoint on a 16x64x16 world slice at y=0..63. Per-tick dump: tick, light FNV
 * hash, cur buffer index (16 ticks). READ-ONLY deps: tick_world_copy.h, light_propagation.h. */
#ifndef MC_TICK_LIGHT_CA_H
#define MC_TICK_LIGHT_CA_H

#include "tick_world_copy.h"
#include "light_propagation.h"

#define TLC_SLICE_OX 0
#define TLC_SLICE_OY 0
#define TLC_SLICE_OZ 0
#define TLC_SLICE_NX LP_NX
#define TLC_SLICE_NY LP_NY
#define TLC_SLICE_NZ LP_NZ
#define TLC_SLICE_VOL LP_VOL
#define TLC_LIGHT_ITERS 128

MC_HD static inline void tlc_extract_slice(const World *w, u8 *sky, u8 *blk, u16 *blocks) {
    const Chunk *c = &w->chunk[0];
    int x, y, z;
    for (y = 0; y < TLC_SLICE_NY; ++y)
        for (z = 0; z < TLC_SLICE_NZ; ++z)
            for (x = 0; x < TLC_SLICE_NX; ++x) {
                int wi = mc_idx(TLC_SLICE_OX + x, TLC_SLICE_OY + y, TLC_SLICE_OZ + z);
                int li = lp_idx(x, y, z);
                u8 packed = c->light[wi];
                sky[li] = (u8)mc_light_sky(packed);
                blk[li] = (u8)mc_light_block(packed);
                blocks[li] = mc_get(c, TLC_SLICE_OX + x, TLC_SLICE_OY + y, TLC_SLICE_OZ + z);
            }
}

MC_HD static inline void tlc_merge_light(World *w, const u8 *sky, const u8 *blk) {
    Chunk *c = &w->chunk[0];
    int x, y, z;
    for (y = 0; y < TLC_SLICE_NY; ++y)
        for (z = 0; z < TLC_SLICE_NZ; ++z)
            for (x = 0; x < TLC_SLICE_NX; ++x) {
                int wi = mc_idx(TLC_SLICE_OX + x, TLC_SLICE_OY + y, TLC_SLICE_OZ + z);
                int li = lp_idx(x, y, z);
                c->light[wi] = mc_light(sky[li], blk[li]);
            }
}

MC_HD static inline void tlc_merge_blocks(World *w, const u16 *blocks) {
    Chunk *c = &w->chunk[0];
    int x, y, z;
    for (y = 0; y < TLC_SLICE_NY; ++y)
        for (z = 0; z < TLC_SLICE_NZ; ++z)
            for (x = 0; x < TLC_SLICE_NX; ++x) {
                int li = lp_idx(x, y, z);
                mc_set(c, TLC_SLICE_OX + x, TLC_SLICE_OY + y, TLC_SLICE_OZ + z, blocks[li]);
            }
}

MC_HD static inline void tlc_zero_slice_light(World *w) {
    Chunk *c = &w->chunk[0];
    int x, y, z;
    for (y = 0; y < TLC_SLICE_NY; ++y)
        for (z = 0; z < TLC_SLICE_NZ; ++z)
            for (x = 0; x < TLC_SLICE_NX; ++x)
                c->light[mc_idx(TLC_SLICE_OX + x, TLC_SLICE_OY + y, TLC_SLICE_OZ + z)] = 0;
}

MC_HD static inline void tlc_init_light_scene(World *w, u64 seed) {
    u16 blocks[LP_VOL];
    lp_init_scene(blocks, (i64)seed);
    tlc_merge_blocks(w, blocks);
    tlc_zero_slice_light(w);
}

MC_HD static inline void tlc_init_env(Env *e, u64 seed) {
    twc_init_env(e, seed);
    tlc_init_light_scene(twc_now(e), seed);
    twc_copy_world(twc_next(e), twc_now(e));
}

MC_HD static inline u64 tlc_light_hash(const World *w) {
    u64 h = 0xcbf29ce484222325ULL;
    const Chunk *c = &w->chunk[0];
    int x, y, z;
    for (y = 0; y < TLC_SLICE_NY; ++y)
        for (z = 0; z < TLC_SLICE_NZ; ++z)
            for (x = 0; x < TLC_SLICE_NX; ++x) {
                int wi = mc_idx(TLC_SLICE_OX + x, TLC_SLICE_OY + y, TLC_SLICE_OZ + z);
                h ^= (u64)c->light[wi];
                h *= 0x100000001b3ULL;
            }
    return h;
}

/* Read light+blocks from now; write propagated light into next (after baseline copy). */
MC_HD static inline void tlc_tick_env(Env *e, u16 *blocks, u8 *sky, u8 *blk,
                                      u8 *tmp_sky, u8 *tmp_blk) {
    World *now = twc_now(e);
    World *next = twc_next(e);
    twc_copy_world(next, now);
    tlc_extract_slice(now, sky, blk, blocks);
    lp_propagate(sky, blk, tmp_sky, tmp_blk, blocks, TLC_LIGHT_ITERS);
    tlc_merge_light(next, sky, blk);
    next->tick = now->tick + 1;
    twc_swap(e);
}

typedef TwcEmitFn TlcEmitFn;

MC_HD static inline void tlc_run(Env *e, u64 seed, u16 *blocks, u8 *sky, u8 *blk,
                                 u8 *tmp_sky, u8 *tmp_blk, TlcEmitFn emit, void *ctx) {
    int t;
    tlc_init_env(e, seed);
    for (t = 0; t < TWC_NTICKS; ++t) {
        World *now;
        u64 tick_bits, light_hash, cur_bits;
        tlc_tick_env(e, blocks, sky, blk, tmp_sky, tmp_blk);
        now = twc_now(e);
        tick_bits = (u64)now->tick;
        light_hash = tlc_light_hash(now);
        cur_bits = (u64)(u32)e->cur;
        if (emit) emit(tick_bits, light_hash, cur_bits, ctx);
    }
}

#endif /* MC_TICK_LIGHT_CA_H */
