/* mob_spawning: hostile spawn attempt cycle (WorldEntitySpawner.findChunksForSpawning subset).
 *
 * INTERNAL verify (CPU==CUDA). Synthetic 16x16x48 flat chunk: bedrock/stone/grass floor, seed-varied
 * dark ceiling pockets + torches. Runtime RNG = mc_hash_rng keyed (tick,x,y,z,purpose) per SPEC rule 1.
 * Read-only deps: block_props_table.h (block id, solidity, light emit/opacity), mc_rng.h.
 *
 * CUT (output-invariant for this harness): Forge events, entity construction/collision, passive/water
 * creature types, chunk shuffle order (single flat chunk), PlayerChunkMap/world border, biome spawn lists
 * (fixed overworld monster table). Light = height-map skylight + torch block-light (not full propagation).
 *
 * Traps: ordered temporaries; no a[i]=i++; -ffp-contract=off/--fmad=false. */
#ifndef MC_MOB_SPAWNING_H
#define MC_MOB_SPAWNING_H

#include "mc.h"
#include "mc_world.h"
#include "mc_blocks.h"
#include "mc_rng.h"
#include "block_props_table.h"

#define MS_NX 16
#define MS_NY 48
#define MS_NZ 16
#define MS_VOL (MS_NX * MS_NY * MS_NZ)

#define MS_MOB_COUNT_DIV 289
#define MS_MONSTER_CAP   70
#define MS_MAX_DECISIONS 512

#define MS_FLOOR_Y 4
#define MS_PLAYER_X 8.5f
#define MS_PLAYER_Y 5.0f
#define MS_PLAYER_Z 8.5f
#define MS_WORLD_SPAWN_X 0
#define MS_WORLD_SPAWN_Y 200
#define MS_WORLD_SPAWN_Z 0

enum {
    MS_PURPOSE_BASE  = 1,
    MS_PURPOSE_GROUP = 2,
    MS_PURPOSE_WALK  = 3,
    MS_PURPOSE_LIGHT = 4,
    MS_PURPOSE_MOB   = 5,
};

enum {
    MS_RES_SPAWN          = 0,
    MS_RES_FAIL_BLOCK     = 1,
    MS_RES_FAIL_LIGHT_SKY = 2,
    MS_RES_FAIL_LIGHT_BLK = 3,
    MS_RES_FAIL_PLAYER    = 4,
    MS_RES_FAIL_SPAWN_PT  = 5,
    MS_RES_FAIL_INIT_SOLID = 6,
};

#define MS_MONSTER_N 5
#define MS_MONSTER_TOTAL_WEIGHT 410

typedef struct {
    u16 blocks[MS_VOL];
    u8  sky[MS_VOL];
    u8  blk[MS_VOL];
    u64 seed;
    i64 tick;
    int n_decisions;
    u64 decisions[MS_MAX_DECISIONS];
} MsScene;

MC_HD static inline int ms_idx(int x, int y, int z) {
    return (y * MS_NZ + z) * MS_NX + x;
}

MC_HD static inline int ms_in(int x, int y, int z) {
    return x >= 0 && x < MS_NX && y >= 0 && y < MS_NY && z >= 0 && z < MS_NZ;
}

MC_HD static inline u16 ms_get(const u16 *blocks, int x, int y, int z) {
    return ms_in(x, y, z) ? blocks[ms_idx(x, y, z)] : mc_state(BLK_AIR, 0);
}

MC_HD static inline void ms_set(u16 *blocks, int x, int y, int z, u16 s) {
    if (ms_in(x, y, z)) blocks[ms_idx(x, y, z)] = s;
}

MC_HD static inline int ms_height_at(const u16 *blocks, int x, int z) {
    int y;
    for (y = MS_NY - 1; y >= 0; --y) {
        int id = mc_state_id(ms_get(blocks, x, y, z));
        BptProps p = mc_bpt_props(id);
        if (p.light_opacity != 0) return y + 1;
    }
    return 0;
}

MC_HD static inline int ms_is_normal_cube(int id) {
    if (id <= 0) return 0;
    BptProps p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) != 0;
}

MC_HD static inline int ms_is_valid_empty_spawn(int id) {
    if (id == BLK_AIR) return 1;
    BptProps p = mc_bpt_props(id);
    if (p.flags & BF_LIQUID) return 0;
    if (p.flags & BF_SOLID) return 0;
    return 1;
}

