/* mob_ai_enderman: EntityEnderman AI (idle -> chase -> attack) with A* paths + teleport subset.
 * Internal verify: CPU==CUDA, 64 ticks, dump state+pos+teleport per tick (576 lines).
 *
 * Cloned from mob_ai_zombie_astar.h; adds attemptTeleport / teleportRandomly / teleportToEntity
 * from EntityEnderman + AIFindPlayer.updateTask (close tp distSq<16, far tp distSq>256 after 30 ticks).
 * CUT: shouldAttackPlayer pumpkin/stare raycast, block carry/place, water/day damage, screaming sound.
 * Hash RNG keyed (seed,tick,x,y,z,purpose) per SPEC rule 1.
 * READ-ONLY deps: mob_ai_zombie_astar.h (reference), pathfinding.h (pf_find_astar).
 *
 * Scenario (deterministic):
 *   - Flat stone floor, enderman at (2.5, 2.0, 8.5).
 *   - Player script: 0-15 far (dist>11) -> CHASE+far-tp timer; 16-25 mid (4-11) -> CHASE;
 *     26-35 close (<4) -> close tp + ATTACK; 36-63 far -> CHASE+far-tp.
 */
#ifndef MC_MOB_AI_ENDERMAN_H
#define MC_MOB_AI_ENDERMAN_H

#include <math.h>
#include "mc.h"
#include "mc_rng.h"
#include "mc_math.h"
#include "pathfinding.h"

#define MAE_NUM_TICKS         64
#define MAE_FOLLOW_RANGE      64.0
#define MAE_ATTACK_REACH      1.8
#define MAE_MOVE_SPEED        0.30000001192092896
#define MAE_ATTACK_COOLDOWN   20
#define MAE_REPATH_INTERVAL   10
#define MAE_WANDER_INTERVAL   40
#define MAE_ASTAR_RANGE       16
#define MAE_ENTITY_HEIGHT     3
#define MAE_CLOSE_TP_SQ       16.0
#define MAE_FAR_TP_SQ         121.0  /* 11^2; PF_DIM=16 caps max sep ~14.7, Java uses 256 */
#define MAE_FAR_TP_TICKS      30

#define MAE_PURPOSE_WANDER    0x4D414501u
#define MAE_PURPOSE_REPATH    0x4D414502u
#define MAE_PURPOSE_TP_RAND   0x4D414503u
#define MAE_PURPOSE_TP_ENTITY 0x4D414504u

enum {
    MAE_STATE_IDLE   = 0,
    MAE_STATE_CHASE  = 1,
    MAE_STATE_ATTACK = 2,
};

typedef struct {
    double x, y, z;
    float  yaw;
    u32    state;
    u32    attack_time;
    u32    path_idx;
    u32    path_len;
    i32    repath_timer;
    i32    wander_timer;
    i32    teleport_time;
    u32    screaming;
    i16    path_wp[PF_MAX_PATH][3];
} MaeEnderman;

typedef struct {
    u32    state;
    double x, y, z;
    double yaw;
    u32    attack_time;
    u32    path_idx;
    u32    screaming;
    u32    did_teleport;
    u32    teleport_time;
} MaeTickOut;

#ifdef __CUDACC__
__device__ __noinline__ int mae_pf_find_astar_dev(const u16 *grid, int sx, int sy, int sz,
                                                  int gx, int gy, int gz,
                                                  int entity_height, int max_range,
                                                  PfWork *work, PfResult *out);
#endif

MC_HD static inline int mae_pf_find_astar(const u16 *grid, int sx, int sy, int sz,
                                          int gx, int gy, int gz,
                                          int entity_height, int max_range,
                                          PfWork *work, PfResult *out) {
#if defined(__CUDA_ARCH__)
    return mae_pf_find_astar_dev(grid, sx, sy, sz, gx, gy, gz, entity_height, max_range, work, out);
#else
    return pf_find_astar(grid, sx, sy, sz, gx, gy, gz, entity_height, max_range, work, out);
#endif
}

MC_HD static inline double mae_dist_sq(double x0, double y0, double z0,
                                       double x1, double y1, double z1) {
    double dx = x0 - x1;
    double dy = y0 - y1;
    double dz = z0 - z1;
    return dx * dx + dy * dy + dz * dz;
}

MC_HD static inline double mae_dist_xz(double x0, double z0, double x1, double z1) {
    double dx = x0 - x1;
    double dz = z0 - z1;
    return sqrt(dx * dx + dz * dz);
}

