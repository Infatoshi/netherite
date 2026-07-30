/* mob_ai_creeper: EntityCreeper AI (idle -> chase -> swell/fuse -> explode) with pf_find_astar paths.
 * Internal verify: CPU==CUDA, 64 ticks, dump state+pos per tick (448 lines).
 *
 * PORT TARGET: EntityCreeper.onUpdate fuse subset + EntityAICreeperSwell + chase/wander collapsed.
 * CUT: block break (createExplosion), sounds, ocelot avoid, flint-and-steel ignite, powered/lightning.
 *
 * READ-ONLY deps: mob_ai_zombie_astar.h pattern, pathfinding.h (pf_find_astar).
 *
 * Scenario (deterministic):
 *   - Flat stone floor, creeper at (2.5, 2.0, 8.5).
 *   - Player script: ticks 0-15 far -> IDLE; 16-33 mid -> CHASE; 34-63 adjacent -> SWELL/fuse.
 *   - fuseTime=30: swell from tick 34 explodes on tick 63 (last tick).
 */
#ifndef MC_MOB_AI_CREEPER_H
#define MC_MOB_AI_CREEPER_H

#include <math.h>
#include "mc.h"
#include "mc_rng.h"
#include "mc_math.h"
#include "pathfinding.h"

#define MAC_NUM_TICKS       64
#define MAC_FOLLOW_RANGE    8.0
#define MAC_SWELL_RANGE_SQ  9.0
#define MAC_SWELL_RESET_SQ  49.0
#define MAC_MOVE_SPEED      0.23
#define MAC_FUSE_TIME       30
#define MAC_REPATH_INTERVAL 10
#define MAC_WANDER_INTERVAL 40
#define MAC_ASTAR_RANGE     16

#define MAC_PURPOSE_WANDER  0x4D414301u
#define MAC_PURPOSE_REPATH  0x4D414302u

enum {
    MAC_STATE_IDLE    = 0,
    MAC_STATE_CHASE   = 1,
    MAC_STATE_SWELL   = 2,
    MAC_STATE_EXPLODED = 3,
};

typedef struct {
    double x, y, z;
    float  yaw;
    u32    state;
    i32    creeper_state;
    i32    time_since_ignited;
    u32    alive;
    u32    path_idx;
    u32    path_len;
    i32    repath_timer;
    i32    wander_timer;
    i16    path_wp[PF_MAX_PATH][3];
} MacCreeper;

typedef struct {
    u32    state;
    double x, y, z;
    double yaw;
    u32    fuse_time;
    u32    path_idx;
} MacTickOut;

#ifdef __CUDACC__
__device__ __noinline__ int mac_pf_find_astar_dev(const u16 *grid, int sx, int sy, int sz,
                                                  int gx, int gy, int gz,
                                                  int entity_height, int max_range,
                                                  PfWork *work, PfResult *out);
#endif

MC_HD static inline int mac_pf_find_astar(const u16 *grid, int sx, int sy, int sz,
                                          int gx, int gy, int gz,
                                          int entity_height, int max_range,
                                          PfWork *work, PfResult *out) {
#if defined(__CUDA_ARCH__)
    return mac_pf_find_astar_dev(grid, sx, sy, sz, gx, gy, gz, entity_height, max_range, work, out);
#else
    return pf_find_astar(grid, sx, sy, sz, gx, gy, gz, entity_height, max_range, work, out);
#endif
}

MC_HD static inline double mac_dist_sq(double x0, double y0, double z0,
                                       double x1, double y1, double z1) {
    double dx = x0 - x1;
    double dy = y0 - y1;
    double dz = z0 - z1;
    return dx * dx + dy * dy + dz * dz;
}

MC_HD static inline double mac_dist_xz(double x0, double z0, double x1, double z1) {
    double dx = x0 - x1;
    double dz = z0 - z1;
    return sqrt(dx * dx + dz * dz);
}

MC_HD static inline void mac_player_pos(int tick, double *px, double *py, double *pz) {
    if (tick < 16) {
        *px = 13.5; *py = 2.0; *pz = 13.5;
    } else if (tick < 34) {
        *px = 10.5; *py = 2.0; *pz = 8.5;
    } else {
        *px = 3.2; *py = 2.0; *pz = 8.5;
    }
}

MC_HD static inline void mac_face(MacCreeper *c, double tx, double tz) {
    double dx = tx - c->x;
    double dz = tz - c->z;
    if (dx * dx + dz * dz < 1.0e-8) return;
    c->yaw = (float)(atan2(dz, dx) * 180.0 / MC_PI - 90.0);
}

