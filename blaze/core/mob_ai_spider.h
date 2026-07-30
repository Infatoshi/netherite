/* mob_ai_spider: EntitySpider AI (idle -> chase -> attack) with A* paths + leap/climb subset.
 * Internal verify: CPU==CUDA, 64 ticks, dump state+pos+climb/leap per tick (640 lines).
 *
 * Cloned from mob_ai_zombie_astar.h; adds EntityAILeapAtTarget (motionY=0.4F, distSq 4..16,
 * hash RNG 1/5 on ground) and PathNavigateClimber / isBesideClimbableBlock (horizontal collision
 * -> climb motionY=0.2, direct move toward target Y when path empty). CUT: brightness gate
 * (AISpiderTarget), skeleton jockey, potion effects, water/day damage.
 * Hash RNG keyed (seed,tick,x,y,z,purpose) per SPEC rule 1.
 * READ-ONLY deps: mob_ai_zombie_astar.h (reference), pathfinding.h (pf_find_astar).
 *
 * Scenario (deterministic):
 *   - Flat stone + wall at x=6 (y=2..5), spider at (2.5, 2.0, 8.5).
 *   - Player: 0-15 far -> IDLE; 16-40 elevated behind wall -> CHASE+climb+leap;
 *     41-63 adjacent -> ATTACK (leap window when dist 2..4).
 */
#ifndef MC_MOB_AI_SPIDER_H
#define MC_MOB_AI_SPIDER_H

#include <math.h>
#include "mc.h"
#include "mc_rng.h"
#include "mc_math.h"
#include "pathfinding.h"

#define MAS_NUM_TICKS         64
#define MAS_FOLLOW_RANGE      16.0
#define MAS_PLAYER_WIDTH      0.6
#define MAS_ATTACK_REACH      (4.0 + MAS_PLAYER_WIDTH)
#define MAS_ATTACK_REACH_SQ   (MAS_ATTACK_REACH * MAS_ATTACK_REACH)
#define MAS_MOVE_SPEED        0.30000001192092896
#define MAS_ATTACK_COOLDOWN   20
#define MAS_REPATH_INTERVAL   10
#define MAS_WANDER_INTERVAL   40
#define MAS_ASTAR_RANGE       16
#define MAS_ENTITY_HEIGHT     2
#define MAS_CLIMB_STEP        0.2
#define MAS_LEAP_MOTION_Y     0.4
#define MAS_GRAVITY           0.08

#define MAS_PURPOSE_WANDER    0x4D415301u
#define MAS_PURPOSE_REPATH    0x4D415302u
#define MAS_PURPOSE_LEAP      0x4D415303u

enum {
    MAS_STATE_IDLE   = 0,
    MAS_STATE_CHASE  = 1,
    MAS_STATE_ATTACK = 2,
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
    u32    on_ground;
    u32    climbing;
    u32    leap_ticks;
    double motion_x;
    double motion_y;
    double motion_z;
    i16    path_wp[PF_MAX_PATH][3];
} MasSpider;

typedef struct {
    u32    state;
    double x, y, z;
    double yaw;
    u32    attack_time;
    u32    path_idx;
    u32    climbing;
    u32    on_ground;
    u32    did_leap;
} MasTickOut;

#ifdef __CUDACC__
__device__ __noinline__ int mas_pf_find_astar_dev(const u16 *grid, int sx, int sy, int sz,
                                                  int gx, int gy, int gz,
                                                  int entity_height, int max_range,
                                                  PfWork *work, PfResult *out);
#endif

MC_HD static inline int mas_pf_find_astar(const u16 *grid, int sx, int sy, int sz,
                                          int gx, int gy, int gz,
                                          int entity_height, int max_range,
                                          PfWork *work, PfResult *out) {
#if defined(__CUDA_ARCH__)
    return mas_pf_find_astar_dev(grid, sx, sy, sz, gx, gy, gz, entity_height, max_range, work, out);
#else
    return pf_find_astar(grid, sx, sy, sz, gx, gy, gz, entity_height, max_range, work, out);
#endif
}

MC_HD static inline double mas_dist_sq(double x0, double y0, double z0,
                                       double x1, double y1, double z1) {
    double dx = x0 - x1;
    double dy = y0 - y1;
    double dz = z0 - z1;
    return dx * dx + dy * dy + dz * dz;
}

MC_HD static inline double mas_dist_xz(double x0, double z0, double x1, double z1) {
    double dx = x0 - x1;
    double dz = z0 - z1;
    return sqrt(dx * dx + dz * dz);
}

