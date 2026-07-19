/* mob_ai_zombie: EntityZombie AI state machine (idle -> chase -> attack) on a synthetic flat
 * PF grid. Internal verify: CPU==CUDA, 64 ticks, dump state+pos per tick.
 *
 * PORT TARGET: net/minecraft/entity/monster/EntityZombie.java initEntityAI task subset
 * (EntityAIZombieAttack + EntityAINearestAttackableTarget + EntityAIWanderAvoidWater collapsed
 * into three explicit states). Runtime randomness uses mc_hash_rng only (order-independent).
 *
 * READ-ONLY deps: pathfinding.h (pf_scene_flat grid helpers; stub path replaces A* until
 * pathfind-w3 is merged), block_props_table.h (via pf_is_walkable), mc_entity.h ENT_ZOMBIE schema.
 *
 * Scenario (deterministic):
 *   - Flat stone floor, zombie at (2.5, 2.0, 8.5).
 *   - Player script: ticks 0-15 far -> IDLE; 16-45 mid -> CHASE; 46-63 adjacent -> ATTACK.
 */
#ifndef MC_MOB_AI_ZOMBIE_H
#define MC_MOB_AI_ZOMBIE_H

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
    double path_tx, path_ty, path_tz;
} MazZombie;

typedef struct {
    u32    state;
    double x, y, z;
    double yaw;
    u32    attack_time;
    u32    path_idx;
} MazTickOut;

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

/* Inline stub path (straight-line goal); replaces pf_find_astar until pathfind-w3 lands. */
MC_HD static inline void maz_set_path(MazZombie *z, double tx, double ty, double tz) {
    z->path_tx = tx;
    z->path_ty = ty;
    z->path_tz = tz;
    z->path_len = 1;
    z->path_idx = 0;
}

MC_HD static inline void maz_clear_path(MazZombie *z) {
    z->path_len = 0;
    z->path_idx = 0;
}

MC_HD static inline void maz_follow_path(MazZombie *z, const u16 *grid, double speed) {
    if (z->path_len == 0) return;
    maz_move_toward(z, grid, z->path_tx, z->path_tz, speed);
    if (maz_dist_xz(z->x, z->z, z->path_tx, z->path_tz) < 0.35)
        z->path_idx = 1;
}

MC_HD static inline int maz_has_target(double zx, double zy, double zz,
                                       double px, double py, double pz) {
    double fr = MAZ_FOLLOW_RANGE;
    return maz_dist_sq(zx, zy, zz, px, py, pz) <= fr * fr;
}

MC_HD static inline void maz_idle_wander(i64 seed, int tick, MazZombie *z, const u16 *grid) {
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
        maz_set_path(z, (double)tx + 0.5, 2.0, (double)tz + 0.5);
    z->wander_timer = MAZ_WANDER_INTERVAL / 2;
    maz_follow_path(z, grid, MAZ_MOVE_SPEED * 0.8);
}

MC_HD static inline void maz_tick_one(i64 seed, int tick, MazZombie *z,
                                      const u16 *grid,
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
            maz_set_path(z, px, py, pz);
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
    maz_idle_wander(seed, tick, z, grid);
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
    z->path_tx = z->path_ty = z->path_tz = 0.0;
}

MC_HD static inline void maz_run(i64 seed, int nticks, MazTickOut *out) {
    u16 grid[PF_VOL];
    pf_scene_flat(grid);

    MazZombie z;
    maz_init(&z);

    if (nticks > MAZ_NUM_TICKS) nticks = MAZ_NUM_TICKS;

    for (int t = 0; t < nticks; ++t) {
        double px, py, pz;
        maz_player_pos(t, &px, &py, &pz);
        maz_tick_one(seed, t, &z, grid, px, py, pz);
        out[t].state = z.state;
        out[t].x = z.x;
        out[t].y = z.y;
        out[t].z = z.z;
        out[t].yaw = (double)z.yaw;
        out[t].attack_time = z.attack_time;
        out[t].path_idx = z.path_idx;
    }
}

#endif /* MC_MOB_AI_ZOMBIE_H */
