/* ender_dragon_death: EntityDragon.onDeathUpdate + DragonFightManager.processDragonDeath subset.
 *
 * PORT TARGET: entity/boss/EntityDragon.onDeathUpdate (death_ticks, rise/yaw animation),
 * DragonFightManager.processDragonDeath (WorldGenEndPodium active portal + dragon egg if first kill).
 * READ-ONLY dep: ender_dragon.h (EdArena movement pre-kill only).
 *
 * Fixed 17x37x17 slice (world y 56..92, x/z +-8) around End exit podium (0,63,0). Multi-scenario
 * battery: kill at configurable tick, run through death_ticks>=200 (EDE_NUM_TICKS=210). Dump 10
 * u64 fields/tick. Build -ffp-contract=off / --fmad=false. CUT: XP orbs, particles, gateway spawn,
 * broadcast sound, doMobLoot gating, boss bar. */
#ifndef MC_ENDER_DRAGON_DEATH_H
#define MC_ENDER_DRAGON_DEATH_H

#include <math.h>
#include <string.h>
#include "mc.h"
#include "mc_blocks.h"
#include "mc_math.h"
#include "mc_rng.h"
#include "mc_world.h"
#include "ender_dragon.h"

#define EDE_W           17
#define EDE_H           37
#define EDE_D           17
#define EDE_VOL         (EDE_W * EDE_H * EDE_D)
#define EDE_Y0          56
#define EDE_NUM_TICKS   220
#define EDE_DUMP_FIELDS 10
#define EDE_NUM_SCENARIOS 4

#define EDE_DEATH_RISE  0.10000000149011612
#define EDE_DEATH_YAW   20.0f
#define EDE_DEATH_END   200

enum {
    EDE_BLK_END_PORTAL = 119,
    EDE_BLK_DRAGON_EGG = 122
};

typedef struct {
    u16     blocks[EDE_VOL];
    EdArena arena;
    u8      previously_killed;
    u8      portal_generated;
    u8      egg_placed;
    u8      death_processed;
    int     exit_portal_y;
    int     kill_tick;
    u64     seed;
    i64     tick;
} EdeWorld;

typedef struct {
    u64  seed;
    int  kill_tick;
    u8   previously_killed;
} EdeScenario;

MC_HD static inline int ede_idx(int lx, int ly, int lz) {
    return (ly * EDE_D + lz) * EDE_W + lx;
}

MC_HD static inline int ede_in_local(int lx, int ly, int lz) {
    return lx >= 0 && lx < EDE_W && ly >= 0 && ly < EDE_H && lz >= 0 && lz < EDE_D;
}

MC_HD static inline int ede_world_to_local(int wx, int wy, int wz, int *lx, int *ly, int *lz) {
    *lx = wx + EDE_W / 2;
    *ly = wy - EDE_Y0;
    *lz = wz + EDE_D / 2;
    return ede_in_local(*lx, *ly, *lz);
}

MC_HD static inline u16 ede_get_world(const EdeWorld *w, int wx, int wy, int wz) {
    int lx, ly, lz;
    if (!ede_world_to_local(wx, wy, wz, &lx, &ly, &lz)) return mc_state(BLK_AIR, 0);
    return w->blocks[ede_idx(lx, ly, lz)];
}

MC_HD static inline void ede_set_world(EdeWorld *w, int wx, int wy, int wz, u16 st) {
    int lx, ly, lz;
    if (!ede_world_to_local(wx, wy, wz, &lx, &ly, &lz)) return;
    w->blocks[ede_idx(lx, ly, lz)] = st;
}

