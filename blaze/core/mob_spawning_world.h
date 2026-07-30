/* mob_spawning_world: hostile spawn cycle on a 17x17 flat-chunk world with fixpoint light.
 *
 * INTERNAL verify (CPU==CUDA). READ-ONLY compose: mob_spawning.h (spawn logic/constants),
 * light_propagation.h (Jacobi CA semantics), chunk_provider_flat.h (multi-chunk terrain).
 * Does NOT edit those headers; inlines light CA via block_props_table and replicates
 * ms_hostile_spawn_cycle over the 15x15 interior eligible chunk set (border ring excluded,
 * matching WorldEntitySpawner MOB_COUNT_DIV=289 cap scaling).
 *
 * Scene: cpf_provide_chunk default flat preset for each of 17x17 chunks, seed-varied stone
 * ceiling pockets + torches in interior chunks, lp fixpoint skylight+block light, one hostile
 * spawn pass with hash-RNG chunk shuffle. Output: packed u64 hex spawn decisions (same layout
 * as mob_spawning.h ms_pack_decision). */
#ifndef MC_MOB_SPAWNING_WORLD_H
#define MC_MOB_SPAWNING_WORLD_H

#include "mc.h"
#include "mc_world.h"
#include "mc_blocks.h"
#include "mc_rng.h"
#include "block_props_table.h"
#include "chunk_provider_flat.h"

#define MSW_CHUNKS         17
#define MSW_NX             (MSW_CHUNKS * 16)
#define MSW_NY             48
#define MSW_NZ             (MSW_CHUNKS * 16)
#define MSW_VOL            (MSW_NX * MSW_NY * MSW_NZ)

#define MSW_PLAYER_CHUNK   8
#define MSW_FLOOR_Y        3
#define MSW_MOB_COUNT_DIV  289
#define MSW_MONSTER_CAP    70
#define MSW_MAX_DECISIONS  4096
#define MSW_MAX_LIGHT_ITERS 512
#define MSW_MAX_ELIGIBLE   225

#define MSW_PLAYER_X       136.5f
#define MSW_PLAYER_Y       4.0f
#define MSW_PLAYER_Z       136.5f
#define MSW_WORLD_SPAWN_X  0
#define MSW_WORLD_SPAWN_Y  200
#define MSW_WORLD_SPAWN_Z  0

#define MSW_MONSTER_TOTAL_WEIGHT 410

enum {
    MSW_PURPOSE_BASE    = 1,
    MSW_PURPOSE_GROUP   = 2,
    MSW_PURPOSE_WALK    = 3,
    MSW_PURPOSE_LIGHT   = 4,
    MSW_PURPOSE_MOB     = 5,
    MSW_PURPOSE_SHUFFLE = 6,
};

enum {
    MSW_RES_SPAWN           = 0,
    MSW_RES_FAIL_BLOCK      = 1,
    MSW_RES_FAIL_LIGHT_SKY  = 2,
    MSW_RES_FAIL_LIGHT_BLK  = 3,
    MSW_RES_FAIL_PLAYER     = 4,
    MSW_RES_FAIL_SPAWN_PT   = 5,
    MSW_RES_FAIL_INIT_SOLID = 6,
};

typedef struct {
    u16 blocks[MSW_VOL];
    u8  sky[MSW_VOL];
    u8  blk[MSW_VOL];
    u64 seed;
    i64 tick;
    int n_decisions;
    u64 decisions[MSW_MAX_DECISIONS];
} MswScene;

typedef struct {
    int cx;
    int cz;
    u64 order;
} MswChunkEntry;

MC_HD static inline int msw_idx(int x, int y, int z) {
    return (y * MSW_NZ + z) * MSW_NX + x;
}

MC_HD static inline int msw_in(int x, int y, int z) {
    return x >= 0 && x < MSW_NX && y >= 0 && y < MSW_NY && z >= 0 && z < MSW_NZ;
}

MC_HD static inline u16 msw_get(const u16 *blocks, int x, int y, int z) {
    return msw_in(x, y, z) ? blocks[msw_idx(x, y, z)] : mc_state(BLK_AIR, 0);
}

MC_HD static inline void msw_set(u16 *blocks, int x, int y, int z, u16 s) {
    if (msw_in(x, y, z)) blocks[msw_idx(x, y, z)] = s;
}

