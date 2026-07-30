/* mob_ai_skeleton: EntitySkeleton AI (idle -> chase -> ranged bow) with real pf_find_astar paths.
 * Internal verify: CPU==CUDA, 64 ticks, dump state+pos per tick (448 lines).
 *
 * Same harness/scenario as mob_ai_zombie_astar.h; replaces melee attack with
 * EntityAIAttackRangedBow subset (bow draw 20 ticks, cooldown 20, range 15).
 *
 * READ-ONLY deps: mob_ai_zombie_astar.h (harness pattern reference), combat_math.h
 * (arrow hit damage on synthetic no-armor player). pathfinding.h (pf_find_astar).
 *
 * Scenario (deterministic):
 *   - Flat stone floor, skeleton at (2.5, 2.0, 8.5).
 *   - Player script: ticks 0-15 far -> IDLE; 16-45 mid -> CHASE then RANGED;
 *     46-63 close -> RANGED strafe-back + bow fire.
 */
#ifndef MC_MOB_AI_SKELETON_H
#define MC_MOB_AI_SKELETON_H

#include <math.h>
#include "mc.h"
#include "mc_rng.h"
#include "mc_math.h"
#include "pathfinding.h"
#include "combat_math.h"

#define MAS_NUM_TICKS           64
#define MAS_FOLLOW_RANGE        8.0
#define MAS_MAX_ATTACK_DIST_SQ  (15.0 * 15.0)
#define MAS_MOVE_SPEED          0.115
#define MAS_ATTACK_COOLDOWN     20
#define MAS_BOW_DRAW_TICKS      20
#define MAS_REPATH_INTERVAL     10
#define MAS_WANDER_INTERVAL     40
#define MAS_ASTAR_RANGE         16
#define MAS_PLAYER_HP           20.0f

#define MAS_PURPOSE_WANDER      0x4D415301u
#define MAS_PURPOSE_REPATH      0x4D415302u
#define MAS_PURPOSE_STRAFE      0x4D415303u
#define MAS_PURPOSE_STRAFE2     0x4D415304u
#define MAS_PURPOSE_SHOOT       0x4D415305u

enum {
    MAS_STATE_IDLE   = 0,
    MAS_STATE_CHASE  = 1,
    MAS_STATE_RANGED = 2,
};

typedef struct {
    double x, y, z;
    float  yaw;
    u32    state;
    i32    attack_time;
    u32    path_idx;
    u32    path_len;
    i32    repath_timer;
    i32    wander_timer;
    i32    see_time;
    i32    strafing_time;
    int    strafing_clockwise;
    int    strafing_backwards;
    int    hand_active;
    i32    bow_draw;
    float  player_hp;
    i16    path_wp[PF_MAX_PATH][3];
} MasSkeleton;

typedef struct {
    u32    state;
    double x, y, z;
    double yaw;
    u32    attack_time;
    u32    path_idx;
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
    } else if (tick < 46) {
        *px = 10.5; *py = 2.0; *pz = 8.5;
    } else {
        *px = 3.2; *py = 2.0; *pz = 8.5;
    }
}

MC_HD static inline void mas_face(MasSkeleton *s, double tx, double tz) {
    double dx = tx - s->x;
    double dz = tz - s->z;
    if (dx * dx + dz * dz < 1.0e-8) return;
    s->yaw = (float)(atan2(dz, dx) * 180.0 / MC_PI - 90.0);
}

MC_HD static inline int mas_walkable(const u16 *grid, int x, int y, int z) {
    return pf_is_walkable(grid, x, y, z, 2);
}

MC_HD static inline void mas_move_toward(MasSkeleton *s, const u16 *grid,
                                         double tx, double tz, double speed) {
    double dx = tx - s->x;
    double dz = tz - s->z;
    double dist = sqrt(dx * dx + dz * dz);
    if (dist < 1.0e-6) return;
    double step = speed;
    if (step > dist) step = dist;
    double nx = s->x + dx / dist * step;
    double nz = s->z + dz / dist * step;
    int bx = mc_floor(nx);
    int bz = mc_floor(nz);
    if (mas_walkable(grid, bx, 2, bz)) {
        s->x = nx;
        s->z = nz;
    }
    s->y = 2.0;
    mas_face(s, tx, tz);
}

MC_HD static inline void mas_store_path(MasSkeleton *s, const PfResult *res) {
    int n = res->len;
    if (n > PF_MAX_PATH) n = PF_MAX_PATH;
    s->path_len = (u32)n;
    s->path_idx = 0;
    for (int i = 0; i < n; ++i) {
        s->path_wp[i][0] = res->waypoints[i * 3 + 0];
        s->path_wp[i][1] = res->waypoints[i * 3 + 1];
        s->path_wp[i][2] = res->waypoints[i * 3 + 2];
    }
}

