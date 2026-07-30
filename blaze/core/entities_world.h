/* entities_world: a STANDALONE, CPU==CUDA-verified driver that puts ENTITIES (mob spawning + hostile
 * AI intents + A* pathfinding + living-base travel + cross-chunk collision + melee) into a
 * PERSISTENT MULTI-CHUNK world.
 *
 * Composes already-verified kernels over one double-buffered world:
 *   - tick_world_multi  (twm_gen): builds the persistent TWM_DIM x TWM_DIM chunk region from real
 *     chunk_provider terrain. READ-ONLY: I call twm_gen and never edit tick_world_multi.h.
 *   - mob_spawning      (ms_*): one hostile spawn cycle per tick gated on solid-ground + light; each
 *     SPAWN decision (under a cap) actually spawns a zombie ENTITY into the cross-chunk store - the
 *     "entity spawn + cap tracking" real-wiring TODO from WORKQUEUE.
 *   - pathfinding       (pf_find_astar): A* over a 16x32x16 window that FOLLOWS each mob and spans
 *     chunk borders, toward the player, producing a world-coordinate waypoint.
 *   - living_base / entity_hostile_spine: AI SM -> moveForward/strafe/isJumping/yaw intents, then
 *     eb_tick_living (gravity, friction, pcf_entity_move swept AABB) with type landMovementFactor.
 *   - combat_math       (mc_combat_final_damage): melee damage/armor reduction both directions when a
 *     mob and the player are within reach.
 *
 * PROJECTILES: deferred (documented). The target subset (spawn + AI + A* + cross-chunk move + melee)
 * is composed and verified.
 *
 * DETERMINISM (SPEC): runtime randomness is the stateless hash RNG keyed on (seed, tick, world coords,
 * entity id, purpose) - rule 1, so decisions are thread-schedule-independent. Entities double-buffer
 * (rule 3): the tick reads `now`, writes `next`. Float discipline (rule 4): the oracle builds C with
 * -ffp-contract=off and CUDA with --fmad=false. The world terrain is generated once and is READ-ONLY
 * during the entity tick (persistent multi-chunk world); a flat combat-arena platform is overlaid at
 * EW_ARENA_Y (well clear of tick_world_multi's fluid/light/ticker slices) so mobs have a deterministic
 * walkable multi-chunk surface - the same "synthetic controlled scene on real gen" pattern every
 * isolated harness uses.
 *
 * The verify dump is a flat u64 array (identical formatting CPU/CUDA): per tick, per slot
 * {meta(type|alive|ai|id), x,y,z, vx,vy,vz, yaw|health, path_tx, path_tz, chunk(cx|cz)} plus a
 * per-seed summary {cross_chunk_crossings, spawn_count, total_damage_bits}. cross_chunk_crossings>=1
 * and total_damage>0 are the required cross-chunk + combat evidence, right in the diffed output. */
#ifndef MC_ENTITIES_WORLD_H
#define MC_ENTITIES_WORLD_H

#include <math.h>
#include "tick_world_multi.h"          /* TwmWorld/TwmScratch + twm_gen (READ-ONLY reuse) */
#include "player_physics_world.h"      /* pulls physics_collision_math (McAABB) */
#include "pathfinding.h"               /* pf_find_astar + PfWork/PfResult */
#include "combat_math.h"               /* mc_combat_final_damage / armor / weapon */
#include "mob_spawning.h"              /* ms_init_flat / ms_run / MS_RES_SPAWN */
#include "ew_entity_store.h"
#include "entity_hostile_spine.h"      /* AI intents -> living_base spine (eb_tick_living) */

/* ---- tunables ---- */
#define EW_NTICKS          96
#define EW_ARENA_Y         100         /* solid stone platform y; mobs stand at EW_ARENA_Y+1 */
#define EW_STAND_Y         ((double)(EW_ARENA_Y + 1))
#define EW_MAX_BLOCKS      256
#define EW_FOLLOW_RANGE    64.0        /* large: mobs always engage in this scenario */
#define EW_ATTACK_REACH    2.0
#define EW_SKELETON_RANGE  15.0        /* AbstractSkeleton bow engagement distance */
#define EW_CREEPER_SWELL   3.0         /* creeper swell stop distance (approx 3 blocks) */
#define EW_ATTACK_COOLDOWN 16
#define EW_REPATH_INTERVAL 8
#define EW_MOB_CAP         6           /* max concurrent hostiles (slots 1..EW_MOB_CAP) */
#define EW_ZOMBIE_HEALTH   20.0f
#define EW_PLAYER_HEALTH   40.0f
#define EW_ZOMBIE_RAW      4.0f        /* zombie-class melee raw damage (pre-armor) */
#define EW_PLAYER_ARMOR    2           /* mc_combat_armor_set idx (chain, 12 pts) */
#define EW_PLAYER_WEAPON   3           /* mc_combat_weapon_raw idx (iron sword) */