MC_HD static inline u16 msw_cpf_to_state(int cpf) {
    switch (cpf) {
        case CPF_AIR:       return mc_state(BLK_AIR, 0);
        case CPF_STONE:     return mc_state(BLK_STONE, 0);
        case CPF_GRASS:     return mc_state(BLK_GRASS, 0);
        case CPF_DIRT:      return mc_state(BLK_DIRT, 0);
        case CPF_BEDROCK:   return mc_state(BLK_BEDROCK, 0);
        case CPF_SAND:      return mc_state(BLK_SAND, 0);
        case CPF_GRAVEL:    return mc_state(BLK_GRAVEL, 0);
        case CPF_SANDSTONE: return mc_state(BLK_SANDSTONE, 0);
        default:            return mc_state(BLK_STONE, 0);
    }
}

/* ===== light propagation (light_propagation.h Jacobi semantics, block_props_table props) ===== */

MC_HD static inline int msw_lp_emit(int id) {
    return (int)mc_bpt_props(id).light_emit;
}

MC_HD static inline int msw_lp_opacity_raw(int id) {
    if (id == BLK_AIR) return 0;
    BptProps p = mc_bpt_props(id);
    if (p.light_opacity == 0) return 0;
    return 255;
}

MC_HD static inline int msw_lp_effective_opacity(int id, int emit) {
    int j = msw_lp_opacity_raw(id);
    if (j >= 15 && emit > 0) j = 1;
    if (j < 1) j = 1;
    return j;
}

MC_HD static inline int msw_height_at(const u16 *blocks, int x, int z) {
    int y;
    for (y = MSW_NY - 1; y >= 0; --y) {
        int id = mc_state_id(msw_get(blocks, x, y, z));
        if (msw_lp_opacity_raw(id) != 0) return y + 1;
    }
    return 0;
}

MC_HD static inline void msw_build_height_map(const u16 *blocks, u8 *hm) {
    int x, z;
    for (z = 0; z < MSW_NZ; ++z)
        for (x = 0; x < MSW_NX; ++x)
            hm[z * MSW_NX + x] = (u8)msw_height_at(blocks, x, z);
}

MC_HD static inline int msw_can_see_sky(const u8 *hm, int x, int y, int z) {
    return y >= (int)hm[z * MSW_NX + x];
}

MC_HD static inline int msw_neighbor_sky(const u8 *sky, int x, int y, int z, int dx, int dy, int dz) {
    int nx = x + dx, ny = y + dy, nz = z + dz;
    if (!msw_in(nx, ny, nz)) return 0;
    return (int)sky[msw_idx(nx, ny, nz)];
}

MC_HD static inline int msw_neighbor_block(const u8 *blk, int x, int y, int z, int dx, int dy, int dz) {
    int nx = x + dx, ny = y + dy, nz = z + dz;
    if (!msw_in(nx, ny, nz)) return 0;
    return (int)blk[msw_idx(nx, ny, nz)];
}

MC_HD static inline int msw_raw_sky(const u8 *sky, const u16 *blocks, const u8 *hm,
                                    int x, int y, int z) {
    if (msw_can_see_sky(hm, x, y, z)) return 15;
    {
        int id = mc_state_id(msw_get(blocks, x, y, z));
        int j = msw_lp_effective_opacity(id, 0);
        if (j >= 15) return 0;
        {
            int i = 0, k;
            k = msw_neighbor_sky(sky, x, y, z, 1, 0, 0) - j;  if (k > i) i = k;
            k = msw_neighbor_sky(sky, x, y, z, -1, 0, 0) - j; if (k > i) i = k;
            k = msw_neighbor_sky(sky, x, y, z, 0, 1, 0) - j;  if (k > i) i = k;
            k = msw_neighbor_sky(sky, x, y, z, 0, -1, 0) - j; if (k > i) i = k;
            k = msw_neighbor_sky(sky, x, y, z, 0, 0, 1) - j;  if (k > i) i = k;
            k = msw_neighbor_sky(sky, x, y, z, 0, 0, -1) - j; if (k > i) i = k;
            return i;
        }
    }
}

