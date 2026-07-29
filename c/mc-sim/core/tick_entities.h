/* tick_entities: Wave 14 - compose tick_world_copy + player_physics_full + mob_ai_zombie_astar
 * + optional projectile_motion per tick on a 16x256x16 env.
 *
 * INTERNAL verify (CPU==CUDA). Each tick: copy now->next, one player physics step on
 * cp_provide_chunk terrain, one zombie A* AI tick (player pos as target), optional arrow step.
 * Per-tick dump: tick number, entity-state FNV hash, cur buffer index (16 ticks).
 * READ-ONLY deps: tick_world_copy.h, player_physics_full.h, mob_ai_zombie_astar.h,
 * projectile_motion.h. */
#ifndef MC_TICK_ENTITIES_H
#define MC_TICK_ENTITIES_H

#include <math.h>
#include "tick_world_copy.h"
#include "player_physics_full.h"
#include "mob_ai_zombie_astar.h"
#include "projectile_motion.h"

#define TE_PURPOSE_ARROW 0x54454501u

typedef struct {
    McPcfEntity player;
    MazZombie   zombie;
    McArrow     arrow;
    i32         arrow_active;
} TeAux;

typedef struct {
    u16 pf_grid[PF_VOL];
    u16 pm_grid[PM_VOL];
    PcfBlock blocks[PPF_MAX_BLOCKS];
} TeScratch;

MC_HD static inline u16 te_cb_to_state(int cb) {
    return mc_state((int)ppf_cb_to_vanilla((u16)cb), 0);
}

MC_HD static inline void te_load_primer_to_chunk(Chunk *c, const ChunkPrimer *p) {
    int x, y, z;
    for (y = 0; y < MC_CY; ++y)
        for (z = 0; z < MC_CZ; ++z)
            for (x = 0; x < MC_CX; ++x)
                mc_set(c, x, y, z, te_cb_to_state(cb_get(p, x, y, z)));
}

MC_HD static inline int te_arrow_active(u64 seed) {
    return (seed % 3) != 0;
}

MC_HD static inline void te_init_arrow(McArrow *a, u64 seed) {
    (void)seed;
    a->inGround = 0;
    a->ticksInAir = 0;
    a->posX = 8.0;
    a->posY = 15.0;
    a->posZ = 8.0;
    a->motionX = 0.3;
    a->motionY = 1.5;
    a->motionZ = 0.0;
}

MC_HD static inline void te_hash_u64(u64 *h, u64 v) {
    *h ^= v;
    *h *= 0x100000001b3ULL;
}

MC_HD static inline void te_hash_double(u64 *h, double d) {
    union { double d; u64 u; } u;
    u.d = d;
    te_hash_u64(h, u.u);
}

MC_HD static inline u64 te_entity_hash(const TeAux *aux) {
    u64 h = 0xcbf29ce484222325ULL;
    const McPcfEntity *p = &aux->player;
    const MazZombie *z = &aux->zombie;

    te_hash_double(&h, p->posX);
    te_hash_double(&h, p->posY);
    te_hash_double(&h, p->posZ);
    te_hash_double(&h, p->motionX);
    te_hash_double(&h, p->motionY);
    te_hash_double(&h, p->motionZ);
    te_hash_u64(&h, (u64)(u32)p->onGround);
    te_hash_u64(&h, (u64)(u32)p->isSneaking);

    te_hash_u64(&h, (u64)z->state);
    te_hash_double(&h, z->x);
    te_hash_double(&h, z->y);
    te_hash_double(&h, z->z);
    te_hash_double(&h, (double)z->yaw);
    te_hash_u64(&h, (u64)z->attack_time);
    te_hash_u64(&h, (u64)z->path_idx);

    te_hash_u64(&h, (u64)(u32)aux->arrow_active);
    if (aux->arrow_active) {
        te_hash_double(&h, aux->arrow.posX);
        te_hash_double(&h, aux->arrow.posY);
        te_hash_double(&h, aux->arrow.posZ);
        te_hash_double(&h, aux->arrow.motionX);
        te_hash_double(&h, aux->arrow.motionY);
        te_hash_double(&h, aux->arrow.motionZ);
        te_hash_u64(&h, (u64)(u32)aux->arrow.inGround);
    }
    return h;
}

MC_HD static inline void te_init_env(Env *e, TeAux *aux, u64 seed,
                                      ChunkPrimer *primer, CpScratch *sc,
                                      const McSinTable *st,
                                      TeScratch *scratch) {
    twc_init_env(e, seed);
    cp_provide_chunk(primer, sc, st, seed, 0, 0);
    te_load_primer_to_chunk(&twc_now(e)->chunk[0], primer);
    twc_copy_world(twc_next(e), twc_now(e));

    ppf_init_entity(&aux->player);
    aux->player.posX = PPF_SPAWN_X;
    aux->player.posY = PPF_SPAWN_Y;
    aux->player.posZ = PPF_SPAWN_Z;
    aux->player.box = mc_pcm_player_box(aux->player.posX, aux->player.posY, aux->player.posZ);

    maz_init(&aux->zombie);
    pf_scene_flat(scratch->pf_grid);

    aux->arrow_active = te_arrow_active(seed);
    pm_scene_flat(scratch->pm_grid);
    if (aux->arrow_active)
        te_init_arrow(&aux->arrow, seed);
}

MC_HD static inline void te_tick_env(Env *e, TeAux *aux,
                                      ChunkPrimer *primer, const McSinTable *st,
                                      TeScratch *scratch, PfWork *work) {
    World *now = twc_now(e);
    World *next = twc_next(e);
    i64 tick = now->tick;

    twc_copy_world(next, now);

    {
        PpfAction act = ppf_action_for_tick((i64)now->seed, (int)tick);
        ppf_player_tick(primer, st, &aux->player, &act, (int)tick, scratch->blocks);
    }

    maz_tick_one((i64)now->seed, (int)tick, &aux->zombie, scratch->pf_grid, work,
                 aux->player.posX, aux->player.posY, aux->player.posZ);

    if (aux->arrow_active && !aux->arrow.inGround)
        pm_arrow_tick(&aux->arrow, scratch->pm_grid);

    next->tick = tick + 1;
    twc_swap(e);
}

typedef TwcEmitFn TeEmitFn;

MC_HD static inline void te_run(Env *e, TeAux *aux, u64 seed,
                                ChunkPrimer *primer, CpScratch *sc,
                                const McSinTable *st, PfWork *work,
                                TeScratch *scratch,
                                TeEmitFn emit, void *ctx) {
    int t;

    te_init_env(e, aux, seed, primer, sc, st, scratch);

    for (t = 0; t < TWC_NTICKS; ++t) {
        World *now;
        u64 tick_bits, ent_hash, cur_bits;

        te_tick_env(e, aux, primer, st, scratch, work);
        now = twc_now(e);
        tick_bits = (u64)now->tick;
        ent_hash = te_entity_hash(aux);
        cur_bits = (u64)(u32)e->cur;
        if (emit) emit(tick_bits, ent_hash, cur_bits, ctx);
    }
}

#endif /* MC_TICK_ENTITIES_H */