#define EW_PURPOSE_WANDER  0x45571001u
#define EW_PURPOSE_SPAWNPOS 0x45571002u

#define EW_LINES_PER_SLOT  11
#define EW_NSUMMARY        3
#define EW_NLINES          (EW_NTICKS * EW_MAX_ENTITIES * EW_LINES_PER_SLOT + EW_NSUMMARY)

/* runtime state for one env (persistent world + double-buffered entities + running evidence). */
typedef struct {
    EwStore ea;                        /* entity buffer A */
    EwStore eb;                        /* entity buffer B */
    int     ecur;                      /* 0 -> now=ea,next=eb */
    i64     tick;
    u64     seed;
    i32     next_id;
    u64     cross_crossings;           /* running count of chunk-boundary crossings */
    u64     spawn_count;               /* zombies spawned via the mob_spawning cycle */
    float   total_damage;              /* total melee damage dealt (any direction) */
} EwState;

/* per-env scratch kept off the CUDA stack (pathfinding window + work, PcfBlock list, spawn scene). */
typedef struct {
    u16     pf_grid[PF_VOL];
    PfWork  pf_work;
    PfResult pf_res;
    PcfBlock blocks[EW_MAX_BLOCKS];  /* living-spine collision (pcf_entity_move) */
    MsScene ms;
} EwScratch;

MC_HD static inline EwStore *ew_now(EwState *st)  { return st->ecur ? &st->eb : &st->ea; }
MC_HD static inline EwStore *ew_next(EwState *st) { return st->ecur ? &st->ea : &st->eb; }
MC_HD static inline void     ew_swap(EwState *st) { st->ecur ^= 1; }

/* chunk index of a world coordinate (arithmetic shift = floor-div by 16, works for negatives). */
MC_HD static inline int ew_chunk_of(double w) { return mc_floor(w) >> 4; }

/* ---- multi-chunk world block query (world coords) over the persistent `now` chunk buffer ---- */
MC_HD static inline int ew_world_block_id(const Chunk *chunks, int wx, int wy, int wz) {
    int r = TWM_DIM / 2;
    int cx, cz, ci, lx, lz;
    if (wy < 0 || wy > MC_CY - 1) return BLK_AIR;
    cx = wx >> 4;
    cz = wz >> 4;
    if (cx < -r || cx > r || cz < -r || cz > r) return BLK_AIR;   /* outside region = air */
    ci = (cx + r) + (cz + r) * TWM_DIM;
    lx = wx - (cx << 4);
    lz = wz - (cz << 4);
    return mc_state_id(mc_get(&chunks[ci], lx, wy, lz));
}

MC_HD static inline int ew_block_is_solid(int id) {
    if (id == BLK_AIR) return 0;
    BptProps p = mc_bpt_props(id);
    if (p.flags & BF_LIQUID) return 0;      /* liquids pass-through */
    return (p.flags & BF_SOLID) != 0;
}

/* collect solid blocks (world coords) as PcfBlocks for pcf_entity_move / living spine. */
MC_HD static inline int ew_collect_pcf(const Chunk *chunks, const McAABB *q,
                                       PcfBlock *out, int maxb) {
    int n = 0;
    int x0 = mc_floor(q->minX) - 1, x1 = mc_floor(q->maxX) + 1;
    int y0 = mc_floor(q->minY) - 1, y1 = mc_floor(q->maxY) + 1;
    int z0 = mc_floor(q->minZ) - 1, z1 = mc_floor(q->maxZ) + 1;
    int x, y, z;
    if (y0 < 0) y0 = 0;
    if (y1 > MC_CY - 1) y1 = MC_CY - 1;
    for (x = x0; x <= x1; ++x)
        for (y = y0; y <= y1; ++y)
            for (z = z0; z <= z1; ++z) {
                int id = ew_world_block_id(chunks, x, y, z);
                if (!ew_block_is_solid(id)) continue;
                if (n >= maxb) return n;
                out[n].block_id = id;
                out[n].ox = (double)x;
                out[n].oy = (double)y;
                out[n].oz = (double)z;
                out[n].ladder_facing = 0;
                ++n;
            }
    return n;
}