MC_HD static inline void mas_set_path(MasSkeleton *s, const u16 *grid, PfWork *work,
                                      double tx, double ty, double tz) {
    int sx = mc_floor(s->x);
    int sy = mc_floor(s->y);
    int sz = mc_floor(s->z);
    int gx = mc_floor(tx);
    int gy = mc_floor(ty);
    int gz = mc_floor(tz);
    PfResult res;
    int n = mas_pf_find_astar(grid, sx, sy, sz, gx, gy, gz, 2, MAS_ASTAR_RANGE, work, &res);
    if (n > 0) {
        mas_store_path(s, &res);
        return;
    }
    s->path_len = 1;
    s->path_idx = 0;
    s->path_wp[0][0] = (i16)gx;
    s->path_wp[0][1] = (i16)gy;
    s->path_wp[0][2] = (i16)gz;
}

MC_HD static inline void mas_clear_path(MasSkeleton *s) {
    s->path_len = 0;
    s->path_idx = 0;
}

MC_HD static inline void mas_follow_path(MasSkeleton *s, const u16 *grid, double speed) {
    if (s->path_len == 0 || s->path_idx >= s->path_len) return;
    double tx = (double)s->path_wp[s->path_idx][0] + 0.5;
    double tz = (double)s->path_wp[s->path_idx][2] + 0.5;
    mas_move_toward(s, grid, tx, tz, speed);
    if (mas_dist_xz(s->x, s->z, tx, tz) < 0.35)
        s->path_idx++;
}

MC_HD static inline int mas_has_target(double sx, double sy, double sz,
                                       double px, double py, double pz) {
    double fr = MAS_FOLLOW_RANGE;
    return mas_dist_sq(sx, sy, sz, px, py, pz) <= fr * fr;
}

MC_HD static inline void mas_idle_wander(i64 seed, int tick, MasSkeleton *s, const u16 *grid,
                                         PfWork *work) {
    if (s->wander_timer > 0) {
        s->wander_timer--;
        mas_follow_path(s, grid, MAS_MOVE_SPEED * 0.8);
        return;
    }
    u64 h = mc_hash_seed((u64)seed, tick, mc_floor(s->x), mc_floor(s->y), mc_floor(s->z),
                           MAS_PURPOSE_WANDER);
    int rx = mc_hash_bound(h, 7) - 3;
    int rz = mc_hash_bound(mc_hash64(h + 1ULL), 7) - 3;
    int tx = mc_floor(s->x) + rx;
    int tz = mc_floor(s->z) + rz;
    if (tx < 1) tx = 1;
    if (tx > PF_DIM_X - 2) tx = PF_DIM_X - 2;
    if (tz < 1) tz = 1;
    if (tz > PF_DIM_Z - 2) tz = PF_DIM_Z - 2;
    if (mas_walkable(grid, tx, 2, tz))
        mas_set_path(s, grid, work, (double)tx + 0.5, 2.0, (double)tz + 0.5);
    s->wander_timer = MAS_WANDER_INTERVAL / 2;
    mas_follow_path(s, grid, MAS_MOVE_SPEED * 0.8);
}

MC_HD static inline void mas_strafe(MasSkeleton *s, const u16 *grid, double d0) {
    if (d0 > MAS_MAX_ATTACK_DIST_SQ * 0.75)
        s->strafing_backwards = 0;
    else if (d0 < MAS_MAX_ATTACK_DIST_SQ * 0.25)
        s->strafing_backwards = 1;

    double yaw_rad = (double)s->yaw * MC_PI / 180.0 + MC_PI / 2.0;
    double fwd_x = cos(yaw_rad);
    double fwd_z = sin(yaw_rad);
    double right_x = -fwd_z;
    double right_z = fwd_x;
    double mx = (s->strafing_backwards ? -0.5 : 0.5) * fwd_x;
    double mz = (s->strafing_backwards ? -0.5 : 0.5) * fwd_z;
    if (s->strafing_clockwise) {
        mx += 0.5 * right_x;
        mz += 0.5 * right_z;
    } else {
        mx -= 0.5 * right_x;
        mz -= 0.5 * right_z;
    }
    mas_move_toward(s, grid, s->x + mx, s->z + mz, MAS_MOVE_SPEED * 0.5);
}

MC_HD static inline void mas_shoot_arrow(i64 seed, int tick, MasSkeleton *s) {
    float velocity = 1.0f;
    u64 h = mc_hash_seed((u64)seed, tick, mc_floor(s->x), mc_floor(s->y), mc_floor(s->z),
                         MAS_PURPOSE_SHOOT);
    float gauss = (mc_hash_f01(h) * 2.0f - 1.0f) * 0.25f;
    float raw = velocity * 2.0f + gauss + 3.0f * 0.11f;
    McCombatArmor armor = mc_combat_armor_set(0);
    float dmg = mc_combat_final_damage(raw, &armor);
    s->player_hp -= dmg;
    if (s->player_hp < 0.0f) s->player_hp = 0.0f;
}