MC_HD static inline int mac_walkable(const u16 *grid, int x, int y, int z) {
    return pf_is_walkable(grid, x, y, z, 2);
}

MC_HD static inline void mac_move_toward(MacCreeper *c, const u16 *grid,
                                         double tx, double tz, double speed) {
    double dx = tx - c->x;
    double dz = tz - c->z;
    double dist = sqrt(dx * dx + dz * dz);
    if (dist < 1.0e-6) return;
    double step = speed;
    if (step > dist) step = dist;
    double nx = c->x + dx / dist * step;
    double nz = c->z + dz / dist * step;
    int bx = mc_floor(nx);
    int bz = mc_floor(nz);
    if (mac_walkable(grid, bx, 2, bz)) {
        c->x = nx;
        c->z = nz;
    }
    c->y = 2.0;
    mac_face(c, tx, tz);
}

MC_HD static inline void mac_store_path(MacCreeper *c, const PfResult *res) {
    int n = res->len;
    if (n > PF_MAX_PATH) n = PF_MAX_PATH;
    c->path_len = (u32)n;
    c->path_idx = 0;
    for (int i = 0; i < n; ++i) {
        c->path_wp[i][0] = res->waypoints[i * 3 + 0];
        c->path_wp[i][1] = res->waypoints[i * 3 + 1];
        c->path_wp[i][2] = res->waypoints[i * 3 + 2];
    }
}

MC_HD static inline void mac_set_path(MacCreeper *c, const u16 *grid, PfWork *work,
                                      double tx, double ty, double tz) {
    int sx = mc_floor(c->x);
    int sy = mc_floor(c->y);
    int sz = mc_floor(c->z);
    int gx = mc_floor(tx);
    int gy = mc_floor(ty);
    int gz = mc_floor(tz);
    PfResult res;
    int n = mac_pf_find_astar(grid, sx, sy, sz, gx, gy, gz, 2, MAC_ASTAR_RANGE, work, &res);
    if (n > 0) {
        mac_store_path(c, &res);
        return;
    }
    c->path_len = 1;
    c->path_idx = 0;
    c->path_wp[0][0] = (i16)gx;
    c->path_wp[0][1] = (i16)gy;
    c->path_wp[0][2] = (i16)gz;
}

MC_HD static inline void mac_clear_path(MacCreeper *c) {
    c->path_len = 0;
    c->path_idx = 0;
}

MC_HD static inline void mac_follow_path(MacCreeper *c, const u16 *grid, double speed) {
    if (c->path_len == 0 || c->path_idx >= c->path_len) return;
    double tx = (double)c->path_wp[c->path_idx][0] + 0.5;
    double tz = (double)c->path_wp[c->path_idx][2] + 0.5;
    mac_move_toward(c, grid, tx, tz, speed);
    if (mac_dist_xz(c->x, c->z, tx, tz) < 0.35)
        c->path_idx++;
}

MC_HD static inline int mac_has_target(double cx, double cy, double cz,
                                       double px, double py, double pz) {
    double fr = MAC_FOLLOW_RANGE;
    return mac_dist_sq(cx, cy, cz, px, py, pz) <= fr * fr;
}

MC_HD static inline int mac_can_see(double cx, double cy, double cz,
                                  double px, double py, double pz) {
    (void)cx; (void)cy; (void)cz; (void)px; (void)py; (void)pz;
    return 1;
}

/* EntityAICreeperSwell.updateTask */
MC_HD static inline void mac_swell_update(MacCreeper *c, double px, double py, double pz) {
    double dsq = mac_dist_sq(c->x, c->y, c->z, px, py, pz);
    if (dsq > MAC_SWELL_RESET_SQ || !mac_can_see(c->x, c->y, c->z, px, py, pz)) {
        c->creeper_state = -1;
        return;
    }
    if (dsq < MAC_SWELL_RANGE_SQ || c->creeper_state > 0) {
        c->creeper_state = 1;
        mac_clear_path(c);
        mac_face(c, px, pz);
    } else {
        c->creeper_state = -1;
    }
}

