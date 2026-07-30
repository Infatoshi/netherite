/* py_gym_env_smoke: Wave 14 - pybind11 gym.Env smoke over tick_compose_full.
 *
 * reset(seed)->obs, step(action)->obs/reward/done/info. Obs schema subset aligned
 * with qrl mod (x/y/z, yaw/pitch, vx/vy/vz, on_ground, look block). Fixed replay
 * sequence for determinism oracle. READ-ONLY dep: tick_compose_full.h. */
#ifndef MC_PY_GYM_ENV_SMOKE_H
#define MC_PY_GYM_ENV_SMOKE_H

#include "tick_compose_full.h"

#define PGES_N_TICKS       16
#define PGES_YAW_QUANTUM   15.0f
#define PGES_SCHEMA_VER    1u
#define PGES_PURPOSE_OBS   0x5047455301u

typedef struct {
    int forward;
    int back;
    int left;
    int right;
    int jump;
    int sneak;
    int yaw;    /* -1, 0, 1 -> +/- 15 deg */
    int pitch;  /* -1, 0, 1 -> +/- 15 deg */
} PgesAction;

typedef struct {
    double x, y, z;
    double vx, vy, vz;
    float  yaw, pitch;
    int    on_ground;
    int    look_type; /* 0 miss, 1 block */
    int    look_bx, look_by, look_bz;
    u64    tick;
    u64    combined_hash;
} PgesObs;

typedef struct {
    Env         e;
    TcfAux      aux;
    TcfScratch  scratch;
    ChunkPrimer primer;
    CpScratch   sc;
    PfWork      work;
    McSinTable  st;
    float       yaw;
    float       pitch;
    u64         seed;
    int         steps;
    int         done;
    int         last_n_dec;
    float       last_reward;
} PgesEnv;

static const PgesAction PGES_REPLAY[PGES_N_TICKS] = {
    {1, 0, 0, 0, 0, 0,  0,  0},
    {1, 0, 0, 0, 1, 0,  0,  0},
    {0, 0, 1, 0, 0, 0,  1,  0},
    {0, 0, 0, 1, 0, 0,  0,  0},
    {0, 1, 0, 0, 0, 0,  0,  0},
    {0, 0, 0, 0, 0, 1,  0, -1},
    {1, 0, 0, 0, 0, 0, -1,  0},
    {0, 0, 0, 0, 1, 0,  0,  0},
    {1, 0, 1, 0, 0, 0,  0,  1},
    {0, 0, 0, 0, 0, 0,  0,  0},
    {0, 0, 0, 1, 0, 0,  1,  0},
    {1, 0, 0, 0, 0, 1,  0,  0},
    {0, 1, 0, 0, 0, 0, -1,  0},
    {0, 0, 1, 0, 1, 0,  0,  0},
    {1, 0, 0, 0, 0, 0,  0,  0},
    {0, 0, 0, 0, 0, 0,  0,  0},
};

MC_HD static inline void pges_hash_u64(u64 *h, u64 v) {
    *h ^= v;
    *h *= 0x100000001b3ULL;
}

MC_HD static inline void pges_hash_double(u64 *h, double d) {
    union { double d; u64 u; } u;
    u.d = d;
    pges_hash_u64(h, u.u);
}

MC_HD static inline void pges_hash_i32(u64 *h, i32 v) {
    pges_hash_u64(h, (u64)(u32)v);
}

MC_HD static inline float pges_quantize_yaw(float y) {
    float q = PGES_YAW_QUANTUM;
    return (float)((int)(y / q + (y >= 0.0f ? 0.5f : -0.5f))) * q;
}

MC_HD static inline float pges_quantize_pitch(float p) {
    float q = PGES_YAW_QUANTUM;
    float v = (float)((int)(p / q + (p >= 0.0f ? 0.5f : -0.5f))) * q;
    if (v < -90.0f) return -90.0f;
    if (v > 90.0f) return 90.0f;
    return v;
}