/* ---- terrain overlay: flat stone combat arena across the whole region, both buffers ---- */
MC_HD static inline void ew_overlay_arena(TwmWorld *w) {
    u16 stone = mc_state(BLK_STONE, 0);
    int i, x, z;
    for (i = 0; i < TWM_NCHUNKS; ++i) {
        Chunk *ca = &w->a[i];
        Chunk *cb = &w->b[i];
        for (z = 0; z < MC_CZ; ++z)
            for (x = 0; x < MC_CX; ++x) {
                mc_set(ca, x, EW_ARENA_Y, z, stone);
                mc_set(cb, x, EW_ARENA_Y, z, stone);
                /* clear the two cells above so the walkable surface is unobstructed */
                mc_set(ca, x, EW_ARENA_Y + 1, z, mc_state(BLK_AIR, 0));
                mc_set(cb, x, EW_ARENA_Y + 1, z, mc_state(BLK_AIR, 0));
                mc_set(ca, x, EW_ARENA_Y + 2, z, mc_state(BLK_AIR, 0));
                mc_set(cb, x, EW_ARENA_Y + 2, z, mc_state(BLK_AIR, 0));
            }
    }
}

/* yaw (deg) that makes forward=1 moveRelative head toward (dx,dz): dir = (-sin,cos). */
MC_HD static inline float ew_yaw_toward(double dx, double dz) {
    return ehs_yaw_toward(dx, dz);
}

/* ---- scripted player target: starts by a mob, then flees left across the x=0 chunk border ---- */
MC_HD static inline void ew_player_script(int tick, double *px, double *py, double *pz) {
    double x;
    if (tick < 8)
        x = 13.0;                               /* stand next to zombie 0 (guarantees early melee) */
    else
        x = 13.0 - 0.34 * (double)(tick - 8);   /* flee left; crosses x=0 (chunk 0 -> chunk -1) */
    if (x < -13.0) x = -13.0;
    *px = x;
    *py = EW_STAND_Y;
    *pz = 8.5;
}

/* ---- A* window: 16x32x16 world window centred on the mob, filled from the multi-chunk world ---- */
MC_HD static inline void ew_fill_pf_window(const Chunk *chunks, EwScratch *sc,
                                           int ox, int oy, int oz) {
    int lx, ly, lz;
    for (ly = 0; ly < PF_DIM_Y; ++ly)
        for (lz = 0; lz < PF_DIM_Z; ++lz)
            for (lx = 0; lx < PF_DIM_X; ++lx) {
                int id = ew_world_block_id(chunks, ox + lx, oy + ly, oz + lz);
                sc->pf_grid[pf_idx(lx, ly, lz)] = mc_state(id, 0);
            }
}