MC_HD static inline int ms_can_spawn_blocks(const u16 *blocks, int x, int y, int z) {
    int below = mc_state_id(ms_get(blocks, x, y - 1, z));
    if (below == BLK_BEDROCK) return 0;
    if (!ms_is_normal_cube(below)) return 0;
    if (!ms_is_valid_empty_spawn(mc_state_id(ms_get(blocks, x, y, z)))) return 0;
    if (!ms_is_valid_empty_spawn(mc_state_id(ms_get(blocks, x, y + 1, z)))) return 0;
    return 1;
}

MC_HD static inline void ms_build_light(const u16 *blocks, u8 *sky, u8 *blk) {
    int x, y, z;
    u8 hm[MS_NX * MS_NZ];
    for (z = 0; z < MS_NZ; ++z)
        for (x = 0; x < MS_NX; ++x)
            hm[z * MS_NX + x] = (u8)ms_height_at(blocks, x, z);

    for (y = 0; y < MS_NY; ++y)
        for (z = 0; z < MS_NZ; ++z)
            for (x = 0; x < MS_NX; ++x) {
                int i = ms_idx(x, y, z);
                sky[i] = (y >= (int)hm[z * MS_NX + x]) ? 15 : 0;
                blk[i] = 0;
            }

    for (y = 0; y < MS_NY; ++y)
        for (z = 0; z < MS_NZ; ++z)
            for (x = 0; x < MS_NX; ++x) {
                int id = mc_state_id(ms_get(blocks, x, y, z));
                int emit = (int)mc_bpt_props(id).light_emit;
                if (emit <= 0) continue;
                int r, dz, dx, dy;
                for (dy = -14; dy <= 14; ++dy)
                    for (dz = -14; dz <= 14; ++dz)
                        for (dx = -14; dx <= 14; ++dx) {
                            int nx = x + dx, ny = y + dy, nz = z + dz;
                            if (!ms_in(nx, ny, nz)) continue;
                            r = dx; if (dx < 0) r = -dx;
                            if (dz < 0) { if (-dz > r) r = -dz; } else if (dz > r) r = dz;
                            if (dy < 0) { if (-dy > r) r = -dy; } else if (dy > r) r = dy;
                            if (r >= emit) continue;
                            {
                                int lv = emit - r;
                                int ni = ms_idx(nx, ny, nz);
                                if (lv > (int)blk[ni]) blk[ni] = (u8)lv;
                            }
                        }
            }
}

MC_HD static inline void ms_init_flat(MsScene *s, u64 seed) {
    u16 air = mc_state(BLK_AIR, 0);
    u16 bed = mc_state(BLK_BEDROCK, 0);
    u16 stone = mc_state(BLK_STONE, 0);
    u16 grass = mc_state(BLK_GRASS, 0);
    u16 torch = mc_state(BLK_TORCH, 0);
    int x, y, z;

    s->seed = seed;
    s->tick = 0;
    s->n_decisions = 0;

    for (y = 0; y < MS_NY; ++y)
        for (z = 0; z < MS_NZ; ++z)
            for (x = 0; x < MS_NX; ++x)
                ms_set(s->blocks, x, y, z, air);

    for (z = 0; z < MS_NZ; ++z)
        for (x = 0; x < MS_NX; ++x) {
            ms_set(s->blocks, x, 0, z, bed);
            for (y = 1; y <= 3; ++y) ms_set(s->blocks, x, y, z, stone);
            ms_set(s->blocks, x, MS_FLOOR_Y, z, grass);
        }

    /* Seed-varied dark pocket: stone ceiling y=8..10 over a patch. */
    {
        int cx = 3 + (int)(seed % 5);
        int cz = 3 + (int)((seed / 5) % 5);
        int rad = 2 + (int)((seed / 25) % 2);
        for (z = 0; z < MS_NZ; ++z)
            for (x = 0; x < MS_NX; ++x) {
                int dx = x - cx, dz = z - cz;
                if (dx < 0) dx = -dx;
                if (dz < 0) dz = -dz;
                if (dx <= rad && dz <= rad) {
                    ms_set(s->blocks, x, 8, z, stone);
                    ms_set(s->blocks, x, 9, z, stone);
                    if ((seed + (u64)(x * 7 + z * 11)) % 3 != 0)
                        ms_set(s->blocks, x, 10, z, stone);
                }
            }
    }

    /* Torches for block-light variation. */
    ms_set(s->blocks, 2, MS_FLOOR_Y + 1, 2, torch);
    ms_set(s->blocks, 13, MS_FLOOR_Y + 1, 13, torch);
    if (seed % 2 == 0)
        ms_set(s->blocks, 7, MS_FLOOR_Y + 1, 12, torch);

    ms_build_light(s->blocks, s->sky, s->blk);
}