MC_HD static inline void mae_player_pos(int tick, double *px, double *py, double *pz) {
    if (tick < 16) {
        *px = 13.6; *py = 2.0; *pz = 8.5;
    } else if (tick < 26) {
        *px = 10.5; *py = 2.0; *pz = 8.5;
    } else if (tick < 36) {
        *px = 3.2; *py = 2.0; *pz = 8.5;
    } else {
        *px = 13.6; *py = 2.0; *pz = 8.5;
    }
}

MC_HD static inline void mae_face(MaeEnderman *e, double tx, double tz) {
    double dx = tx - e->x;
    double dz = tz - e->z;
    if (dx * dx + dz * dz < 1.0e-8) return;
    e->yaw = (float)(atan2(dz, dx) * 180.0 / MC_PI - 90.0);
}

MC_HD static inline int mae_walkable(const u16 *grid, int x, int y, int z) {
    return pf_is_walkable(grid, x, y, z, MAE_ENTITY_HEIGHT);
}

MC_HD static inline int mae_fits_at(const u16 *grid, int bx, int by, int bz) {
    if (!pf_in(bx, by, bz)) return 0;
    if (!pf_is_solid(pf_get(grid, bx, by - 1, bz))) return 0;
    for (int dy = 0; dy < MAE_ENTITY_HEIGHT; ++dy) {
        if (pf_is_solid(pf_get(grid, bx, by + dy, bz))) return 0;
    }
    return 1;
}

MC_HD static inline int mae_attempt_teleport(MaeEnderman *e, const u16 *grid,
                                            double tx, double ty, double tz) {
    double ox = e->x;
    double oy = e->y;
    double oz = e->z;
    int bx, by, bz;

    e->x = tx;
    e->y = ty;
    e->z = tz;

    bx = mc_floor(e->x);
    by = mc_floor(e->y);
    bz = mc_floor(e->z);

    if (!pf_in(bx, by, bz)) {
        e->x = ox;
        e->y = oy;
        e->z = oz;
        return 0;
    }

    while (by > 0 && !mae_fits_at(grid, bx, by, bz)) {
        if (pf_is_solid(pf_get(grid, bx, by - 1, bz))) {
            if (!mae_fits_at(grid, bx, by, bz)) break;
            break;
        }
        e->y -= 1.0;
        by--;
    }

    if (!mae_fits_at(grid, bx, by, bz)) {
        e->x = ox;
        e->y = oy;
        e->z = oz;
        return 0;
    }
    return 1;
}

MC_HD static inline int mae_teleport_randomly(i64 seed, int tick, MaeEnderman *e,
                                              const u16 *grid) {
    u64 h;
    double tx, ty, tz;
    i32 dy_off;

    h = mc_hash_seed((u64)seed, tick, mc_floor(e->x), mc_floor(e->y), mc_floor(e->z),
                     MAE_PURPOSE_TP_RAND);
    tx = e->x + ((double)mc_hash_f01(h) - 0.5) * 64.0;
    h = mc_hash64(h + 1ULL);
    dy_off = mc_hash_bound(h, 64) - 32;
    ty = e->y + (double)dy_off;
    h = mc_hash64(h + 2ULL);
    tz = e->z + ((double)mc_hash_f01(h) - 0.5) * 64.0;
    return mae_attempt_teleport(e, grid, tx, ty, tz);
}

MC_HD static inline int mae_teleport_to_entity(i64 seed, int tick, MaeEnderman *e,
                                               const u16 *grid,
                                               double px, double py, double pz) {
    u64 h;
    double dx, dy, dz, len;
    double tx, ty, tz;
    i32 y_off;

    dx = e->x - px;
    dy = (e->y + (double)MAE_ENTITY_HEIGHT * 0.5) - py;
    dz = e->z - pz;
    len = sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1.0e-6) return 0;
    dx /= len;
    dy /= len;
    dz /= len;

    h = mc_hash_seed((u64)seed, tick, mc_floor(e->x), mc_floor(e->y), mc_floor(e->z),
                     MAE_PURPOSE_TP_ENTITY);
    tx = e->x + ((double)mc_hash_f01(h) - 0.5) * 8.0 - dx * 16.0;
    h = mc_hash64(h + 1ULL);
    y_off = mc_hash_bound(h, 16) - 8;
    ty = e->y + (double)y_off - dy * 16.0;
    h = mc_hash64(h + 2ULL);
    tz = e->z + ((double)mc_hash_f01(h) - 0.5) * 8.0 - dz * 16.0;
    return mae_attempt_teleport(e, grid, tx, ty, tz);
}