/* Repath a chasing mob: A* over a mob-centred window toward the player; set world-coord waypoint. */
MC_HD static inline void ew_repath(const Chunk *chunks, EwScratch *sc, EwStore *nx, int i,
                                   double px, double pz) {
    int zfx = mc_floor(nx->x[i]);
    int zfz = mc_floor(nx->z[i]);
    int ox = zfx - 7;                            /* mob near window centre (local x=7) */
    int oz = zfz - 7;
    int oy = EW_ARENA_Y - 2;                     /* arena floor lands at local y=2, stand at y=3 */
    int slx = zfx - ox, slz = zfz - oz;          /* mob start cell (=7,7) */
    int sly = 3;
    int glx = mc_floor(px) - ox;
    int glz = mc_floor(pz) - oz;
    int gly = 3;
    if (glx < 1) glx = 1; if (glx > PF_DIM_X - 2) glx = PF_DIM_X - 2;
    if (glz < 1) glz = 1; if (glz > PF_DIM_Z - 2) glz = PF_DIM_Z - 2;
    if (slx < 1) slx = 1; if (slx > PF_DIM_X - 2) slx = PF_DIM_X - 2;
    if (slz < 1) slz = 1; if (slz > PF_DIM_Z - 2) slz = PF_DIM_Z - 2;

    ew_fill_pf_window(chunks, sc, ox, oy, oz);
    pf_find_astar(sc->pf_grid, slx, sly, slz, glx, gly, glz, 2, 16, &sc->pf_work, &sc->pf_res);

    if (sc->pf_res.len > 0) {
        /* first waypoint after the start node -> world coords */
        int wx = sc->pf_res.waypoints[0];
        int wz = sc->pf_res.waypoints[2];
        nx->path_tx[i] = (double)(ox + wx) + 0.5;
        nx->path_tz[i] = (double)(oz + wz) + 0.5;
        nx->path_ty[i] = EW_STAND_Y;
        nx->path_len[i] = (u32)sc->pf_res.len;
    } else {
        /* fallback: head straight at the player (A* found nothing on this window) */
        nx->path_tx[i] = px;
        nx->path_tz[i] = pz;
        nx->path_ty[i] = EW_STAND_Y;
        nx->path_len[i] = 0;
    }
}

/* slipperiness by vanilla id (default 0.6, ice/packed-ice 0.98). */
MC_HD static inline float ppw_cb_slipperiness_id(int id) {
    if (id == 79 /* ice */ || id == 174 /* packed ice */) return 0.98f;
    return 0.6f;
}

/* One mob body step via EntityLivingBase spine.
 * AI has already written ai_state / path_* / yaw intent sources onto `nx`; `moving` selects
 * walk-toward-path vs stop. Intents (moveForward/strafe/isJumping/yaw) feed eb_tick_living;
 * landMovementFactor comes from type (zombie 0.23 ... enderman 0.3). No grid teleports. */
MC_HD static inline void ew_mob_move(const Chunk *chunks, EwScratch *sc, const McSinTable *stab,
                                     EwStore *nx, int i, int moving) {
    EhsIntent intent;
    EbLiving liv;
    float slip = 0.6f;
    int blocked;
    int nb;
    McAABB q;
    double pface_x, pface_z;

    /* Face-target for ATTACK intents: path holds last waypoint; player face uses path when
     * caller already set yaw, but ehs_intent_from_ai re-derives from (px,pz) - pass path as
     * stand-in when the caller used path for wander/chase. For ATTACK the tick path passes
     * player coords via path_tx/tz (see ew_tick). */
    pface_x = nx->path_tx[i];
    pface_z = nx->path_tz[i];

    ehs_intent_from_ai(nx->type[i], nx->ai_state[i], moving,
                       nx->x[i], nx->z[i],
                       nx->path_tx[i], nx->path_tz[i],
                       pface_x, pface_z,
                       &intent);
    /* ATTACK branch in ew_tick sets yaw toward the player before calling us; prefer that when
     * stopped so melee facing is not overwritten by a stale path waypoint. */
    if (!moving && nx->ai_state[i] == EW_AI_ATTACK)
        intent.yaw = nx->yaw[i];

    ehs_load_living(&liv, nx, i, &intent);

    if (liv.base.phys.onGround) {
        int bx = mc_floor(liv.base.phys.posX);
        int by = mc_floor(liv.base.phys.box.minY) - 1;
        int bz = mc_floor(liv.base.phys.posZ);
        int bid = ew_world_block_id(chunks, bx, by, bz);
        if (ew_block_is_solid(bid)) slip = ppw_cb_slipperiness_id(bid);
    }

    /* Expand by current motion + stepHeight margin so auto-step sees the floor edge. */
    q = mc_aabb_addcoord(&liv.base.phys.box, liv.base.phys.motionX,
                         liv.base.phys.motionY, liv.base.phys.motionZ);
    q.minY -= (double)liv.base.phys.stepHeight;
    q.maxY += (double)liv.base.phys.stepHeight;
    nb = ew_collect_pcf(chunks, &q, sc->blocks, EW_MAX_BLOCKS);

    blocked = (nx->health[i] <= 0.0f) ? 1 : 0;
    eb_tick_living(&liv, slip, blocked, sc->blocks, nb, stab);
    ehs_store_living(nx, i, &liv);
}

