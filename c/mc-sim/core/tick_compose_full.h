/* tick_compose_full: Wave 14 - full tick loop (64 ticks).
 *
 * INTERNAL verify (CPU==CUDA). Chains per tick on 16x256x16 env:
 *   copy -> block tickers -> fluid CA -> light fixpoint -> entities -> spawn cycle.
 * Per-tick dump: tick, combined FNV hash (blocks+light+entities+spawn), cur.
 * READ-ONLY deps: tick_compose_1.h, tick_light_ca.h, tick_entities.h, tick_spawn.h. */
#ifndef MC_TICK_COMPOSE_FULL_H
#define MC_TICK_COMPOSE_FULL_H

#include "tick_compose_1.h"
#include "tick_light_ca.h"
#include "tick_entities.h"
#include "tick_spawn.h"

#define TCF_NTICKS 64

typedef struct {
    TrbAux     trb;
    TsAux      ts;
    TeAux      te;
    TeScratch  te_scratch;
} TcfAux;

typedef struct {
    u16 fluid_cur[TFC_SLICE_VOL];
    u16 fluid_tmp[TFC_SLICE_VOL];
    u16 lp_blocks[LP_VOL];
    u8  sky[LP_VOL];
    u8  blk[LP_VOL];
    u8  tmp_sky[LP_VOL];
    u8  tmp_blk[LP_VOL];
    u16 spawn_blocks[TS_VOL];
    u64 decisions[TS_MAX_DECISIONS];
} TcfScratch;

MC_HD static inline u64 tcf_combine_hash(u64 blocks, u64 light, u64 entities, u64 spawn) {
    u64 h = 0xcbf29ce484222325ULL;
    h ^= blocks;
    h *= 0x100000001b3ULL;
    h ^= light;
    h *= 0x100000001b3ULL;
    h ^= entities;
    h *= 0x100000001b3ULL;
    h ^= spawn;
    h *= 0x100000001b3ULL;
    return h;
}

MC_HD static inline void tcf_sync_spawn_light(const World *w, TsAux *aux) {
    const Chunk *c = &w->chunk[0];
    int x, y, z;
    for (y = 0; y < TS_NY; ++y)
        for (z = 0; z < TS_NZ; ++z)
            for (x = 0; x < TS_NX; ++x) {
                int wi = mc_idx(x, TS_OY + y, z);
                int si = ts_idx(x, y, z);
                u8 packed = c->light[wi];
                aux->sky[si] = (u8)mc_light_sky(packed);
                aux->blk[si] = (u8)mc_light_block(packed);
            }
}

MC_HD static inline void tcf_init_entities(TcfAux *aux, u64 seed) {
    ppf_init_entity(&aux->te.player);
    aux->te.player.posX = PPF_SPAWN_X;
    aux->te.player.posY = PPF_SPAWN_Y;
    aux->te.player.posZ = PPF_SPAWN_Z;
    aux->te.player.box = mc_pcm_player_box(aux->te.player.posX, aux->te.player.posY,
                                           aux->te.player.posZ);

    maz_init(&aux->te.zombie);
    pf_scene_flat(aux->te_scratch.pf_grid);

    aux->te.arrow_active = te_arrow_active(seed);
    pm_scene_flat(aux->te_scratch.pm_grid);
    if (aux->te.arrow_active)
        te_init_arrow(&aux->te.arrow, seed);
}

MC_HD static inline void tcf_init_env(Env *e, TcfAux *aux, u64 seed,
                                      ChunkPrimer *primer, CpScratch *sc,
                                      const McSinTable *st, TcfScratch *scratch) {
    twc_init_env(e, seed);
    cp_provide_chunk(primer, sc, st, seed, 0, 0);
    te_load_primer_to_chunk(&twc_now(e)->chunk[0], primer);
    trb_init_fixtures(&twc_now(e)->chunk[0], &aux->trb, seed);
    tfc_init_fluids(twc_now(e), seed);
    tlc_init_light_scene(twc_now(e), seed);
    ts_init_fixtures(&twc_now(e)->chunk[0], seed);

    tlc_extract_slice(twc_now(e), scratch->sky, scratch->blk, scratch->lp_blocks);
    lp_propagate(scratch->sky, scratch->blk, scratch->tmp_sky, scratch->tmp_blk,
                 scratch->lp_blocks, TLC_LIGHT_ITERS);
    tlc_merge_light(twc_now(e), scratch->sky, scratch->blk);

    tcf_init_entities(aux, seed);
    tcf_sync_spawn_light(twc_now(e), &aux->ts);
    twc_copy_world(twc_next(e), twc_now(e));
}

