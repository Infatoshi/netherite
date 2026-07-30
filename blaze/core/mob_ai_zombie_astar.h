/* mob_ai_zombie_astar: EntityZombie AI (idle -> chase -> attack) with real pf_find_astar paths.
 * Internal verify: CPU==CUDA, 64 ticks, dump state+pos per tick (448 lines).
 *
 * Same harness/scenario as mob_ai_zombie.h; replaces straight-line path stub with A* waypoints.
 * READ-ONLY deps: mob_ai_zombie.h (reference), pathfinding.h (pf_find_astar).
 *
 * Scenario (deterministic):
 *   - Flat stone floor, zombie at (2.5, 2.0, 8.5).
 *   - Player script: ticks 0-15 far -> IDLE; 16-45 mid -> CHASE; 46-63 adjacent -> ATTACK.
 */
#ifndef MC_MOB_AI_ZOMBIE_ASTAR_H
#define MC_MOB_AI_ZOMBIE_ASTAR_H

#include <math.h>
#include "mc.h"
#include "mc_rng.h"
#include "mc_math.h"
#include "pathfinding.h"

#define MAZ_NUM_TICKS       64
#define MAZ_FOLLOW_RANGE    8.0
#define MAZ_ATTACK_REACH    1.8
#define MAZ_MOVE_SPEED      0.115
#define MAZ_ATTACK_COOLDOWN 20
#define MAZ_REPATH_INTERVAL 10
#define MAZ_WANDER_INTERVAL 40
#define MAZ_ASTAR_RANGE     16

#define MAZ_PURPOSE_WANDER  0x4D415A01u
#define MAZ_PURPOSE_REPATH  0x4D415A02u

enum {
    MAZ_STATE_IDLE   = 0,
    MAZ_STATE_CHASE  = 1,
    MAZ_STATE_ATTACK = 2,
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
    i16    path_wp[PF_MAX_PATH][3];
} MazZombie;

typedef struct {
    u32    state;
    double x, y, z;
    double yaw;
    u32    attack_time;
    u32    path_idx;
} MazTickOut;

#ifdef __CUDACC__
__device__ __noinline__ int maz_pf_find_astar_dev(const u16 *grid, int sx, int sy, int sz,
                                                  int gx, int gy, int gz,
                                                  int entity_height, int max_range,
                                                  PfWork *work, PfResult *out);
#endif

MC_HD static inline int maz_pf_find_astar(const u16 *grid, int sx, int sy, int sz,
                                          int gx, int gy, int gz,
                                          int entity_height, int max_range,
                                          PfWork *work, PfResult *out) {
#if defined(__CUDA_ARCH__)
    return maz_pf_find_astar_dev(grid, sx, sy, sz, gx, gy, gz, entity_height, max_range, work, out);
#else
    return pf_find_astar(grid, sx, sy, sz, gx, gy, gz, entity_height, max_range, work, out);
#endif
}

MC_HD static inline double maz_dist_sq(double x0, double y0, double z0,
                                       double x1, double y1, double z1) {
    double dx = x0 - x1;
    double dy = y0 - y1;
    double dz = z0 - z1;
    return dx * dx + dy * dy + dz * dz;
}

MC_HD static inline double maz_dist_xz(double x0, double z0, double x1, double z1) {
    double dx = x0 - x1;
    double dz = z0 - z1;
    return sqrt(dx * dx + dz * dz);
}

MC_HD static inline void maz_player_pos(int tick, double *px, double *py, double *pz) {
    if (tick < 16) {
        *px = 13.5; *py = 2.0; *pz = 13.5;
    } else if (tick < 46) {
        *px = 10.5; *py = 2.0; *pz = 8.5;
    } else {
        *px = 3.2; *py = 2.0; *pz = 8.5;
    }
}

MC_HD static inline void maz_face(MazZombie *z, double tx, double tz) {
    double dx = tx - z->x;
    double dz = tz - z->z;
    if (dx * dx + dz * dz < 1.0e-8) return;
    z->yaw = (float)(atan2(dz, dx) * 180.0 / MC_PI - 90.0);
}

MC_HD static inline int maz_walkable(const u16 *grid, int x, int y, int z) {
    return pf_is_walkable(grid, x, y, z, 2);
}

MC_HD static inline void maz_move_toward(MazZombie *z, const u16 *grid,
                                         double tx, double tz, double speed) {
    double dx = tx - z->x;
    double dz = tz - z->z;
    double dist = sqrt(dx * dx + dz * dz);
    if (dist < 1.0e-6) return;
    double step = speed;
    if (step > dist) step = dist;
    double nx = z->x + dx / dist * step;
    double nz = z->z + dz / dist * step;
    int bx = mc_floor(nx);
    int bz = mc_floor(nz);
    if (maz_walkable(grid, bx, 2, bz)) {
        z->x = nx;
        z->z = nz;
    }
    z->y = 2.0;
    maz_face(z, tx, tz);
}

MC_HD static inline void maz_store_path(MazZombie *z, const PfResult *res) {
    int n = res->len;
    if (n > PF_MAX_PATH) n = PF_MAX_PATH;
    z->path_len = (u32)n;
    z->path_idx = 0;
    for (int i = 0; i < n; ++i) {
        z->path_wp[i][0] = res->waypoints[i * 3 + 0];
        z->path_wp[i][1] = res->waypoints[i * 3 + 1];
        z->path_wp[i][2] = res->waypoints[i * 3 + 2];
    }
}