MC_HD static inline double ew_dist_xz(double ax, double az, double bx, double bz) {
    double dx = ax - bx, dz = az - bz;
    return sqrt(dx * dx + dz * dz);
}

/* ---- one full entity tick over the persistent multi-chunk world ----
 * MC_NOINLINE (device): keep this large frame OUT of ew_run/ew_init so the recursive worldgen
 * (cp_provide_chunk/genlayer) does not share a stack frame with the pathfinding/collision tick -
 * the sum would overflow the 256KB max device stack. */
MC_HD MC_NOINLINE static void ew_tick(EwState *st, EwScratch *sc, const Chunk *chunks,
                                      const McSinTable *stab) {
    EwStore *now = ew_now(st);
    EwStore *nx  = ew_next(st);
    i64 tick = st->tick;
    int i;

    ew_store_copy(nx, now);            /* double-buffer baseline (rule 3) */

    /* ---- scripted player target (slot 0) ---- */
    if (now->type[0] == EW_TYPE_PLAYER) {
        double px, py, pz;
        ew_player_script((int)tick, &px, &py, &pz);
        nx->x[0] = px; nx->y[0] = py; nx->z[0] = pz;
        nx->vx[0] = nx->vy[0] = nx->vz[0] = 0.0;
    }

    /* player position observed by every mob this tick = the NOW player (never mid-tick writes). */
    double pnx, pny, pnz;
    ew_player_script((int)tick, &pnx, &pny, &pnz);   /* == now->player after last tick's write */
    (void)now;

    /* ---- hostile AI SM (intents) + living-base travel (read now, write next) ----
     * Decision only: chase/attack/idle produce path + ai_state + yaw. Body motion is
     * eb_tick_living inside ew_mob_move (not kinematic teleport). */
    for (i = 1; i < EW_MAX_ENTITIES; ++i) {
        u8 typ;
        int stop_body = 0;
        int do_melee = 0;

        if (!now->alive[i] || !ehs_is_hostile(now->type[i])) continue;
        typ = now->type[i];

        if (nx->attack_time[i] > 0) nx->attack_time[i]--;

        {
            double zx = now->x[i], zy = now->y[i], zz = now->z[i];
            double d3 = sqrt((zx - pnx) * (zx - pnx) + (zy - pny) * (zy - pny)
                             + (zz - pnz) * (zz - pnz));
            double dxz = ew_dist_xz(zx, zz, pnx, pnz);

            /* Type-specific engagement: melee hostiles stop at EW_ATTACK_REACH; creeper swells
             * a bit farther; skeleton holds for bow inside EW_SKELETON_RANGE. */
            if (typ == EW_TYPE_SKELETON && d3 <= EW_SKELETON_RANGE && dxz > EW_ATTACK_REACH) {
                nx->ai_state[i] = EW_AI_ATTACK;   /* ranged hold (dump schema reuses ATTACK) */
                nx->yaw[i] = ew_yaw_toward(pnx - zx, pnz - zz);
                nx->path_tx[i] = pnx; nx->path_tz[i] = pnz; nx->path_ty[i] = EW_STAND_Y;
                stop_body = 1;
            } else if (typ == EW_TYPE_CREEPER && dxz <= EW_CREEPER_SWELL) {
                nx->ai_state[i] = EW_AI_ATTACK;   /* swell: stop, face */
                nx->yaw[i] = ew_yaw_toward(pnx - zx, pnz - zz);
                nx->path_tx[i] = pnx; nx->path_tz[i] = pnz; nx->path_ty[i] = EW_STAND_Y;
                stop_body = 1;
            } else if (dxz <= EW_ATTACK_REACH) {
                nx->ai_state[i] = EW_AI_ATTACK;
                nx->yaw[i] = ew_yaw_toward(pnx - zx, pnz - zz);
                nx->path_tx[i] = pnx; nx->path_tz[i] = pnz; nx->path_ty[i] = EW_STAND_Y;
                stop_body = 1;
                do_melee = 1;   /* all hostiles may chip the player in reach (harness evidence) */
            } else if (d3 <= EW_FOLLOW_RANGE) {
                nx->ai_state[i] = EW_AI_CHASE;
                if (nx->repath_timer[i] <= 0) {
                    ew_repath(chunks, sc, nx, i, pnx, pnz);
                    {
                        u64 h = mc_hash_seed(st->seed, tick, mc_floor(zx), mc_floor(zy),
                                             mc_floor(zz),
                                             (u32)(0x52455000u ^ (u32)now->id[i]));
                        nx->repath_timer[i] = EW_REPATH_INTERVAL + mc_hash_bound(h, 4);
                    }
                } else {
                    nx->repath_timer[i]--;
                }
                stop_body = 0;
            } else {
                /* IDLE: hash-RNG wander target (rule 1) - intent walk, not teleport */
                nx->ai_state[i] = EW_AI_IDLE;
                {
                    u64 h = mc_hash_seed(st->seed, tick, mc_floor(zx), mc_floor(zy),
                                         mc_floor(zz),
                                         EW_PURPOSE_WANDER ^ (u32)now->id[i]);
                    int rx = mc_hash_bound(h, 7) - 3;
                    int rz = mc_hash_bound(mc_hash64(h + 1ULL), 7) - 3;
                    nx->path_tx[i] = zx + (double)rx;
                    nx->path_tz[i] = zz + (double)rz;
                    nx->path_ty[i] = EW_STAND_Y;
                }
                stop_body = 0;
            }

            if (do_melee && nx->attack_time[i] <= 0) {
                McCombatArmor pa = mc_combat_armor_set(EW_PLAYER_ARMOR);
                float dmg = mc_combat_final_damage(EW_ZOMBIE_RAW, &pa);
                nx->health[0] -= dmg;
                st->total_damage += dmg;
                nx->attack_time[i] = EW_ATTACK_COOLDOWN;
            }

            ew_mob_move(chunks, sc, stab, nx, i, stop_body ? 0 : 1);
        }

        /* cross-chunk crossing evidence: chunk index changed this tick? */
        {
            int ncx = ew_chunk_of(nx->x[i]);
            int ncz = ew_chunk_of(nx->z[i]);
            if (ncx != now->cx[i] || ncz != now->cz[i]) st->cross_crossings++;
            nx->cx[i] = ncx;
            nx->cz[i] = ncz;
        }
    }

    /* player track its own chunk crossing too (it is an entity in the store). */
    {
        int ncx = ew_chunk_of(nx->x[0]);
        int ncz = ew_chunk_of(nx->z[0]);
        if (now->type[0] == EW_TYPE_PLAYER && (ncx != now->cx[0] || ncz != now->cz[0]))
            st->cross_crossings++;
        nx->cx[0] = ncx;
        nx->cz[0] = ncz;
    }

    /* ---- player retaliation: hit the nearest in-reach hostile (combat_math, other direction) ---- */
    if (nx->attack_time[0] > 0) nx->attack_time[0]--;
    if (nx->attack_time[0] <= 0) {
        int best = -1;
        double bestd = EW_ATTACK_REACH;
        for (i = 1; i < EW_MAX_ENTITIES; ++i) {
            if (!now->alive[i] || !ehs_is_hostile(now->type[i])) continue;
            double d = ew_dist_xz(now->x[i], now->z[i], pnx, pnz);
            if (d <= bestd) { bestd = d; best = i; }
        }
        if (best >= 0) {
            float raw = mc_combat_weapon_raw(EW_PLAYER_WEAPON);
            McCombatArmor za = mc_combat_armor_set(0);   /* hostiles: no armor */
            float dmg = mc_combat_final_damage(raw, &za);
            nx->health[best] -= dmg;
            st->total_damage += dmg;
            nx->attack_time[0] = EW_ATTACK_COOLDOWN;
            if (nx->health[best] <= 0.0f) { nx->alive[best] = 0; }
        }
    }

    /* ---- mob spawning cycle: gate on solid-ground + light (verified kernel), spawn under cap ---- */
    {
        int alive_mobs = 0, s;
        ms_run(&sc->ms, tick);          /* one WorldEntitySpawner hostile cycle (hash RNG) */
        for (i = 1; i < EW_MAX_ENTITIES; ++i)
            if (ehs_is_hostile(nx->type[i]) && nx->alive[i]) alive_mobs++;
        if (alive_mobs < EW_MOB_CAP) {
            for (s = 0; s < sc->ms.n_decisions; ++s) {
                u64 d = sc->ms.decisions[s];
                int result = (int)((d >> 56) & 0xF);
                if (result != MS_RES_SPAWN) continue;
                {
                    int dxl = (int)((d >> 16) & 0xFF);   /* ms local x in [0,16) */
                    int dzl = (int)((d >> 32) & 0xFF);   /* ms local z in [0,16) */
                    /* map into the region so successive spawns land in different chunks */
                    double wx = (double)(-14 + 2 * dxl) + 0.5;
                    double wz = (double)(-14 + (dzl % 8)) + 0.5;
                    int slot;
                    if (wx < -15.0) wx = -15.0; if (wx > 31.0) wx = 31.0;
                    /* spawned hostiles are zombies (canonical ms monster); speed via type switch */
                    slot = ew_store_spawn(nx, EW_TYPE_ZOMBIE, st->next_id, wx, EW_STAND_Y, wz,
                                          EW_ZOMBIE_HEALTH);
                    if (slot >= 0) {
                        nx->cx[slot] = ew_chunk_of(wx);
                        nx->cz[slot] = ew_chunk_of(wz);
                        st->next_id++;
                        st->spawn_count++;
                    }
                    break;   /* at most one spawn per tick */
                }
            }
        }
    }

    st->tick = tick + 1;
    ew_swap(st);
}