MC_HD static inline void mae_move_toward(MaeEnderman *e, const u16 *grid,
                                         double tx, double tz, double speed) {
    double dx = tx - e->x;
    double dz = tz - e->z;
    double dist = sqrt(dx * dx + dz * dz);
    int bx, bz;
    if (dist < 1.0e-6) return;
    {
        double step = speed;
        double nx, nz;
        if (step > dist) step = dist;
        nx = e->x + dx / dist * step;
        nz = e->z + dz / dist * step;
        bx = mc_floor(nx);
        bz = mc_floor(nz);
        if (mae_walkable(grid, bx, 2, bz)) {
            e->x = nx;
            e->z = nz;
        }
    }
    e->y = 2.0;
    mae_face(e, tx, tz);
}

MC_HD static inline void mae_store_path(MaeEnderman *e, const PfResult *res) {
    int n = res->len;
    int i;
    if (n > PF_MAX_PATH) n = PF_MAX_PATH;
    e->path_len = (u32)n;
    e->path_idx = 0;
    for (i = 0; i < n; ++i) {
        e->path_wp[i][0] = res->waypoints[i * 3 + 0];
        e->path_wp[i][1] = res->waypoints[i * 3 + 1];
        e->path_wp[i][2] = res->waypoints[i * 3 + 2];
    }
}

MC_HD static inline void mae_set_path(MaeEnderman *e, const u16 *grid, PfWork *work,
                                      double tx, double ty, double tz) {
    int sx = mc_floor(e->x);
    int sy = mc_floor(e->y);
    int sz = mc_floor(e->z);
    int gx = mc_floor(tx);
    int gy = mc_floor(ty);
    int gz = mc_floor(tz);
    PfResult res;
    int n = mae_pf_find_astar(grid, sx, sy, sz, gx, gy, gz, MAE_ENTITY_HEIGHT,
                              MAE_ASTAR_RANGE, work, &res);
    if (n > 0) {
        mae_store_path(e, &res);
        return;
    }
    e->path_len = 1;
    e->path_idx = 0;
    e->path_wp[0][0] = (i16)gx;
    e->path_wp[0][1] = (i16)gy;
    e->path_wp[0][2] = (i16)gz;
}

MC_HD static inline void mae_clear_path(MaeEnderman *e) {
    e->path_len = 0;
    e->path_idx = 0;
}

MC_HD static inline void mae_follow_path(MaeEnderman *e, const u16 *grid, double speed) {
    double tx, tz;
    if (e->path_len == 0 || e->path_idx >= e->path_len) return;
    tx = (double)e->path_wp[e->path_idx][0] + 0.5;
    tz = (double)e->path_wp[e->path_idx][2] + 0.5;
    mae_move_toward(e, grid, tx, tz, speed);
    if (mae_dist_xz(e->x, e->z, tx, tz) < 0.35)
        e->path_idx++;
}

MC_HD static inline int mae_has_target(double ex, double ey, double ez,
                                       double px, double py, double pz) {
    double fr = MAE_FOLLOW_RANGE;
    return mae_dist_sq(ex, ey, ez, px, py, pz) <= fr * fr;
}

MC_HD static inline int mae_teleport_update(i64 seed, int tick, MaeEnderman *e,
                                            const u16 *grid,
                                            double px, double py, double pz,
                                            int targeted) {
    double dsq;
    int did = 0;

    if (!targeted) {
        e->teleport_time = 0;
        return 0;
    }

    dsq = mae_dist_sq(e->x, e->y, e->z, px, py, pz);
    if (dsq < MAE_CLOSE_TP_SQ) {
        did = mae_teleport_randomly(seed, tick, e, grid);
        e->teleport_time = 0;
        return did;
    }
    if (dsq > MAE_FAR_TP_SQ) {
        e->teleport_time++;
        if (e->teleport_time >= MAE_FAR_TP_TICKS) {
            did = mae_teleport_to_entity(seed, tick, e, grid, px, py, pz);
            e->teleport_time = 0;
        }
        return did;
    }
    e->teleport_time = 0;
    return 0;
}