MC_HD static inline void tcf_tick_env(Env *e, TcfAux *aux, ChunkPrimer *primer,
                                      const McSinTable *st, TcfScratch *scratch,
                                      PfWork *work, int *n_dec) {
    World *now = twc_now(e);
    World *next = twc_next(e);
    i64 tick = now->tick;
    twc_copy_world(next, now);

    trb_tick_slice(&now->chunk[0], &next->chunk[0], &aux->trb, now->seed, tick);
    trb_random_attempts(&now->chunk[0], &next->chunk[0], now->seed, tick);

    tfc_extract_slice(next, scratch->fluid_cur);
    ff_ca_step(scratch->fluid_cur, scratch->fluid_tmp,
               TFC_SLICE_NX, TFC_SLICE_NY, TFC_SLICE_NZ);
    tfc_merge_slice(next, scratch->fluid_tmp);

    tlc_extract_slice(next, scratch->sky, scratch->blk, scratch->lp_blocks);
    lp_propagate(scratch->sky, scratch->blk, scratch->tmp_sky, scratch->tmp_blk,
                 scratch->lp_blocks, TLC_LIGHT_ITERS);
    tlc_merge_light(next, scratch->sky, scratch->blk);

    {
        PpfAction act = ppf_action_for_tick((i64)now->seed, (int)tick);
        ppf_player_tick(primer, st, &aux->te.player, &act, (int)tick, aux->te_scratch.blocks);
    }
    maz_tick_one((i64)now->seed, (int)tick, &aux->te.zombie, aux->te_scratch.pf_grid, work,
                 aux->te.player.posX, aux->te.player.posY, aux->te.player.posZ);
    if (aux->te.arrow_active && !aux->te.arrow.inGround)
        pm_arrow_tick(&aux->te.arrow, aux->te_scratch.pm_grid);

    ts_extract_blocks(&next->chunk[0], scratch->spawn_blocks);
    tcf_sync_spawn_light(next, &aux->ts);
    ts_hostile_spawn_cycle(now->seed, tick, scratch->spawn_blocks, &aux->ts,
                           scratch->decisions, n_dec);

    next->tick = tick + 1;
    twc_swap(e);
}

typedef void (*TcfEmitFn)(u64 tick_bits, u64 combined_hash, u64 cur_bits, void *ctx);

MC_HD static inline void tcf_run(Env *e, TcfAux *aux, u64 seed,
                                 ChunkPrimer *primer, CpScratch *sc,
                                 const McSinTable *st, PfWork *work,
                                 TcfScratch *scratch, TcfEmitFn emit, void *ctx) {
    int t;

    tcf_init_env(e, aux, seed, primer, sc, st, scratch);

    for (t = 0; t < TCF_NTICKS; ++t) {
        World *now;
        u64 tick_bits, combined, cur_bits;
        u64 blocks_h, light_h, ent_h, spawn_h;
        int n_dec = 0;

        tcf_tick_env(e, aux, primer, st, scratch, work, &n_dec);
        now = twc_now(e);
        tick_bits = (u64)now->tick;

        blocks_h = twc_blocks_hash(now);
        light_h = tlc_light_hash(now);
        ent_h = te_entity_hash(&aux->te);
        spawn_h = ts_spawn_hash(scratch->decisions, n_dec);
        combined = tcf_combine_hash(blocks_h, light_h, ent_h, spawn_h);

        cur_bits = (u64)(u32)e->cur;
        if (emit) emit(tick_bits, combined, cur_bits, ctx);
    }
}

#endif /* MC_TICK_COMPOSE_FULL_H */