/* Darken the mob_spawning scene into a roofed shaft so the VERIFIED spawn cycle actually passes its
 * light + solid-ground + player/spawn-exclusion gates and emits live spawns (the fixed synthetic
 * flat scene never does, being well-lit and centred on ms's internal player). We only reshape the
 * scene BLOCKS + rebuild light with ms_build_light; the gating logic (ms_hostile_spawn_cycle) is
 * untouched, verified kernel code. Spawns become seed-dependent (real gating over the dark shaft). */
MC_HD static inline void ew_darken_ms(MsScene *ms) {
    u16 stone = mc_state(BLK_STONE, 0);
    u16 air = mc_state(BLK_AIR, 0);
    int x, y, z;
    for (z = 0; z < MS_NZ; ++z)
        for (x = 0; x < MS_NX; ++x) {
            for (y = 5; y < MS_NY; ++y) {
                int id = mc_state_id(ms_get(ms->blocks, x, y, z));
                if (id == BLK_TORCH || id == BLK_STONE) ms_set(ms->blocks, x, y, z, air);
            }
            ms_set(ms->blocks, x, 27, z, stone);   /* dark spawn platform (edges escape player sphere) */
            ms_set(ms->blocks, x, 35, z, stone);   /* roof -> sky light 0 in the shaft below */
        }
    ms_build_light(ms->blocks, ms->sky, ms->blk);
}