MC_HD static inline int msw_raw_block(const u8 *blk, const u16 *blocks, int x, int y, int z) {
    int id = mc_state_id(msw_get(blocks, x, y, z));
    int emit = msw_lp_emit(id);
    int j = msw_lp_effective_opacity(id, emit);
    if (j >= 15) return 0;
    if (emit >= 14) return emit;
    {
        int i = emit, k;
        k = msw_neighbor_block(blk, x, y, z, 1, 0, 0) - j;  if (k > i) i = k;
        k = msw_neighbor_block(blk, x, y, z, -1, 0, 0) - j; if (k > i) i = k;
        k = msw_neighbor_block(blk, x, y, z, 0, 1, 0) - j;  if (k > i) i = k;
        k = msw_neighbor_block(blk, x, y, z, 0, -1, 0) - j; if (k > i) i = k;
        k = msw_neighbor_block(blk, x, y, z, 0, 0, 1) - j;  if (k > i) i = k;
        k = msw_neighbor_block(blk, x, y, z, 0, 0, -1) - j; if (k > i) i = k;
        return i;
    }
}

MC_HD static inline int msw_light_packed_equal(const u8 *a_sky, const u8 *a_blk,
                                               const u8 *b_sky, const u8 *b_blk) {
    int i;
    for (i = 0; i < MSW_VOL; ++i) {
        u8 pa = mc_light(a_sky[i], a_blk[i]);
        u8 pb = mc_light(b_sky[i], b_blk[i]);
        if (pa != pb) return 0;
    }
    return 1;
}

MC_HD static inline void msw_ca_step(const u8 *cur_sky, const u8 *cur_blk,
                                     u8 *next_sky, u8 *next_blk,
                                     const u16 *blocks, const u8 *hm) {
    int x, y, z;
    for (y = 0; y < MSW_NY; ++y)
        for (z = 0; z < MSW_NZ; ++z)
            for (x = 0; x < MSW_NX; ++x) {
                int i = msw_idx(x, y, z);
                next_sky[i] = (u8)msw_raw_sky(cur_sky, blocks, hm, x, y, z);
                next_blk[i] = (u8)msw_raw_block(cur_blk, blocks, x, y, z);
            }
}

MC_HD static inline void msw_propagate_light(u8 *sky, u8 *blk, u8 *tmp_sky, u8 *tmp_blk,
                                             const u16 *blocks, int max_iters) {
    u8 hm[MSW_NX * MSW_NZ];
    int i;
    msw_build_height_map(blocks, hm);
    for (i = 0; i < max_iters; ++i) {
        msw_ca_step(sky, blk, tmp_sky, tmp_blk, blocks, hm);
        if (msw_light_packed_equal(sky, blk, tmp_sky, tmp_blk)) break;
        {
            int j;
            for (j = 0; j < MSW_VOL; ++j) {
                sky[j] = tmp_sky[j];
                blk[j] = tmp_blk[j];
            }
        }
    }
}

/* ===== spawn helpers (mob_spawning.h logic, world coords) ===== */

MC_HD static inline int msw_is_normal_cube(int id) {
    if (id <= 0) return 0;
    BptProps p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) != 0;
}

MC_HD static inline int msw_is_valid_empty_spawn(int id) {
    if (id == BLK_AIR) return 1;
    BptProps p = mc_bpt_props(id);
    if (p.flags & BF_LIQUID) return 0;
    if (p.flags & BF_SOLID) return 0;
    return 1;
}

MC_HD static inline int msw_can_spawn_blocks(const u16 *blocks, int x, int y, int z) {
    int below = mc_state_id(msw_get(blocks, x, y - 1, z));
    if (below == BLK_BEDROCK) return 0;
    if (!msw_is_normal_cube(below)) return 0;
    if (!msw_is_valid_empty_spawn(mc_state_id(msw_get(blocks, x, y, z)))) return 0;
    if (!msw_is_valid_empty_spawn(mc_state_id(msw_get(blocks, x, y + 1, z)))) return 0;
    return 1;
}