MC_HD static inline u64 ms_pack_decision(int attempt, int x, int y, int z,
                                         int sky, int blk_lt, int block_id,
                                         int result, int mob_type) {
    u64 v = 0;
    v |= (u64)(attempt & 0xFFFF);
    v |= (u64)(x & 0xFF) << 16;
    v |= (u64)(y & 0xFF) << 24;
    v |= (u64)(z & 0xFF) << 32;
    v |= (u64)(sky & 0xF) << 40;
    v |= (u64)(blk_lt & 0xF) << 44;
    v |= (u64)(block_id & 0xFF) << 48;
    v |= (u64)(result & 0xF) << 56;
    v |= (u64)(mob_type & 0xF) << 60;
    return v;
}

MC_HD static inline void ms_record(MsScene *s, u64 d) {
    if (s->n_decisions < MS_MAX_DECISIONS)
        s->decisions[s->n_decisions++] = d;
}

MC_HD static inline float ms_dist_sq(float ax, float ay, float az, float bx, float by, float bz) {
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return dx * dx + dy * dy + dz * dz;
}

MC_HD static inline u8 ms_pick_monster(u64 seed, i64 tick, int x, int y, int z) {
    u64 h = mc_hash_seed(seed, tick, x, y, z, MS_PURPOSE_MOB);
    i32 roll = mc_hash_bound(h, MS_MONSTER_TOTAL_WEIGHT);
    if (roll < 100) return 6;  /* ENT_SPIDER */
    if (roll < 200) return 3;  /* ENT_ZOMBIE */
    if (roll < 300) return 4;  /* ENT_SKELETON */
    if (roll < 400) return 5;  /* ENT_CREEPER */
    return 7;                  /* ENT_ENDERMAN */
}

MC_HD static inline int ms_initial_air(const u16 *blocks, int x, int y, int z) {
    int id = mc_state_id(ms_get(blocks, x, y, z));
    if (ms_is_normal_cube(id)) return 0;
    if (id != BLK_AIR) {
        BptProps p = mc_bpt_props(id);
        if (p.flags & BF_LIQUID) return 0;
        if (p.flags & BF_SOLID) return 0;
    }
    return 1;
}