/* ---- scene init: generate world, overlay arena, place the scripted player + scenario mobs ----
 * MC_NOINLINE (device): isolates the recursive worldgen frame (twm_gen -> cp_provide_chunk) from
 * ew_run so the per-thread stack stays within the 256KB device max. */
MC_HD MC_NOINLINE static void ew_init(EwState *st, EwScratch *sc, TwmWorld *w, TwmScratch *ts,
                                      ChunkPrimer *primer, CpScratch *cpsc, const McSinTable *stab,
                                      u64 seed) {
    twm_gen(w, ts, primer, cpsc, stab, seed);     /* persistent multi-chunk world (READ-ONLY reuse) */
    ew_overlay_arena(w);

    ew_store_clear(&st->ea);
    ew_store_clear(&st->eb);
    st->ecur = 0;
    st->tick = 0;
    st->seed = seed;
    st->next_id = 100;
    st->cross_crossings = 0;
    st->spawn_count = 0;
    st->total_damage = 0.0f;

    ms_init_flat(&sc->ms, seed);
    ew_darken_ms(&sc->ms);

    {
        EwStore *s = &st->ea;
        /* slot 0: scripted player target */
        double px, py, pz;
        ew_player_script(0, &px, &py, &pz);
        s->type[0] = EW_TYPE_PLAYER;
        s->alive[0] = 1;
        s->id[0] = 0;
        s->x[0] = px; s->y[0] = py; s->z[0] = pz;
        s->health[0] = EW_PLAYER_HEALTH;
        s->on_ground[0] = 1;
        s->cx[0] = ew_chunk_of(px);
        s->cz[0] = ew_chunk_of(pz);
        if (s->count < 1) s->count = 1;

        /* scenario hostiles: one of each type so landMovementFactor type-switch is exercised.
         * Adjacent zombie for immediate melee; others chase across chunk borders. */
        ew_store_spawn(s, EW_TYPE_ZOMBIE,   1, 11.5, EW_STAND_Y,  8.5, EW_ZOMBIE_HEALTH);
        ew_store_spawn(s, EW_TYPE_SKELETON, 2, 20.5, EW_STAND_Y,  8.5, EW_ZOMBIE_HEALTH);
        ew_store_spawn(s, EW_TYPE_CREEPER,  3, 27.5, EW_STAND_Y, 10.5, EW_ZOMBIE_HEALTH);
        ew_store_spawn(s, EW_TYPE_SPIDER,   4, 15.5, EW_STAND_Y, 12.5, EW_ZOMBIE_HEALTH);
        ew_store_spawn(s, EW_TYPE_ENDERMAN, 5, 24.5, EW_STAND_Y,  6.5, EW_ZOMBIE_HEALTH);
        for (int i = 1; i < EW_MAX_ENTITIES; ++i) {
            if (ehs_is_hostile(s->type[i])) {
                s->cx[i] = ew_chunk_of(s->x[i]);
                s->cz[i] = ew_chunk_of(s->z[i]);
            }
        }
    }
    ew_store_copy(&st->eb, &st->ea);
}