MC_HD static inline double ede_block_dist(int bx, int by, int bz, int px, int py, int pz) {
    double dx = (double)bx - (double)px;
    double dy = (double)by - (double)py;
    double dz = (double)bz - (double)pz;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

/* WorldGenEndPodium.generate subset at (px,py,pz), activePortal=true. */
MC_HD static inline void ede_generate_podium(EdeWorld *w, int px, int py, int pz, int active) {
    u16 air = mc_state(BLK_AIR, 0);
    u16 bed = mc_state(BLK_BEDROCK, 0);
    u16 end = mc_state(BLK_END_STONE, 0);
    u16 portal = mc_state(EDE_BLK_END_PORTAL, 0);

    for (int bz = pz - 4; bz <= pz + 4; ++bz) {
        for (int by = py - 1; by <= py + 32; ++by) {
            for (int bx = px - 4; bx <= px + 4; ++bx) {
                double d0 = ede_block_dist(bx, by, bz, px, py, pz);
                if (d0 > 3.5) continue;
                if (by < py) {
                    if (d0 <= 2.5)
                        ede_set_world(w, bx, by, bz, bed);
                    else
                        ede_set_world(w, bx, by, bz, end);
                } else if (by > py) {
                    ede_set_world(w, bx, by, bz, air);
                } else if (d0 > 2.5) {
                    ede_set_world(w, bx, by, bz, bed);
                } else if (active) {
                    ede_set_world(w, bx, by, bz, portal);
                } else {
                    ede_set_world(w, bx, by, bz, air);
                }
            }
        }
    }

    for (int i = 0; i < 4; ++i)
        ede_set_world(w, px, py + i, pz, bed);

    /* Torches at py+2 on four horizontal faces (BlockTorch.FACING = attach face). */
    int ty = py + 2;
    ede_set_world(w, px, ty, pz - 1, mc_state(BLK_TORCH, 2)); /* north */
    ede_set_world(w, px, ty, pz + 1, mc_state(BLK_TORCH, 3)); /* south */
    ede_set_world(w, px - 1, ty, pz, mc_state(BLK_TORCH, 4)); /* west */
    ede_set_world(w, px + 1, ty, pz, mc_state(BLK_TORCH, 5)); /* east */
}

MC_HD static inline int ede_get_height(const EdeWorld *w, int wx, int wz) {
    for (int wy = EDE_Y0 + EDE_H - 1; wy >= EDE_Y0; --wy) {
        int id = mc_state_id(ede_get_world(w, wx, wy, wz));
        if (id != BLK_AIR) return wy;
    }
    return EDE_Y0;
}

MC_HD static inline void ede_place_dragon_egg(EdeWorld *w) {
    int ey = ede_get_height(w, 0, 0);
    ede_set_world(w, 0, ey, 0, mc_state(EDE_BLK_DRAGON_EGG, 0));
    w->egg_placed = 1;
}

MC_HD static inline void ede_process_dragon_death(EdeWorld *w) {
    if (w->portal_generated) return;
    ede_generate_podium(w, 0, w->exit_portal_y, 0, 1);
    w->portal_generated = 1;
    if (!w->previously_killed)
        ede_place_dragon_egg(w);
    w->previously_killed = 1;
}

MC_HD static inline void ede_on_death_tick(EdeWorld *w) {
    EdDragon *d = &w->arena.dragon;
    d->death_ticks++;
    d->y += EDE_DEATH_RISE;
    d->yaw += EDE_DEATH_YAW;

    if (d->death_ticks >= EDE_DEATH_END && !w->death_processed) {
        ede_process_dragon_death(w);
        w->death_processed = 1;
        d->alive = 0;
    }
}

MC_HD static inline void ede_trigger_kill(EdeWorld *w) {
    w->arena.dragon.health = 0.0f;
    ede_on_death_tick(w);
}

MC_HD static inline void ede_init_scene(EdeWorld *w, const EdeScenario *sc) {
    w->seed = sc->seed;
    w->tick = 0;
    w->kill_tick = sc->kill_tick;
    w->previously_killed = sc->previously_killed;
    w->portal_generated = 0;
    w->egg_placed = 0;
    w->death_processed = 0;
    w->exit_portal_y = 63;

    u16 air = mc_state(BLK_AIR, 0);
    u16 end = mc_state(BLK_END_STONE, 0);
    u16 bed = mc_state(BLK_BEDROCK, 0);

    for (int ly = 0; ly < EDE_H; ++ly) {
        u16 st = air;
        int wy = EDE_Y0 + ly;
        if (wy <= w->exit_portal_y)
            st = (wy == EDE_Y0) ? bed : end;
        for (int lz = 0; lz < EDE_D; ++lz)
            for (int lx = 0; lx < EDE_W; ++lx)
                w->blocks[ede_idx(lx, ly, lz)] = st;
    }

    /* Inactive exit fountain (activated on dragon death). */
    ede_generate_podium(w, 0, w->exit_portal_y, 0, 0);

    ed_init(&w->arena, sc->seed);
    w->arena.dragon.health = w->arena.dragon.max_health;
    w->arena.dragon.death_ticks = 0;
    w->arena.dragon.alive = 1;
    w->arena.dragon.y = 100.0 + (double)(sc->seed % 5u);
    w->arena.dragon.yaw = (float)(sc->seed % 360u);
}

MC_HD static inline int ede_count_block(const EdeWorld *w, int bid) {
    int n = 0;
    for (int i = 0; i < EDE_VOL; ++i)
        if (mc_state_id(w->blocks[i]) == bid) n++;
    return n;
}

MC_HD static inline u64 ede_bitcopy64(const void *p) {
    u64 bits;
    memcpy(&bits, p, 8);
    return bits;
}

MC_HD static inline void ede_pack_tick(const EdeWorld *w, u64 *out) {
    const EdDragon *d = &w->arena.dragon;
    int egg_y = ede_get_height(w, 0, 0);
    out[0] = (u64)(u32)d->death_ticks;
    out[1] = (u64)(u32)d->alive;
    out[2] = ede_bitcopy64(&d->y);
    out[3] = ede_bitcopy64(&d->yaw);
    out[4] = (u64)(u32)w->portal_generated;
    out[5] = (u64)(u32)w->egg_placed;
    out[6] = (u64)(u32)ede_count_block(w, EDE_BLK_END_PORTAL);
    out[7] = (u64)(u32)mc_state_id(ede_get_world(w, 0, w->exit_portal_y, 0));
    out[8] = (u64)(u32)mc_state_id(ede_get_world(w, 0, egg_y, 0));
    out[9] = (u64)(u32)w->death_processed;
}

MC_HD static inline void ede_scenario_params(int idx, EdeScenario *sc) {
    switch (idx) {
    case 0: sc->seed = 12345ULL; sc->kill_tick = 0;  sc->previously_killed = 0; break;
    case 1: sc->seed = 0ULL;     sc->kill_tick = 0;  sc->previously_killed = 0; break;
    case 2: sc->seed = 7ULL;     sc->kill_tick = 0;  sc->previously_killed = 1; break;
    case 3: sc->seed = 999ULL;   sc->kill_tick = 12; sc->previously_killed = 0; break;
    default: sc->seed = 12345ULL; sc->kill_tick = 0; sc->previously_killed = 0; break;
    }
}

MC_HD static inline void ede_run(EdeWorld *w, const McSinTable *st, const EdeScenario *sc,
                                 int nticks, u64 *out) {
    ede_init_scene(w, sc);
    u8 killed = 0;

    for (int t = 0; t < nticks; ++t) {
        w->tick = t;
        if (!killed && t == w->kill_tick) {
            ede_trigger_kill(w);
            killed = 1;
        } else if (killed || w->arena.dragon.death_ticks > 0) {
            ede_on_death_tick(w);
        } else {
            ed_tick_dragon(&w->arena, st);
        }
        ede_pack_tick(w, out + (size_t)t * EDE_DUMP_FIELDS);
    }
}

MC_HD static inline void ede_run_scenario(int idx, const McSinTable *st, EdeWorld *w,
                                            int nticks, u64 *out) {
    EdeScenario sc;
    ede_scenario_params(idx, &sc);
    ede_run(w, st, &sc, nticks, out);
}

#endif /* MC_ENDER_DRAGON_DEATH_H */