MC_HD static inline void mas_player_pos(int tick, double *px, double *py, double *pz) {
    if (tick < 16) {
        *px = 13.5; *py = 2.0; *pz = 13.5;
    } else if (tick < 41) {
        *px = 10.5; *py = 5.0; *pz = 8.5;
    } else {
        *px = 3.2; *py = 2.0; *pz = 8.5;
    }
}

MC_HD static inline void mas_face(MasSpider *s, double tx, double tz) {
    double dx = tx - s->x;
    double dz = tz - s->z;
    if (dx * dx + dz * dz < 1.0e-8) return;
    s->yaw = (float)(atan2(dz, dx) * 180.0 / MC_PI - 90.0);
}

MC_HD static inline int mas_walkable(const u16 *grid, int x, int y, int z) {
    return pf_is_walkable(grid, x, y, z, MAS_ENTITY_HEIGHT);
}

MC_HD static inline int mas_body_blocked(const u16 *grid, int bx, int by, int bz) {
    int dy;
    for (dy = 0; dy < MAS_ENTITY_HEIGHT; ++dy) {
        if (pf_is_solid(pf_get(grid, bx, by + dy, bz))) return 1;
    }
    return 0;
}

MC_HD static inline int mas_collided_h(const u16 *grid, double x, double y, double z,
                                      double nx, double nz) {
    int bx = mc_floor(nx);
    int bz = mc_floor(nz);
    int by = mc_floor(y);
    if (!pf_in(bx, by, bz)) return 0;
    return mas_body_blocked(grid, bx, by, bz);
}

MC_HD static inline void mas_scene_spider(u16 *grid) {
    u16 stone = mc_state(BLK_STONE, 0);
    pf_scene_flat(grid);
    pf_fill_box(grid, 6, 2, 6, 6, 5, 10, stone);
}

MC_HD static inline void mas_move_toward(MasSpider *s, const u16 *grid,
                                         double tx, double tz, double speed) {
    double dx = tx - s->x;
    double dz = tz - s->z;
    double dist = sqrt(dx * dx + dz * dz);
    double step, nx, nz;
    int bx, bz;
    if (dist < 1.0e-6) return;
    step = speed;
    if (step > dist) step = dist;
    nx = s->x + dx / dist * step;
    nz = s->z + dz / dist * step;
    if (mas_collided_h(grid, s->x, s->y, s->z, nx, s->z) ||
        mas_collided_h(grid, s->x, s->y, s->z, s->x, nz) ||
        mas_collided_h(grid, s->x, s->y, s->z, nx, nz)) {
        s->climbing = 1;
    } else {
        bx = mc_floor(nx);
        bz = mc_floor(nz);
        if (mas_walkable(grid, bx, mc_floor(s->y), bz)) {
            s->x = nx;
            s->z = nz;
        } else {
            s->climbing = 1;
        }
    }
    mas_face(s, tx, tz);
}

MC_HD static inline void mas_climb_toward(MasSpider *s, const u16 *grid,
                                          double tx, double ty, double tz, double speed) {
    mas_move_toward(s, grid, tx, tz, speed);
    if (s->climbing || ty > s->y + 0.25) {
        if (s->climbing)
            s->y += MAS_CLIMB_STEP;
        else if (ty > s->y)
            s->y += (ty - s->y < speed * 0.5 ? ty - s->y : speed * 0.5);
        s->on_ground = 0;
    }
    if (s->climbing && mas_collided_h(grid, s->x, s->y, s->z, s->x, s->z))
        s->y += MAS_CLIMB_STEP;
    s->climbing = mas_collided_h(grid, s->x, s->y, s->z,
                                 s->x + 0.3, s->z) ||
                  mas_collided_h(grid, s->x, s->y, s->z,
                                 s->x - 0.3, s->z) ||
                  mas_collided_h(grid, s->x, s->y, s->z,
                                 s->x, s->z + 0.3) ||
                  mas_collided_h(grid, s->x, s->y, s->z,
                                 s->x, s->z - 0.3);
}

MC_HD static inline void mas_store_path(MasSpider *s, const PfResult *res) {
    int n = res->len;
    int i;
    if (n > PF_MAX_PATH) n = PF_MAX_PATH;
    s->path_len = (u32)n;
    s->path_idx = 0;
    for (i = 0; i < n; ++i) {
        s->path_wp[i][0] = res->waypoints[i * 3 + 0];
        s->path_wp[i][1] = res->waypoints[i * 3 + 1];
        s->path_wp[i][2] = res->waypoints[i * 3 + 2];
    }
}