MC_HD static inline void maz_set_path(MazZombie *z, const u16 *grid, PfWork *work,
                                      double tx, double ty, double tz) {
    int sx = mc_floor(z->x);
    int sy = mc_floor(z->y);
    int sz = mc_floor(z->z);
    int gx = mc_floor(tx);
    int gy = mc_floor(ty);
    int gz = mc_floor(tz);
    PfResult res;
    int n = maz_pf_find_astar(grid, sx, sy, sz, gx, gy, gz, 2, MAZ_ASTAR_RANGE, work, &res);
    if (n > 0) {
        maz_store_path(z, &res);
        return;
    }
    z->path_len = 1;
    z->path_idx = 0;
    z->path_wp[0][0] = (i16)gx;
    z->path_wp[0][1] = (i16)gy;
    z->path_wp[0][2] = (i16)gz;
}

MC_HD static inline void maz_clear_path(MazZombie *z) {
    z->path_len = 0;
    z->path_idx = 0;
}

MC_HD static inline void maz_follow_path(MazZombie *z, const u16 *grid, double speed) {
    if (z->path_len == 0 || z->path_idx >= z->path_len) return;
    double tx = (double)z->path_wp[z->path_idx][0] + 0.5;
    double tz = (double)z->path_wp[z->path_idx][2] + 0.5;
    maz_move_toward(z, grid, tx, tz, speed);
    if (maz_dist_xz(z->x, z->z, tx, tz) < 0.35)
        z->path_idx++;
}

MC_HD static inline int maz_has_target(double zx, double zy, double zz,
                                       double px, double py, double pz) {
    double fr = MAZ_FOLLOW_RANGE;
    return maz_dist_sq(zx, zy, zz, px, py, pz) <= fr * fr;
}

MC_HD static inline void maz_idle_wander(i64 seed, int tick, MazZombie *z, const u16 *grid,
                                         PfWork *work) {
    if (z->wander_timer > 0) {
        z->wander_timer--;
        maz_follow_path(z, grid, MAZ_MOVE_SPEED * 0.8);
        return;
    }
    u64 h = mc_hash_seed((u64)seed, tick, mc_floor(z->x), mc_floor(z->y), mc_floor(z->z),
                           MAZ_PURPOSE_WANDER);
    int rx = mc_hash_bound(h, 7) - 3;
    int rz = mc_hash_bound(mc_hash64(h + 1ULL), 7) - 3;
    int tx = mc_floor(z->x) + rx;
    int tz = mc_floor(z->z) + rz;
    if (tx < 1) tx = 1;
    if (tx > PF_DIM_X - 2) tx = PF_DIM_X - 2;
    if (tz < 1) tz = 1;
    if (tz > PF_DIM_Z - 2) tz = PF_DIM_Z - 2;
    if (maz_walkable(grid, tx, 2, tz))
        maz_set_path(z, grid, work, (double)tx + 0.5, 2.0, (double)tz + 0.5);
    z->wander_timer = MAZ_WANDER_INTERVAL / 2;
    maz_follow_path(z, grid, MAZ_MOVE_SPEED * 0.8);
}

MC_HD static inline void maz_tick_one(i64 seed, int tick, MazZombie *z,
                                      const u16 *grid, PfWork *work,
                                      double px, double py, double pz) {
    if (z->attack_time > 0) z->attack_time--;

    int targeted = maz_has_target(z->x, z->y, z->z, px, py, pz);
    double reach = MAZ_ATTACK_REACH;
    int in_reach = targeted &&
        maz_dist_sq(z->x, z->y, z->z, px, py, pz) <= reach * reach;

    if (in_reach) {
        z->state = MAZ_STATE_ATTACK;
        maz_face(z, px, pz);
        maz_clear_path(z);
        if (z->attack_time <= 0)
            z->attack_time = MAZ_ATTACK_COOLDOWN;
        return;
    }

    if (targeted) {
        z->state = MAZ_STATE_CHASE;
        z->wander_timer = 0;
        if (z->repath_timer <= 0) {
            maz_set_path(z, grid, work, px, py, pz);
            u64 h = mc_hash_seed((u64)seed, tick, mc_floor(z->x), mc_floor(z->y), mc_floor(z->z),
                                 MAZ_PURPOSE_REPATH);
            z->repath_timer = MAZ_REPATH_INTERVAL + mc_hash_bound(h, 5);
        } else {
            z->repath_timer--;
        }
        maz_follow_path(z, grid, MAZ_MOVE_SPEED);
        return;
    }

    z->state = MAZ_STATE_IDLE;
    z->repath_timer = 0;
    maz_idle_wander(seed, tick, z, grid, work);
}

MC_HD static inline void maz_init(MazZombie *z) {
    z->x = 2.5;
    z->y = 2.0;
    z->z = 8.5;
    z->yaw = 0.0f;
    z->state = MAZ_STATE_IDLE;
    z->attack_time = 0;
    z->path_idx = 0;
    z->path_len = 0;
    z->repath_timer = 0;
    z->wander_timer = 0;
}

MC_HD static inline void maz_run(i64 seed, int nticks, MazTickOut *out, PfWork *work) {
    u16 grid[PF_VOL];
    pf_scene_flat(grid);

    MazZombie z;
    maz_init(&z);

    if (nticks > MAZ_NUM_TICKS) nticks = MAZ_NUM_TICKS;

    for (int t = 0; t < nticks; ++t) {
        double px, py, pz;
        maz_player_pos(t, &px, &py, &pz);
        maz_tick_one(seed, t, &z, grid, work, px, py, pz);
        out[t].state = z.state;
        out[t].x = z.x;
        out[t].y = z.y;
        out[t].z = z.z;
        out[t].yaw = (double)z.yaw;
        out[t].attack_time = z.attack_time;
        out[t].path_idx = z.path_idx;
    }
}

#endif /* MC_MOB_AI_ZOMBIE_ASTAR_H */