MC_HD static inline u32 mas_emit_attack_time(const MasSkeleton *s) {
    if (s->hand_active)
        return (u32)s->bow_draw;
    if (s->attack_time < 0)
        return 0u;
    return (u32)s->attack_time;
}

MC_HD static inline void mas_ranged_tick(i64 seed, int tick, MasSkeleton *s,
                                         const u16 *grid, double px, double pz, double d0) {
    s->state = MAS_STATE_RANGED;
    s->wander_timer = 0;
    mas_clear_path(s);
    mas_face(s, px, pz);

    s->strafing_time++;
    if (s->strafing_time >= 20) {
        u64 h = mc_hash_seed((u64)seed, tick, mc_floor(s->x), mc_floor(s->y), mc_floor(s->z),
                             MAS_PURPOSE_STRAFE);
        if ((double)mc_hash_f01(h) < 0.3)
            s->strafing_clockwise = !s->strafing_clockwise;
        h = mc_hash_seed((u64)seed, tick, mc_floor(s->x), mc_floor(s->y), mc_floor(s->z),
                         MAS_PURPOSE_STRAFE2);
        if ((double)mc_hash_f01(h) < 0.3)
            s->strafing_backwards = !s->strafing_backwards;
        s->strafing_time = 0;
    }

    if (s->strafing_time > -1)
        mas_strafe(s, grid, d0);

    if (s->hand_active) {
        s->bow_draw++;
        if (s->bow_draw >= MAS_BOW_DRAW_TICKS) {
            s->hand_active = 0;
            s->bow_draw = 0;
            mas_shoot_arrow(seed, tick, s);
            s->attack_time = MAS_ATTACK_COOLDOWN;
        }
    } else {
        s->attack_time--;
        if (s->attack_time <= 0 && s->see_time >= -60) {
            s->hand_active = 1;
            s->bow_draw = 0;
        }
    }
}

MC_HD static inline void mas_tick_one(i64 seed, int tick, MasSkeleton *s,
                                      const u16 *grid, PfWork *work,
                                      double px, double py, double pz) {
    int targeted = mas_has_target(s->x, s->y, s->z, px, py, pz);
    double d0 = mas_dist_sq(s->x, s->y, s->z, px, py, pz);

    if (targeted) {
        if (s->see_time < 100) s->see_time++;
    } else if (s->see_time > -100) {
        s->see_time--;
    }

    if (targeted && d0 <= MAS_MAX_ATTACK_DIST_SQ && s->see_time >= 20) {
        mas_ranged_tick(seed, tick, s, grid, px, pz, d0);
        return;
    }

    s->hand_active = 0;
    s->bow_draw = 0;
    s->strafing_time = -1;

    if (targeted) {
        s->state = MAS_STATE_CHASE;
        s->wander_timer = 0;
        if (s->repath_timer <= 0) {
            mas_set_path(s, grid, work, px, py, pz);
            u64 h = mc_hash_seed((u64)seed, tick, mc_floor(s->x), mc_floor(s->y), mc_floor(s->z),
                                 MAS_PURPOSE_REPATH);
            s->repath_timer = MAS_REPATH_INTERVAL + mc_hash_bound(h, 5);
        } else {
            s->repath_timer--;
        }
        mas_follow_path(s, grid, MAS_MOVE_SPEED);
        return;
    }

    s->state = MAS_STATE_IDLE;
    s->repath_timer = 0;
    s->attack_time = -1;
    mas_idle_wander(seed, tick, s, grid, work);
}

MC_HD static inline void mas_init(MasSkeleton *s) {
    s->x = 2.5;
    s->y = 2.0;
    s->z = 8.5;
    s->yaw = 0.0f;
    s->state = MAS_STATE_IDLE;
    s->attack_time = -1;
    s->path_idx = 0;
    s->path_len = 0;
    s->repath_timer = 0;
    s->wander_timer = 0;
    s->see_time = 0;
    s->strafing_time = -1;
    s->strafing_clockwise = 0;
    s->strafing_backwards = 0;
    s->hand_active = 0;
    s->bow_draw = 0;
    s->player_hp = MAS_PLAYER_HP;
}

MC_HD static inline void mas_run(i64 seed, int nticks, MasTickOut *out, PfWork *work) {
    u16 grid[PF_VOL];
    pf_scene_flat(grid);

    MasSkeleton sk;
    mas_init(&sk);

    if (nticks > MAS_NUM_TICKS) nticks = MAS_NUM_TICKS;

    for (int t = 0; t < nticks; ++t) {
        double px, py, pz;
        mas_player_pos(t, &px, &py, &pz);
        mas_tick_one(seed, t, &sk, grid, work, px, py, pz);
        out[t].state = sk.state;
        out[t].x = sk.x;
        out[t].y = sk.y;
        out[t].z = sk.z;
        out[t].yaw = (double)sk.yaw;
        out[t].attack_time = mas_emit_attack_time(&sk);
        out[t].path_idx = sk.path_idx;
    }
}

#endif /* MC_MOB_AI_SKELETON_H */