MC_HD static inline void mas_set_path(MasSpider *s, const u16 *grid, PfWork *work,
                                      double tx, double ty, double tz) {
    int sx = mc_floor(s->x);
    int sy = mc_floor(s->y);
    int sz = mc_floor(s->z);
    int gx = mc_floor(tx);
    int gy = mc_floor(ty);
    int gz = mc_floor(tz);
    PfResult res;
    int n = mas_pf_find_astar(grid, sx, sy, sz, gx, gy, gz, MAS_ENTITY_HEIGHT,
                              MAS_ASTAR_RANGE, work, &res);
    if (n > 0) {
        mas_store_path(s, &res);
        return;
    }
    s->path_len = 0;
    s->path_idx = 0;
}

MC_HD static inline void mas_clear_path(MasSpider *s) {
    s->path_len = 0;
    s->path_idx = 0;
}

MC_HD static inline void mas_follow_path(MasSpider *s, const u16 *grid, double speed) {
    double tx, ty, tz;
    int wy;
    if (s->path_len == 0 || s->path_idx >= s->path_len) return;
    tx = (double)s->path_wp[s->path_idx][0] + 0.5;
    ty = (double)s->path_wp[s->path_idx][1];
    tz = (double)s->path_wp[s->path_idx][2] + 0.5;
    wy = s->path_wp[s->path_idx][1];
    if (wy > mc_floor(s->y))
        mas_climb_toward(s, grid, tx, ty + 0.5, tz, speed);
    else
        mas_move_toward(s, grid, tx, tz, speed);
    if (mas_dist_xz(s->x, s->z, tx, tz) < 0.35 &&
        mc_floor(s->y) >= wy - 1)
        s->path_idx++;
}

MC_HD static inline int mas_has_target(double sx, double sy, double sz,
                                       double px, double py, double pz) {
    double fr = MAS_FOLLOW_RANGE;
    (void)sy; (void)py;
    return mas_dist_sq(sx, 2.0, sz, px, 2.0, pz) <= fr * fr;
}

MC_HD static inline int mas_try_leap(i64 seed, int tick, MasSpider *s,
                                   double px, double py, double pz) {
    double dsq, d0, d1, f;
    u64 h;
    (void)py;
    if (!s->on_ground || s->leap_ticks > 0) return 0;
    dsq = mas_dist_sq(s->x, s->y, s->z, px, py, pz);
    if (dsq < 4.0 || dsq > 16.0) return 0;
    h = mc_hash_seed((u64)seed, tick, mc_floor(s->x), mc_floor(s->y), mc_floor(s->z),
                     MAS_PURPOSE_LEAP);
    if (mc_hash_bound(h, 5) != 0) return 0;
    d0 = px - s->x;
    d1 = pz - s->z;
    f = sqrt(d0 * d0 + d1 * d1);
    if (f >= 1.0e-4) {
        s->motion_x += d0 / (double)f * 0.5 * 0.800000011920929 + s->motion_x * 0.20000000298023224;
        s->motion_z += d1 / (double)f * 0.5 * 0.800000011920929 + s->motion_z * 0.20000000298023224;
    }
    s->motion_y = MAS_LEAP_MOTION_Y;
    s->on_ground = 0;
    s->leap_ticks = 1;
    return 1;
}

MC_HD static inline void mas_apply_leap(MasSpider *s, const u16 *grid) {
    int by;
    if (s->leap_ticks == 0) return;
    s->x += s->motion_x;
    s->y += s->motion_y;
    s->z += s->motion_z;
    s->motion_y -= MAS_GRAVITY;
    s->motion_x *= 0.91;
    s->motion_z *= 0.91;
    by = mc_floor(s->y);
    if (by <= 2 && s->motion_y <= 0.0) {
        s->y = 2.0;
        s->motion_x = 0.0;
        s->motion_y = 0.0;
        s->motion_z = 0.0;
        s->on_ground = 1;
        s->leap_ticks = 0;
    } else if (mas_body_blocked(grid, mc_floor(s->x), by, mc_floor(s->z))) {
        s->climbing = 1;
        s->leap_ticks = 0;
        s->motion_x = 0.0;
        s->motion_y = 0.0;
        s->motion_z = 0.0;
        s->on_ground = 0;
    } else {
        s->on_ground = 0;
    }
}