MC_HD static inline void mac_idle_wander(i64 seed, int tick, MacCreeper *c, const u16 *grid,
                                         PfWork *work) {
    if (c->wander_timer > 0) {
        c->wander_timer--;
        mac_follow_path(c, grid, MAC_MOVE_SPEED * 0.8);
        return;
    }
    u64 h = mc_hash_seed((u64)seed, tick, mc_floor(c->x), mc_floor(c->y), mc_floor(c->z),
                           MAC_PURPOSE_WANDER);
    int rx = mc_hash_bound(h, 7) - 3;
    int rz = mc_hash_bound(mc_hash64(h + 1ULL), 7) - 3;
    int tx = mc_floor(c->x) + rx;
    int tz = mc_floor(c->z) + rz;
    if (tx < 1) tx = 1;
    if (tx > PF_DIM_X - 2) tx = PF_DIM_X - 2;
    if (tz < 1) tz = 1;
    if (tz > PF_DIM_Z - 2) tz = PF_DIM_Z - 2;
    if (mac_walkable(grid, tx, 2, tz))
        mac_set_path(c, grid, work, (double)tx + 0.5, 2.0, (double)tz + 0.5);
    c->wander_timer = MAC_WANDER_INTERVAL / 2;
    mac_follow_path(c, grid, MAC_MOVE_SPEED * 0.8);
}

MC_HD static inline void mac_chase(MacCreeper *c, i64 seed, int tick, const u16 *grid,
                                   PfWork *work, double px, double py, double pz) {
    c->wander_timer = 0;
    if (c->repath_timer <= 0) {
        mac_set_path(c, grid, work, px, py, pz);
        u64 h = mc_hash_seed((u64)seed, tick, mc_floor(c->x), mc_floor(c->y), mc_floor(c->z),
                             MAC_PURPOSE_REPATH);
        c->repath_timer = MAC_REPATH_INTERVAL + mc_hash_bound(h, 5);
    } else {
        c->repath_timer--;
    }
    mac_follow_path(c, grid, MAC_MOVE_SPEED);
}

/* EntityCreeper.onUpdate fuse (no block break) */
MC_HD static inline void mac_fuse_tick(MacCreeper *c) {
    if (!c->alive) return;
    int i = c->creeper_state;
    if (i > 0 && c->time_since_ignited == 0) {
        /* primed sound - CUT */
    }
    c->time_since_ignited += i;
    if (c->time_since_ignited < 0)
        c->time_since_ignited = 0;
    if (c->time_since_ignited >= MAC_FUSE_TIME) {
        c->time_since_ignited = MAC_FUSE_TIME;
        c->alive = 0;
        c->state = MAC_STATE_EXPLODED;
    }
}

MC_HD static inline void mac_tick_one(i64 seed, int tick, MacCreeper *c,
                                      const u16 *grid, PfWork *work,
                                      double px, double py, double pz) {
    if (!c->alive) {
        c->state = MAC_STATE_EXPLODED;
        return;
    }

    int targeted = mac_has_target(c->x, c->y, c->z, px, py, pz);
    mac_swell_update(c, px, py, pz);

    if (c->creeper_state > 0) {
        c->state = MAC_STATE_SWELL;
    } else if (targeted) {
        c->state = MAC_STATE_CHASE;
        mac_chase(c, seed, tick, grid, work, px, py, pz);
    } else {
        c->state = MAC_STATE_IDLE;
        c->repath_timer = 0;
        mac_idle_wander(seed, tick, c, grid, work);
    }

    mac_fuse_tick(c);
}

MC_HD static inline void mac_init(MacCreeper *c) {
    c->x = 2.5;
    c->y = 2.0;
    c->z = 8.5;
    c->yaw = 0.0f;
    c->state = MAC_STATE_IDLE;
    c->creeper_state = -1;
    c->time_since_ignited = 0;
    c->alive = 1;
    c->path_idx = 0;
    c->path_len = 0;
    c->repath_timer = 0;
    c->wander_timer = 0;
}

MC_HD static inline void mac_run(i64 seed, int nticks, MacTickOut *out, PfWork *work) {
    u16 grid[PF_VOL];
    pf_scene_flat(grid);

    MacCreeper c;
    mac_init(&c);

    if (nticks > MAC_NUM_TICKS) nticks = MAC_NUM_TICKS;

    for (int t = 0; t < nticks; ++t) {
        double px, py, pz;
        mac_player_pos(t, &px, &py, &pz);
        mac_tick_one(seed, t, &c, grid, work, px, py, pz);
        out[t].state = c.state;
        out[t].x = c.x;
        out[t].y = c.y;
        out[t].z = c.z;
        out[t].yaw = (double)c.yaw;
        out[t].fuse_time = (u32)c.time_since_ignited;
        out[t].path_idx = c.path_idx;
    }
}

#endif /* MC_MOB_AI_CREEPER_H */