MC_HD static inline void pges_apply_aim(PgesEnv *g, const PgesAction *a) {
    if (a->yaw != 0)
        g->yaw = pges_quantize_yaw(g->yaw + (float)a->yaw * PGES_YAW_QUANTUM);
    if (a->pitch != 0)
        g->pitch = pges_quantize_pitch(g->pitch + (float)a->pitch * PGES_YAW_QUANTUM);
}

MC_HD static inline PpfAction pges_to_ppf(const PgesEnv *g, const PgesAction *a) {
    PpfAction act;
    act.forward = a->forward ? 1.0f : (a->back ? -1.0f : 0.0f);
    act.strafe  = a->right ? 1.0f : (a->left ? -1.0f : 0.0f);
    act.yaw     = g->yaw;
    act.pitch   = g->pitch;
    act.jump    = a->jump ? 1 : 0;
    act.sneak   = a->sneak ? 1 : 0;
    return act;
}

MC_HD static inline u64 pges_world_combined_hash(PgesEnv *g, int n_dec,
                                                 const u64 *decisions) {
    World *now = twc_now(&g->e);
    u64 blocks_h = twc_blocks_hash(now);
    u64 light_h = tlc_light_hash(now);
    u64 ent_h = te_entity_hash(&g->aux.te);
    u64 spawn_h = ts_spawn_hash(decisions, n_dec);
    return tcf_combine_hash(blocks_h, light_h, ent_h, spawn_h);
}

MC_HD static inline void pges_fill_obs(PgesEnv *g, PgesObs *obs, u64 combined) {
    const McPcfEntity *p = &g->aux.te.player;
    World *now = twc_now(&g->e);
    int bx, by, bz;
    u16 st;

    obs->x = p->posX;
    obs->y = p->posY;
    obs->z = p->posZ;
    obs->vx = p->motionX;
    obs->vy = p->motionY;
    obs->vz = p->motionZ;
    obs->yaw = g->yaw;
    obs->pitch = g->pitch;
    obs->on_ground = p->onGround;
    obs->tick = (u64)now->tick;
    obs->combined_hash = combined;

    bx = mc_floor(p->posX);
    by = mc_floor(p->posY) - 1;
    bz = mc_floor(p->posZ);
    if (bx >= 0 && bx < MC_CX && bz >= 0 && bz < MC_CZ && by >= 0 && by < MC_CY) {
        st = mc_get(&now->chunk[0], bx, by, bz);
        if (mc_state_id(st) != 0) {
            obs->look_type = 1;
            obs->look_bx = bx;
            obs->look_by = by;
            obs->look_bz = bz;
        } else {
            obs->look_type = 0;
            obs->look_bx = obs->look_by = obs->look_bz = 0;
        }
    } else {
        obs->look_type = 0;
        obs->look_bx = obs->look_by = obs->look_bz = 0;
    }
}

MC_HD static inline u64 pges_obs_hash(const PgesObs *obs) {
    u64 h = 0xcbf29ce484222325ULL;
    pges_hash_u64(&h, (u64)PGES_SCHEMA_VER);
    pges_hash_double(&h, obs->x);
    pges_hash_double(&h, obs->y);
    pges_hash_double(&h, obs->z);
    pges_hash_double(&h, obs->vx);
    pges_hash_double(&h, obs->vy);
    pges_hash_double(&h, obs->vz);
    pges_hash_double(&h, (double)obs->yaw);
    pges_hash_double(&h, (double)obs->pitch);
    pges_hash_i32(&h, obs->on_ground);
    pges_hash_i32(&h, obs->look_type);
    pges_hash_i32(&h, obs->look_bx);
    pges_hash_i32(&h, obs->look_by);
    pges_hash_i32(&h, obs->look_bz);
    pges_hash_u64(&h, obs->tick);
    pges_hash_u64(&h, obs->combined_hash);
    return h;
}