MC_HD static inline void mas_idle_wander(i64 seed, int tick, MasSpider *s, const u16 *grid,
                                         PfWork *work) {
    u64 h;
    int rx, rz, tx, tz;
    if (s->wander_timer > 0) {
        s->wander_timer--;
        mas_follow_path(s, grid, MAS_MOVE_SPEED * 0.8);
        return;
    }
    h = mc_hash_seed((u64)seed, tick, mc_floor(s->x), mc_floor(s->y), mc_floor(s->z),
                     MAS_PURPOSE_WANDER);
    rx = mc_hash_bound(h, 7) - 3;
    rz = mc_hash_bound(mc_hash64(h + 1ULL), 7) - 3;
    tx = mc_floor(s->x) + rx;
    tz = mc_floor(s->z) + rz;
    if (tx < 1) tx = 1;
    if (tx > PF_DIM_X - 2) tx = PF_DIM_X - 2;
    if (tz < 1) tz = 1;
    if (tz > PF_DIM_Z - 2) tz = PF_DIM_Z - 2;
    if (mas_walkable(grid, tx, 2, tz))
        mas_set_path(s, grid, work, (double)tx + 0.5, 2.0, (double)tz + 0.5);
    s->wander_timer = MAS_WANDER_INTERVAL / 2;
    mas_follow_path(s, grid, MAS_MOVE_SPEED * 0.8);
}

MC_HD static inline void mas_tick_one(i64 seed, int tick, MasSpider *s,
                                      const u16 *grid, PfWork *work,
                                      double px, double py, double pz,
                                      u32 *did_leap) {
    u64 h;
    int targeted, in_reach;
    *did_leap = 0;

    if (s->attack_time > 0) s->attack_time--;

    if (s->leap_ticks > 0) {
        mas_apply_leap(s, grid);
        s->state = MAS_STATE_CHASE;
        mas_face(s, px, pz);
        return;
    }

    targeted = mas_has_target(s->x, s->y, s->z, px, py, pz);
    in_reach = targeted &&
        mas_dist_sq(s->x, s->y, s->z, px, py, pz) <= MAS_ATTACK_REACH_SQ;

    if (in_reach) {
        s->state = MAS_STATE_ATTACK;
        mas_face(s, px, pz);
        mas_clear_path(s);
        s->climbing = 0;
        if (s->attack_time <= 0)
            s->attack_time = MAS_ATTACK_COOLDOWN;
        return;
    }

    if (targeted) {
        s->state = MAS_STATE_CHASE;
        s->wander_timer = 0;
        if (mas_try_leap(seed, tick, s, px, py, pz)) {
            *did_leap = 1;
            mas_clear_path(s);
            return;
        }
        if (s->repath_timer <= 0) {
            mas_set_path(s, grid, work, px, py, pz);
            h = mc_hash_seed((u64)seed, tick, mc_floor(s->x), mc_floor(s->y), mc_floor(s->z),
                             MAS_PURPOSE_REPATH);
            s->repath_timer = MAS_REPATH_INTERVAL + mc_hash_bound(h, 5);
        } else {
            s->repath_timer--;
        }
        if (s->path_len > 0)
            mas_follow_path(s, grid, MAS_MOVE_SPEED);
        else
            mas_climb_toward(s, grid, px, py, pz, MAS_MOVE_SPEED);
        return;
    }

    s->state = MAS_STATE_IDLE;
    s->repath_timer = 0;
    s->climbing = 0;
    mas_idle_wander(seed, tick, s, grid, work);
}

MC_HD static inline void mas_init(MasSpider *s) {
    s->x = 2.5;
    s->y = 2.0;
    s->z = 8.5;
    s->yaw = 0.0f;
    s->state = MAS_STATE_IDLE;
    s->attack_time = 0;
    s->path_idx = 0;
    s->path_len = 0;
    s->repath_timer = 0;
    s->wander_timer = 0;
    s->on_ground = 1;
    s->climbing = 0;
    s->leap_ticks = 0;
    s->motion_x = 0.0;
    s->motion_y = 0.0;
    s->motion_z = 0.0;
}

MC_HD static inline void mas_run(i64 seed, int nticks, MasTickOut *out, PfWork *work) {
    u16 grid[PF_VOL];
    MasSpider s;
    int t;
    u32 did_leap;

    mas_scene_spider(grid);
    mas_init(&s);

    if (nticks > MAS_NUM_TICKS) nticks = MAS_NUM_TICKS;

    for (t = 0; t < nticks; ++t) {
        double px, py, pz;
        mas_player_pos(t, &px, &py, &pz);
        mas_tick_one(seed, t, &s, grid, work, px, py, pz, &did_leap);
        out[t].state = s.state;
        out[t].x = s.x;
        out[t].y = s.y;
        out[t].z = s.z;
        out[t].yaw = (double)s.yaw;
        out[t].attack_time = s.attack_time;
        out[t].path_idx = s.path_idx;
        out[t].climbing = s.climbing;
        out[t].on_ground = s.on_ground;
        out[t].did_leap = did_leap;
    }
}

#endif /* MC_MOB_AI_SPIDER_H */