MC_HD static inline u64 msw_pack_decision(int attempt, int x, int y, int z,
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

MC_HD static inline void msw_record(MswScene *s, u64 d) {
    if (s->n_decisions < MSW_MAX_DECISIONS)
        s->decisions[s->n_decisions++] = d;
}

MC_HD static inline float msw_dist_sq(float ax, float ay, float az, float bx, float by, float bz) {
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return dx * dx + dy * dy + dz * dz;
}

MC_HD static inline u8 msw_pick_monster(u64 seed, i64 tick, int x, int y, int z) {
    u64 h = mc_hash_seed(seed, tick, x, y, z, MSW_PURPOSE_MOB);
    i32 roll = mc_hash_bound(h, MSW_MONSTER_TOTAL_WEIGHT);
    if (roll < 100) return 6;
    if (roll < 200) return 3;
    if (roll < 300) return 4;
    if (roll < 400) return 5;
    return 7;
}

MC_HD static inline int msw_initial_air(const u16 *blocks, int x, int y, int z) {
    int id = mc_state_id(msw_get(blocks, x, y, z));
    if (msw_is_normal_cube(id)) return 0;
    if (id != BLK_AIR) {
        BptProps p = mc_bpt_props(id);
        if (p.flags & BF_LIQUID) return 0;
        if (p.flags & BF_SOLID) return 0;
    }
    return 1;
}

MC_HD static inline int msw_chunk_eligible(int cx, int cz) {
    if (cx <= 0 || cx >= MSW_CHUNKS - 1) return 0;
    if (cz <= 0 || cz >= MSW_CHUNKS - 1) return 0;
    return 1;
}

MC_HD static inline void msw_fill_eligible(MswChunkEntry *out, int *n_out, u64 seed, i64 tick) {
    int n = 0;
    int cx, cz;
    for (cx = 1; cx < MSW_CHUNKS - 1; ++cx)
        for (cz = 1; cz < MSW_CHUNKS - 1; ++cz) {
            out[n].cx = cx;
            out[n].cz = cz;
            out[n].order = mc_hash_seed(seed, tick, cx, cz, 0, MSW_PURPOSE_SHUFFLE);
            ++n;
        }
    *n_out = n;
}

MC_HD static inline void msw_sort_chunks(MswChunkEntry *arr, int n) {
    int i, j;
    for (i = 1; i < n; ++i) {
        MswChunkEntry key = arr[i];
        j = i - 1;
        while (j >= 0 && (arr[j].order > key.order ||
               (arr[j].order == key.order &&
                (arr[j].cx > key.cx || (arr[j].cx == key.cx && arr[j].cz > key.cz))))) {
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = key;
    }
}

MC_HD static inline void msw_build_world(u16 *blocks, u64 seed) {
    CpfPrimer primer;
    u16 air = mc_state(BLK_AIR, 0);
    u16 stone = mc_state(BLK_STONE, 0);
    u16 torch = mc_state(BLK_TORCH, 0);
    int x, y, z, cx, cz;

    for (y = 0; y < MSW_NY; ++y)
        for (z = 0; z < MSW_NZ; ++z)
            for (x = 0; x < MSW_NX; ++x)
                msw_set(blocks, x, y, z, air);

    for (cx = 0; cx < MSW_CHUNKS; ++cx) {
        for (cz = 0; cz < MSW_CHUNKS; ++cz) {
            cpf_provide_chunk(&primer, NULL);
            for (y = 0; y < MSW_NY; ++y)
                for (z = 0; z < 16; ++z)
                    for (x = 0; x < 16; ++x)
                        msw_set(blocks, cx * 16 + x, y, cz * 16 + z,
                                msw_cpf_to_state((int)primer.data[cpf_index(x, y, z)]));
        }
    }

    /* Seed-varied dark pockets in interior chunks. */
    for (cx = 1; cx < MSW_CHUNKS - 1; ++cx) {
        for (cz = 1; cz < MSW_CHUNKS - 1; ++cz) {
            u64 h = mc_hash_seed(seed, (u64)cx, (u64)cz, 0, 0, MSW_PURPOSE_BASE);
            if (mc_hash_bound(h, 5) != 0) continue;
            {
                int lx = 3 + (int)(h % 5);
                int lz = 3 + (int)((h / 5) % 5);
                int rad = 2 + (int)((h / 25) % 2);
                int wx0 = cx * 16 + lx;
                int wz0 = cz * 16 + lz;
                int dx, dz;
                for (dz = -rad; dz <= rad; ++dz)
                    for (dx = -rad; dx <= rad; ++dx) {
                        int wx = wx0 + dx, wz = wz0 + dz;
                        if (!msw_in(wx, 8, wz)) continue;
                        msw_set(blocks, wx, 8, wz, stone);
                        msw_set(blocks, wx, 9, wz, stone);
                        if ((seed + (u64)(wx * 7 + wz * 11)) % 3 != 0)
                            msw_set(blocks, wx, 10, wz, stone);
                    }
            }
        }
    }

    /* Torches in a few fixed interior locations + seed-gated extras. */
    msw_set(blocks, 2 * 16 + 2, MSW_FLOOR_Y + 1, 2 * 16 + 2, torch);
    msw_set(blocks, 14 * 16 + 13, MSW_FLOOR_Y + 1, 14 * 16 + 13, torch);
    if (seed % 2 == 0)
        msw_set(blocks, MSW_PLAYER_CHUNK * 16 + 7, MSW_FLOOR_Y + 1,
                MSW_PLAYER_CHUNK * 16 + 12, torch);
}

MC_HD static inline void msw_hostile_spawn_chunk(MswScene *s, int cx, int cz) {
    const u16 *blocks = s->blocks;
    int base_x, base_y, base_z;
    u64 h;
    int k2, i4, l3;
    int ax, ay, az;
    float fx, fy, fz;
    int x0 = cx * 16;
    int z0 = cz * 16;

    h = mc_hash_seed(s->seed, s->tick, cx, cz, 0, MSW_PURPOSE_BASE);
    base_x = x0 + mc_hash_bound(h, 16);
    h = mc_hash64(h + 1);
    base_z = z0 + mc_hash_bound(h, 16);
    {
        int hm = msw_height_at(blocks, base_x, base_z);
        int y_bound = hm + 16 - 1;
        if (y_bound < 1) y_bound = MSW_FLOOR_Y + 1;
        if (y_bound >= MSW_NY) y_bound = MSW_NY - 1;
        h = mc_hash64(h + 2);
        base_y = mc_hash_bound(h, y_bound);
    }

    if (!msw_initial_air(blocks, base_x, base_y, base_z)) {
        msw_record(s, msw_pack_decision(s->n_decisions, base_x, base_y, base_z,
                                       (int)s->sky[msw_idx(base_x, base_y, base_z)],
                                       (int)s->blk[msw_idx(base_x, base_y, base_z)],
                                       mc_state_id(msw_get(blocks, base_x, base_y, base_z)),
                                       MSW_RES_FAIL_INIT_SOLID, 0));
        return;
    }

    for (k2 = 0; k2 < 3; ++k2) {
        h = mc_hash_seed(s->seed, s->tick, base_x, base_y, base_z, MSW_PURPOSE_GROUP);
        h = mc_hash64(h ^ (u64)k2);
        l3 = 1 + mc_hash_bound(h, 4);

        ax = base_x;
        ay = base_y;
        az = base_z;

        for (i4 = 0; i4 < l3; ++i4) {
            int attempt = s->n_decisions;
            u64 hw = mc_hash_seed(s->seed, s->tick, ax, ay, az, MSW_PURPOSE_WALK);
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

            if (!msw_in(ax, ay, az)) {
                msw_record(s, msw_pack_decision(attempt, ax, ay, az, 0, 0, 0,
                                                MSW_RES_FAIL_BLOCK, 0));
                continue;
            }

            fx = (float)ax + 0.5f;
            fy = (float)ay;
            fz = (float)az + 0.5f;

            if (msw_dist_sq(fx, fy, fz, MSW_PLAYER_X, MSW_PLAYER_Y, MSW_PLAYER_Z) < 576.0f) {
                msw_record(s, msw_pack_decision(attempt, ax, ay, az,
                                                 (int)s->sky[msw_idx(ax, ay, az)],
                                                 (int)s->blk[msw_idx(ax, ay, az)],
                                                 mc_state_id(msw_get(blocks, ax, ay, az)),
                                                 MSW_RES_FAIL_PLAYER, 0));
                continue;
            }

            if (msw_dist_sq(fx, fy, fz,
                            (float)MSW_WORLD_SPAWN_X, (float)MSW_WORLD_SPAWN_Y,
                            (float)MSW_WORLD_SPAWN_Z) < 576.0f) {
                msw_record(s, msw_pack_decision(attempt, ax, ay, az,
                                                 (int)s->sky[msw_idx(ax, ay, az)],
                                                 (int)s->blk[msw_idx(ax, ay, az)],
                                                 mc_state_id(msw_get(blocks, ax, ay, az)),
                                                 MSW_RES_FAIL_SPAWN_PT, 0));
                continue;
            }

            if (!msw_can_spawn_blocks(blocks, ax, ay, az)) {
                msw_record(s, msw_pack_decision(attempt, ax, ay, az,
                                                 (int)s->sky[msw_idx(ax, ay, az)],
                                                 (int)s->blk[msw_idx(ax, ay, az)],
                                                 mc_state_id(msw_get(blocks, ax, ay, az)),
                                                 MSW_RES_FAIL_BLOCK, 0));
                continue;
            }

            {
                int sky = (int)s->sky[msw_idx(ax, ay, az)];
                int bl = (int)s->blk[msw_idx(ax, ay, az)];
                u64 hl = mc_hash_seed(s->seed, s->tick, ax, ay, az, MSW_PURPOSE_LIGHT);
                i32 sky_thr = mc_hash_bound(hl, 32);
                hl = mc_hash64(hl + 1);
                i32 blk_thr = mc_hash_bound(hl, 8);
                if (sky > sky_thr) {
                    msw_record(s, msw_pack_decision(attempt, ax, ay, az, sky, bl,
                                                     mc_state_id(msw_get(blocks, ax, ay, az)),
                                                     MSW_RES_FAIL_LIGHT_SKY, 0));
                    continue;
                }
                if (bl > blk_thr) {
                    msw_record(s, msw_pack_decision(attempt, ax, ay, az, sky, bl,
                                                     mc_state_id(msw_get(blocks, ax, ay, az)),
                                                     MSW_RES_FAIL_LIGHT_BLK, 0));
                    continue;
                }
            }

            {
                u8 mob = msw_pick_monster(s->seed, s->tick, ax, ay, az);
                int sky = (int)s->sky[msw_idx(ax, ay, az)];
                int bl = (int)s->blk[msw_idx(ax, ay, az)];
                msw_record(s, msw_pack_decision(attempt, ax, ay, az, sky, bl,
                                                 mc_state_id(msw_get(blocks, ax, ay, az)),
                                                 MSW_RES_SPAWN, (int)mob));
            }
        }
    }
}

MC_HD static inline void msw_hostile_spawn_cycle(MswScene *s) {
    MswChunkEntry order[MSW_MAX_ELIGIBLE];
    int n_eligible = 0;
    int eligible = 0;
    int cap;
    int existing = 0;
    int i;

    msw_fill_eligible(order, &n_eligible, s->seed, s->tick);
    eligible = n_eligible;
    cap = MSW_MONSTER_CAP * eligible / MSW_MOB_COUNT_DIV;
    if (existing > cap) return;

    msw_sort_chunks(order, n_eligible);

    for (i = 0; i < n_eligible; ++i)
        msw_hostile_spawn_chunk(s, order[i].cx, order[i].cz);
}

MC_HD static inline void msw_init(MswScene *s, u64 seed,
                                  u8 *tmp_sky, u8 *tmp_blk) {
    int i;
    s->seed = seed;
    s->tick = 0;
    s->n_decisions = 0;

    msw_build_world(s->blocks, seed);

    for (i = 0; i < MSW_VOL; ++i) {
        s->sky[i] = 0;
        s->blk[i] = 0;
    }
    msw_propagate_light(s->sky, s->blk, tmp_sky, tmp_blk, s->blocks, MSW_MAX_LIGHT_ITERS);
}

MC_HD static inline void msw_run(MswScene *s, i64 tick, u8 *tmp_sky, u8 *tmp_blk) {
    (void)tmp_sky;
    (void)tmp_blk;
    s->tick = tick;
    s->n_decisions = 0;
    msw_hostile_spawn_cycle(s);
}

#endif /* MC_MOB_SPAWNING_WORLD_H */