MC_HD static inline int pges_tick_env(PgesEnv *g, const PpfAction *act) {
    Env *e = &g->e;
    TcfAux *aux = &g->aux;
    TcfScratch *scratch = &g->scratch;
    World *now = twc_now(e);
    World *next = twc_next(e);
    i64 tick = now->tick;
    int n_dec = 0;

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

    ppf_player_tick(&g->primer, &g->st, &aux->te.player, act, (int)tick,
                    aux->te_scratch.blocks);
    maz_tick_one((i64)now->seed, (int)tick, &aux->te.zombie, aux->te_scratch.pf_grid,
                 &g->work, aux->te.player.posX, aux->te.player.posY, aux->te.player.posZ);
    if (aux->te.arrow_active && !aux->te.arrow.inGround)
        pm_arrow_tick(&aux->te.arrow, aux->te_scratch.pm_grid);

    ts_extract_blocks(&next->chunk[0], scratch->spawn_blocks);
    tcf_sync_spawn_light(next, &aux->ts);
    ts_hostile_spawn_cycle(now->seed, tick, scratch->spawn_blocks, &aux->ts,
                           scratch->decisions, &n_dec);

    next->tick = tick + 1;
    twc_swap(e);
    return n_dec;
}

MC_HD static inline void pges_reset(PgesEnv *g, u64 seed) {
    g->seed = seed;
    g->steps = 0;
    g->done = 0;
    g->last_n_dec = 0;
    g->last_reward = 0.0f;
    g->yaw = 0.0f;
    g->pitch = 0.0f;
    mc_sin_table_init(&g->st);
    tcf_init_env(&g->e, &g->aux, seed, &g->primer, &g->sc, &g->st, &g->scratch);
}

MC_HD static inline void pges_step(PgesEnv *g, const PgesAction *action, PgesObs *obs,
                                   float *reward, int *done) {
    PpfAction ppf;
    u64 combined;
    const McPcfEntity *p0 = &g->aux.te.player;
    double px0 = p0->posX, py0 = p0->posY, pz0 = p0->posZ;

    if (g->done) {
        pges_fill_obs(g, obs,
                      pges_world_combined_hash(g, g->last_n_dec, g->scratch.decisions));
        *reward = 0.0f;
        *done = 1;
        return;
    }

    pges_apply_aim(g, action);
    ppf = pges_to_ppf(g, action);
    g->last_n_dec = pges_tick_env(g, &ppf);

    combined = pges_world_combined_hash(g, g->last_n_dec, g->scratch.decisions);
    pges_fill_obs(g, obs, combined);

    {
        const McPcfEntity *p1 = &g->aux.te.player;
        double dx = p1->posX - px0;
        double dy = p1->posY - py0;
        double dz = p1->posZ - pz0;
        *reward = (float)sqrt(dx * dx + dy * dy + dz * dz);
    }
    g->last_reward = *reward;
    g->steps++;
    g->done = (g->steps >= PGES_N_TICKS) ? 1 : 0;
    *done = g->done;
}

MC_HD static inline void pges_obs_after_reset(PgesEnv *g, PgesObs *obs) {
    u64 combined = pges_world_combined_hash(g, g->last_n_dec, g->scratch.decisions);
    pges_fill_obs(g, obs, combined);
}

MC_HD static inline void pges_run_replay(PgesEnv *g, u64 seed,
                                         void (*emit)(u64 obs_hash, void *ctx), void *ctx) {
    PgesObs obs;
    float reward;
    int done;
    int i;

    pges_reset(g, seed);
    pges_obs_after_reset(g, &obs);
    if (emit) emit(pges_obs_hash(&obs), ctx);

    for (i = 0; i < PGES_N_TICKS; ++i) {
        pges_step(g, &PGES_REPLAY[i], &obs, &reward, &done);
        if (emit) emit(pges_obs_hash(&obs), ctx);
    }
}

#endif /* MC_PY_GYM_ENV_SMOKE_H */
