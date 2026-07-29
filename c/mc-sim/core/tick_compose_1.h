/* tick_compose_1: Wave 14 - chain tick_world_copy + tick_random_block + tick_fluid_ca.
 *
 * INTERNAL verify (CPU==CUDA). Same 16x256x16 Env: each tick copies now->next, runs block
 * tickers (slice scan + random attempts), then one ff_ca_step on fluid slice (read post-ticker
 * next, merge into next). Per-tick dump: tick, block FNV hash, cur (16 ticks).
 * READ-ONLY deps: tick_world_copy.h, tick_random_block.h, tick_fluid_ca.h. */
#ifndef MC_TICK_COMPOSE_1_H
#define MC_TICK_COMPOSE_1_H

#include "tick_world_copy.h"
#include "tick_random_block.h"
#include "tick_fluid_ca.h"

MC_HD static inline void tc1_init_env(Env *e, TrbAux *aux, u64 seed) {
    twc_init_env(e, seed);
    trb_init_fixtures(&twc_now(e)->chunk[0], aux, seed);
    tfc_init_fluids(twc_now(e), seed);
    twc_copy_world(twc_next(e), twc_now(e));
}

/* Per tick: copy, block tickers on next, fluid CA on next (post-ticker state). */
MC_HD static inline void tc1_tick_env(Env *e, TrbAux *aux, u16 *cur, u16 *tmp) {
    World *now = twc_now(e);
    World *next = twc_next(e);
    twc_copy_world(next, now);
    trb_tick_slice(&now->chunk[0], &next->chunk[0], aux, now->seed, now->tick);
    trb_random_attempts(&now->chunk[0], &next->chunk[0], now->seed, now->tick);
    tfc_extract_slice(next, cur);
    ff_ca_step(cur, tmp, TFC_SLICE_NX, TFC_SLICE_NY, TFC_SLICE_NZ);
    tfc_merge_slice(next, tmp);
    next->tick = now->tick + 1;
    twc_swap(e);
}

typedef TwcEmitFn Tc1EmitFn;

MC_HD static inline void tc1_run(Env *e, TrbAux *aux, u64 seed, u16 *cur, u16 *tmp,
                                 Tc1EmitFn emit, void *ctx) {
    int t;
    tc1_init_env(e, aux, seed);
    for (t = 0; t < TWC_NTICKS; ++t) {
        World *now;
        u64 tick_bits, block_hash, cur_bits;
        tc1_tick_env(e, aux, cur, tmp);
        now = twc_now(e);
        tick_bits = (u64)now->tick;
        block_hash = twc_blocks_hash(now);
        cur_bits = (u64)(u32)e->cur;
        if (emit) emit(tick_bits, block_hash, cur_bits, ctx);
    }
}

#endif /* MC_TICK_COMPOSE_1_H */