MC_HD static inline void mae_idle_wander(i64 seed, int tick, MaeEnderman *e, const u16 *grid,
                                         PfWork *work) {
    u64 h;
    int rx, rz, tx, tz;

    if (e->wander_timer > 0) {
        e->wander_timer--;
        mae_follow_path(e, grid, MAE_MOVE_SPEED * 0.8);
        return;
    }
    h = mc_hash_seed((u64)seed, tick, mc_floor(e->x), mc_floor(e->y), mc_floor(e->z),
                     MAE_PURPOSE_WANDER);
    rx = mc_hash_bound(h, 7) - 3;
    rz = mc_hash_bound(mc_hash64(h + 1ULL), 7) - 3;
    tx = mc_floor(e->x) + rx;
    tz = mc_floor(e->z) + rz;
    if (tx < 1) tx = 1;
    if (tx > PF_DIM_X - 2) tx = PF_DIM_X - 2;
    if (tz < 1) tz = 1;
    if (tz > PF_DIM_Z - 2) tz = PF_DIM_Z - 2;
    if (mae_walkable(grid, tx, 2, tz))
        mae_set_path(e, grid, work, (double)tx + 0.5, 2.0, (double)tz + 0.5);
    e->wander_timer = MAE_WANDER_INTERVAL / 2;
    mae_follow_path(e, grid, MAE_MOVE_SPEED * 0.8);
}

MC_HD static inline void mae_tick_one(i64 seed, int tick, MaeEnderman *e,
                                      const u16 *grid, PfWork *work,
                                      double px, double py, double pz,
                                      u32 *did_teleport) {
    u64 h;
    double reach;
    int targeted, in_reach, tp;

    *did_teleport = 0;
    if (e->attack_time > 0) e->attack_time--;

    targeted = mae_has_target(e->x, e->y, e->z, px, py, pz);
    e->screaming = targeted ? 1u : 0u;
    reach = MAE_ATTACK_REACH;
    in_reach = targeted &&
        mae_dist_sq(e->x, e->y, e->z, px, py, pz) <= reach * reach;

    tp = mae_teleport_update(seed, tick, e, grid, px, py, pz, targeted);
    if (tp) {
        *did_teleport = 1u;
        mae_clear_path(e);
    }

    if (in_reach) {
        e->state = MAE_STATE_ATTACK;
        mae_face(e, px, pz);
        mae_clear_path(e);
        if (e->attack_time <= 0)
            e->attack_time = MAE_ATTACK_COOLDOWN;
        return;
    }

    if (targeted) {
        e->state = MAE_STATE_CHASE;
        e->wander_timer = 0;
        if (e->repath_timer <= 0) {
            mae_set_path(e, grid, work, px, py, pz);
            h = mc_hash_seed((u64)seed, tick, mc_floor(e->x), mc_floor(e->y), mc_floor(e->z),
                             MAE_PURPOSE_REPATH);
            e->repath_timer = MAE_REPATH_INTERVAL + mc_hash_bound(h, 5);
        } else {
            e->repath_timer--;
        }
        mae_follow_path(e, grid, MAE_MOVE_SPEED);
        return;
    }

    e->state = MAE_STATE_IDLE;
    e->repath_timer = 0;
    e->screaming = 0;
    mae_idle_wander(seed, tick, e, grid, work);
}

MC_HD static inline void mae_init(MaeEnderman *e) {
    e->x = 2.5;
    e->y = 2.0;
    e->z = 8.5;
    e->yaw = 0.0f;
    e->state = MAE_STATE_IDLE;
    e->attack_time = 0;
    e->path_idx = 0;
    e->path_len = 0;
    e->repath_timer = 0;
    e->wander_timer = 0;
    e->teleport_time = 0;
    e->screaming = 0;
}

MC_HD static inline void mae_run(i64 seed, int nticks, MaeTickOut *out, PfWork *work) {
    u16 grid[PF_VOL];
    MaeEnderman e;
    int t;
    u32 did_tp;

    pf_scene_flat(grid);
    mae_init(&e);

    if (nticks > MAE_NUM_TICKS) nticks = MAE_NUM_TICKS;

    for (t = 0; t < nticks; ++t) {
        double px, py, pz;
        mae_player_pos(t, &px, &py, &pz);
        mae_tick_one(seed, t, &e, grid, work, px, py, pz, &did_tp);
        out[t].state = e.state;
        out[t].x = e.x;
        out[t].y = e.y;
        out[t].z = e.z;
        out[t].yaw = (double)e.yaw;
        out[t].attack_time = e.attack_time;
        out[t].path_idx = e.path_idx;
        out[t].screaming = e.screaming;
        out[t].did_teleport = did_tp;
        out[t].teleport_time = (u32)e.teleport_time;
    }
}

#endif /* MC_MOB_AI_ENDERMAN_H */
