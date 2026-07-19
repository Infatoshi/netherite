/* cuda_batch_tick: Wave 14 - N envs x T ticks batched (CPU scalar loop == CUDA one-block-per-env).
 *
 * INTERNAL verify (CPU==CUDA). Each env uses a distinct seed; per-tick dump matches
 * tick_compose_full layout: tick, combined FNV hash, cur (grouped env-major, tick-minor).
 * READ-ONLY dep: tick_compose_full.h (verified, 64 ticks). */
#ifndef MC_CUDA_BATCH_TICK_H
#define MC_CUDA_BATCH_TICK_H

#include "tick_compose_full.h"

#define CBT_NENVS 4
#define CBT_NTICKS TCF_NTICKS

static const u64 CBT_SEEDS[CBT_NENVS] = {12345ULL, 0ULL, 7ULL, 42ULL};

typedef struct {
    u64 tick_bits;
    u64 combined_hash;
    u64 cur_bits;
} CbtEmitLine;

MC_HD static inline void cbt_record_tick(Env *e, TcfAux *aux, TcfScratch *scratch, int n_dec,
                                         CbtEmitLine *line) {
    World *now = twc_now(e);
    u64 blocks_h = twc_blocks_hash(now);
    u64 light_h = tlc_light_hash(now);
    u64 ent_h = te_entity_hash(&aux->te);
    u64 spawn_h = ts_spawn_hash(scratch->decisions, n_dec);

    line->tick_bits = (u64)now->tick;
    line->combined_hash = tcf_combine_hash(blocks_h, light_h, ent_h, spawn_h);
    line->cur_bits = (u64)(u32)e->cur;
}

MC_HD static inline void cbt_run_one(Env *e, TcfAux *aux, u64 seed,
                                     ChunkPrimer *primer, CpScratch *sc,
                                     const McSinTable *st, TcfScratch *scratch,
                                     PfWork *work, CbtEmitLine *lines) {
    int t;

    tcf_init_env(e, aux, seed, primer, sc, st, scratch);

    for (t = 0; t < CBT_NTICKS; ++t) {
        int n_dec = 0;

        tcf_tick_env(e, aux, primer, st, scratch, work, &n_dec);
        cbt_record_tick(e, aux, scratch, n_dec, &lines[t]);
    }
}

#endif /* MC_CUDA_BATCH_TICK_H */