/* One hostile spawn cycle on the flat chunk (WorldEntitySpawner monster path, hash RNG). */
MC_HD static inline void ms_hostile_spawn_cycle(MsScene *s) {
    const u16 *blocks = s->blocks;
    int eligible = 1;
    int cap = MS_MONSTER_CAP * eligible / MS_MOB_COUNT_DIV;
    int existing = 0;
    int base_x, base_y, base_z;
    u64 h;
    int k2, i4, l3;
    int ax, ay, az;
    float fx, fy, fz;

    if (existing > cap) return;

    h = mc_hash_seed(s->seed, s->tick, 0, 0, 0, MS_PURPOSE_BASE);
    base_x = mc_hash_bound(h, MS_NX);
    h = mc_hash64(h + 1);
    base_z = mc_hash_bound(h, MS_NZ);
    {
        int hm = ms_height_at(blocks, base_x, base_z);
        int y_bound = hm + 16 - 1;
        if (y_bound < 1) y_bound = MS_FLOOR_Y + 1;
        h = mc_hash64(h + 2);
        base_y = mc_hash_bound(h, y_bound);
    }

    if (!ms_initial_air(blocks, base_x, base_y, base_z)) {
        ms_record(s, ms_pack_decision(0, base_x, base_y, base_z,
                                      (int)s->sky[ms_idx(base_x, base_y, base_z)],
                                      (int)s->blk[ms_idx(base_x, base_y, base_z)],
                                      mc_state_id(ms_get(blocks, base_x, base_y, base_z)),
                                      MS_RES_FAIL_INIT_SOLID, 0));
        return;
    }

    for (k2 = 0; k2 < 3; ++k2) {
        h = mc_hash_seed(s->seed, s->tick, base_x, base_y, base_z, MS_PURPOSE_GROUP);
        h = mc_hash64(h ^ (u64)k2);
        l3 = 1 + mc_hash_bound(h, 4);

        ax = base_x;
        ay = base_y;
        az = base_z;

        for (i4 = 0; i4 < l3; ++i4) {
            int attempt = s->n_decisions;
            u64 hw = mc_hash_seed(s->seed, s->tick, ax, ay, az, MS_PURPOSE_WALK);
            hw = mc_hash64(hw ^ (u64)(k2 * 16 + i4));
            {
                i32 a = mc_hash_bound(hw, 6);
                hw = mc_hash64(hw + 1);
                i32 b = mc_hash_bound(hw, 6);
                ax += a - b;
            }
            {
                i32 a = mc_hash_bound(hw, 2);
                hw = mc_hash64(hw + 1);
                i32 b = mc_hash_bound(hw, 2);
                ay += a - b;
            }
            {
                i32 a = mc_hash_bound(hw, 6);
                hw = mc_hash64(hw + 1);
                i32 b = mc_hash_bound(hw, 6);
                az += a - b;
            }

            if (!ms_in(ax, ay, az)) {
                ms_record(s, ms_pack_decision(attempt, ax, ay, az, 0, 0, 0, MS_RES_FAIL_BLOCK, 0));
                continue;
            }

            fx = (float)ax + 0.5f;
            fy = (float)ay;
            fz = (float)az + 0.5f;

            if (ms_dist_sq(fx, fy, fz, MS_PLAYER_X, MS_PLAYER_Y, MS_PLAYER_Z) < 576.0f) {
                ms_record(s, ms_pack_decision(attempt, ax, ay, az,
                                              (int)s->sky[ms_idx(ax, ay, az)],
                                              (int)s->blk[ms_idx(ax, ay, az)],
                                              mc_state_id(ms_get(blocks, ax, ay, az)),
                                              MS_RES_FAIL_PLAYER, 0));
                continue;
            }

            if (ms_dist_sq(fx, fy, fz,
                           (float)MS_WORLD_SPAWN_X, (float)MS_WORLD_SPAWN_Y, (float)MS_WORLD_SPAWN_Z) < 576.0f) {
                ms_record(s, ms_pack_decision(attempt, ax, ay, az,
                                              (int)s->sky[ms_idx(ax, ay, az)],
                                              (int)s->blk[ms_idx(ax, ay, az)],
                                              mc_state_id(ms_get(blocks, ax, ay, az)),
                                              MS_RES_FAIL_SPAWN_PT, 0));
                continue;
            }

            if (!ms_can_spawn_blocks(blocks, ax, ay, az)) {
                ms_record(s, ms_pack_decision(attempt, ax, ay, az,
                                              (int)s->sky[ms_idx(ax, ay, az)],
                                              (int)s->blk[ms_idx(ax, ay, az)],
                                              mc_state_id(ms_get(blocks, ax, ay, az)),
                                              MS_RES_FAIL_BLOCK, 0));
                continue;
            }

            {
                int sky = (int)s->sky[ms_idx(ax, ay, az)];
                int bl = (int)s->blk[ms_idx(ax, ay, az)];
                u64 hl = mc_hash_seed(s->seed, s->tick, ax, ay, az, MS_PURPOSE_LIGHT);
                i32 sky_thr = mc_hash_bound(hl, 32);
                hl = mc_hash64(hl + 1);
                i32 blk_thr = mc_hash_bound(hl, 8);
                if (sky > sky_thr) {
                    ms_record(s, ms_pack_decision(attempt, ax, ay, az, sky, bl,
                                                  mc_state_id(ms_get(blocks, ax, ay, az)),
                                                  MS_RES_FAIL_LIGHT_SKY, 0));
                    continue;
                }
                if (bl > blk_thr) {
                    ms_record(s, ms_pack_decision(attempt, ax, ay, az, sky, bl,
                                                  mc_state_id(ms_get(blocks, ax, ay, az)),
                                                  MS_RES_FAIL_LIGHT_BLK, 0));
                    continue;
                }
            }

            {
                u8 mob = ms_pick_monster(s->seed, s->tick, ax, ay, az);
                int sky = (int)s->sky[ms_idx(ax, ay, az)];
                int bl = (int)s->blk[ms_idx(ax, ay, az)];
                ms_record(s, ms_pack_decision(attempt, ax, ay, az, sky, bl,
                                              mc_state_id(ms_get(blocks, ax, ay, az)),
                                              MS_RES_SPAWN, (int)mob));
            }
        }
    }
}

MC_HD static inline void ms_run(MsScene *s, i64 tick) {
    s->tick = tick;
    s->n_decisions = 0;
    ms_hostile_spawn_cycle(s);
}

#endif /* MC_MOB_SPAWNING_H */