/* ---- dump helpers (bit-exact reinterpret so CPU and CUDA emit identical hex) ---- */
MC_HD static inline u64 ew_dbits(double d) {
    union { double d; u64 u; } v; v.d = d; return v.u;
}
MC_HD static inline u32 ew_fbits(float f) {
    union { float f; u32 u; } v; v.f = f; return v.u;
}

MC_HD static inline void ew_dump_tick(const EwStore *s, u64 *out, int base) {
    int i, o = base;
    for (i = 0; i < EW_MAX_ENTITIES; ++i) {
        u64 meta = (u64)s->type[i]
                 | ((u64)s->alive[i] << 8)
                 | ((u64)(s->ai_state[i] & 0xFF) << 16)
                 | ((u64)(u32)s->id[i] << 32);
        out[o++] = meta;
        out[o++] = ew_dbits(s->x[i]);
        out[o++] = ew_dbits(s->y[i]);
        out[o++] = ew_dbits(s->z[i]);
        out[o++] = ew_dbits(s->vx[i]);
        out[o++] = ew_dbits(s->vy[i]);
        out[o++] = ew_dbits(s->vz[i]);
        out[o++] = ((u64)ew_fbits(s->yaw[i]) << 32) | (u64)ew_fbits(s->health[i]);
        out[o++] = ew_dbits(s->path_tx[i]);
        out[o++] = ew_dbits(s->path_tz[i]);
        out[o++] = ((u64)(u32)s->cx[i] << 32) | (u64)(u32)s->cz[i];
    }
}

/* run one env into a flat u64 buffer of EW_NLINES entries (identical CPU/CUDA). */
MC_HD static inline void ew_run(EwState *st, EwScratch *sc, TwmWorld *w, TwmScratch *ts,
                                ChunkPrimer *primer, CpScratch *cpsc, const McSinTable *stab,
                                u64 seed, u64 *out) {
    int t;
    ew_init(st, sc, w, ts, primer, cpsc, stab, seed);
    for (t = 0; t < EW_NTICKS; ++t) {
        const Chunk *chunks = twm_now(w);          /* persistent terrain (read-only during entity tick) */
        ew_tick(st, sc, chunks, stab);
        ew_dump_tick(ew_now(st), out, t * EW_MAX_ENTITIES * EW_LINES_PER_SLOT);
    }
    {
        int b = EW_NTICKS * EW_MAX_ENTITIES * EW_LINES_PER_SLOT;
        out[b + 0] = st->cross_crossings;
        out[b + 1] = st->spawn_count;
        out[b + 2] = (u64)ew_fbits(st->total_damage);
    }
}

#endif /* MC_ENTITIES_WORLD_H */
