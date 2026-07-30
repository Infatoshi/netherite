/* tick_spawn: Wave 14 - mob_spawning_world hostile pass on ticked 16x256x16 env.
 *
 * INTERNAL verify (CPU==CUDA). Composes tick_world_copy (now->next copy + tick++) with one
 * hostile spawn cycle per tick on a 16x48x16 slice (y=0..47). Fixpoint light from
 * mob_spawning_world Jacobi semantics; spawn decisions via msw_pack_decision layout.
 * Per-tick dump: tick, spawn-decision FNV hash, cur buffer index (16 ticks).
 * READ-ONLY deps: tick_world_copy.h, mob_spawning_world.h. */
#ifndef MC_TICK_SPAWN_H
#define MC_TICK_SPAWN_H

#include "tick_world_copy.h"
#include "mob_spawning_world.h"

#define TS_NX 16
#define TS_NY 48
#define TS_NZ 16
#define TS_VOL (TS_NX * TS_NY * TS_NZ)
#define TS_OY 0
#define TS_FLOOR_Y 4
#define TS_MAX_DECISIONS 512

#define TS_PLAYER_X 8.5f
#define TS_PLAYER_Y 5.0f
#define TS_PLAYER_Z 8.5f

typedef struct {
    u8 sky[TS_VOL];
    u8 blk[TS_VOL];
} TsAux;

MC_HD static inline int ts_idx(int x, int y, int z) {
    return (y * TS_NZ + z) * TS_NX + x;
}

MC_HD static inline int ts_in(int x, int y, int z) {
    return x >= 0 && x < TS_NX && y >= 0 && y < TS_NY && z >= 0 && z < TS_NZ;
}

MC_HD static inline u16 ts_get_blocks(const u16 *blocks, int x, int y, int z) {
    return ts_in(x, y, z) ? blocks[ts_idx(x, y, z)] : mc_state(BLK_AIR, 0);
}

MC_HD static inline u16 ts_get_chunk(const Chunk *c, int x, int y, int z) {
    return mc_get(c, x, TS_OY + y, z);
}

MC_HD static inline void ts_set_chunk(Chunk *c, int x, int y, int z, u16 s) {
    mc_set(c, x, TS_OY + y, z, s);
}

MC_HD static inline void ts_extract_blocks(const Chunk *c, u16 *blocks) {
    int x, y, z;
    for (y = 0; y < TS_NY; ++y)
        for (z = 0; z < TS_NZ; ++z)
            for (x = 0; x < TS_NX; ++x)
                blocks[ts_idx(x, y, z)] = ts_get_chunk(c, x, y, z);
}

MC_HD static inline int ts_lp_emit(int id) {
    return (int)mc_bpt_props(id).light_emit;
}

MC_HD static inline int ts_lp_opacity_raw(int id) {
    if (id == BLK_AIR) return 0;
    BptProps p = mc_bpt_props(id);
    if (p.light_opacity == 0) return 0;
    return 255;
}

MC_HD static inline int ts_lp_effective_opacity(int id, int emit) {
    int j = ts_lp_opacity_raw(id);
    if (j >= 15 && emit > 0) j = 1;
    if (j < 1) j = 1;
    return j;
}

MC_HD static inline int ts_height_at(const u16 *blocks, int x, int z) {
    int y;
    for (y = TS_NY - 1; y >= 0; --y) {
        int id = mc_state_id(ts_get_blocks(blocks, x, y, z));
        if (ts_lp_opacity_raw(id) != 0) return y + 1;
    }
    return 0;
}

MC_HD static inline void ts_build_height_map(const u16 *blocks, u8 *hm) {
    int x, z;
    for (z = 0; z < TS_NZ; ++z)
        for (x = 0; x < TS_NX; ++x)
            hm[z * TS_NX + x] = (u8)ts_height_at(blocks, x, z);
}

MC_HD static inline int ts_can_see_sky(const u8 *hm, int x, int y, int z) {
    return y >= (int)hm[z * TS_NX + x];
}

MC_HD static inline int ts_neighbor_sky(const u8 *sky, int x, int y, int z, int dx, int dy, int dz) {
    int nx = x + dx, ny = y + dy, nz = z + dz;
    if (!ts_in(nx, ny, nz)) return 0;
    return (int)sky[ts_idx(nx, ny, nz)];
}

MC_HD static inline int ts_neighbor_block(const u8 *blk, int x, int y, int z, int dx, int dy, int dz) {
    int nx = x + dx, ny = y + dy, nz = z + dz;
    if (!ts_in(nx, ny, nz)) return 0;
    return (int)blk[ts_idx(nx, ny, nz)];
}

MC_HD static inline int ts_raw_sky(const u8 *sky, const u16 *blocks, const u8 *hm,
                                   int x, int y, int z) {
    if (ts_can_see_sky(hm, x, y, z)) return 15;
    {
        int id = mc_state_id(ts_get_blocks(blocks, x, y, z));
        int j = ts_lp_effective_opacity(id, 0);
        if (j >= 15) return 0;
        {
            int i = 0, k;
            k = ts_neighbor_sky(sky, x, y, z, 1, 0, 0) - j;  if (k > i) i = k;
            k = ts_neighbor_sky(sky, x, y, z, -1, 0, 0) - j; if (k > i) i = k;
            k = ts_neighbor_sky(sky, x, y, z, 0, 1, 0) - j;  if (k > i) i = k;
            k = ts_neighbor_sky(sky, x, y, z, 0, -1, 0) - j; if (k > i) i = k;
            k = ts_neighbor_sky(sky, x, y, z, 0, 0, 1) - j;  if (k > i) i = k;
            k = ts_neighbor_sky(sky, x, y, z, 0, 0, -1) - j; if (k > i) i = k;
            return i;
        }
    }
}

MC_HD static inline int ts_raw_block(const u8 *blk, const u16 *blocks, int x, int y, int z) {
    int id = mc_state_id(ts_get_blocks(blocks, x, y, z));
    int emit = ts_lp_emit(id);
    int j = ts_lp_effective_opacity(id, emit);
    if (j >= 15) return 0;
    if (emit >= 14) return emit;
    {
        int i = emit, k;
        k = ts_neighbor_block(blk, x, y, z, 1, 0, 0) - j;  if (k > i) i = k;
        k = ts_neighbor_block(blk, x, y, z, -1, 0, 0) - j; if (k > i) i = k;
        k = ts_neighbor_block(blk, x, y, z, 0, 1, 0) - j;  if (k > i) i = k;
        k = ts_neighbor_block(blk, x, y, z, 0, -1, 0) - j; if (k > i) i = k;
        k = ts_neighbor_block(blk, x, y, z, 0, 0, 1) - j;  if (k > i) i = k;
        k = ts_neighbor_block(blk, x, y, z, 0, 0, -1) - j; if (k > i) i = k;
        return i;
    }
}

MC_HD static inline int ts_light_packed_equal(const u8 *a_sky, const u8 *a_blk,
                                              const u8 *b_sky, const u8 *b_blk) {
    int i;
    for (i = 0; i < TS_VOL; ++i) {
        u8 pa = mc_light(a_sky[i], a_blk[i]);
        u8 pb = mc_light(b_sky[i], b_blk[i]);
        if (pa != pb) return 0;
    }
    return 1;
}

MC_HD static inline void ts_ca_step(const u8 *cur_sky, const u8 *cur_blk,
                                    u8 *next_sky, u8 *next_blk,
                                    const u16 *blocks, const u8 *hm) {
    int x, y, z;
    for (y = 0; y < TS_NY; ++y)
        for (z = 0; z < TS_NZ; ++z)
            for (x = 0; x < TS_NX; ++x) {
                int i = ts_idx(x, y, z);
                next_sky[i] = (u8)ts_raw_sky(cur_sky, blocks, hm, x, y, z);
                next_blk[i] = (u8)ts_raw_block(cur_blk, blocks, x, y, z);
            }
}

MC_HD static inline void ts_propagate_light(TsAux *aux, const u16 *blocks,
                                            u8 *tmp_sky, u8 *tmp_blk, int max_iters) {
    u8 hm[TS_NX * TS_NZ];
    int i;
    ts_build_height_map(blocks, hm);
    for (i = 0; i < max_iters; ++i) {
        ts_ca_step(aux->sky, aux->blk, tmp_sky, tmp_blk, blocks, hm);
        if (ts_light_packed_equal(aux->sky, aux->blk, tmp_sky, tmp_blk)) break;
        {
            int j;
            for (j = 0; j < TS_VOL; ++j) {
                aux->sky[j] = tmp_sky[j];
                aux->blk[j] = tmp_blk[j];
            }
        }
    }
}

MC_HD static inline void ts_init_fixtures(Chunk *c, u64 seed) {
    u16 air = mc_state(BLK_AIR, 0);
    u16 bed = mc_state(BLK_BEDROCK, 0);
    u16 stone = mc_state(BLK_STONE, 0);
    u16 grass = mc_state(BLK_GRASS, 0);
    u16 torch = mc_state(BLK_TORCH, 0);
    int x, y, z;

    for (y = 0; y < TS_NY; ++y)
        for (z = 0; z < TS_NZ; ++z)
            for (x = 0; x < TS_NX; ++x)
                ts_set_chunk(c, x, y, z, air);

    for (z = 0; z < TS_NZ; ++z)
        for (x = 0; x < TS_NX; ++x) {
            ts_set_chunk(c, x, 0, z, bed);
            for (y = 1; y <= 3; ++y) ts_set_chunk(c, x, y, z, stone);
            ts_set_chunk(c, x, TS_FLOOR_Y, z, grass);
        }

    {
        int cx = 3 + (int)(seed % 5);
        int cz = 3 + (int)((seed / 5) % 5);
        int rad = 2 + (int)((seed / 25) % 2);
        for (z = 0; z < TS_NZ; ++z)
            for (x = 0; x < TS_NX; ++x) {
                int dx = x - cx, dz = z - cz;
                if (dx < 0) dx = -dx;
                if (dz < 0) dz = -dz;
                if (dx <= rad && dz <= rad) {
                    ts_set_chunk(c, x, 8, z, stone);
                    ts_set_chunk(c, x, 9, z, stone);
                    if ((seed + (u64)(x * 7 + z * 11)) % 3 != 0)
                        ts_set_chunk(c, x, 10, z, stone);
                }
            }
    }

    ts_set_chunk(c, 2, TS_FLOOR_Y + 1, 2, torch);
    ts_set_chunk(c, 13, TS_FLOOR_Y + 1, 13, torch);
    if (seed % 2 == 0)
        ts_set_chunk(c, 7, TS_FLOOR_Y + 1, 12, torch);
}

MC_HD static inline void ts_init_env(Env *e, TsAux *aux, u64 seed,
                                      u16 *blocks, u8 *tmp_sky, u8 *tmp_blk) {
    int i;
    twc_init_env(e, seed);
    ts_init_fixtures(&twc_now(e)->chunk[0], seed);
    ts_extract_blocks(&twc_now(e)->chunk[0], blocks);
    for (i = 0; i < TS_VOL; ++i) {
        aux->sky[i] = 0;
        aux->blk[i] = 0;
    }
    ts_propagate_light(aux, blocks, tmp_sky, tmp_blk, MSW_MAX_LIGHT_ITERS);
    twc_copy_world(twc_next(e), twc_now(e));
}

MC_HD static inline int ts_is_normal_cube(int id) {
    if (id <= 0) return 0;
    BptProps p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) != 0;
}

MC_HD static inline int ts_is_valid_empty_spawn(int id) {
    if (id == BLK_AIR) return 1;
    BptProps p = mc_bpt_props(id);
    if (p.flags & BF_LIQUID) return 0;
    if (p.flags & BF_SOLID) return 0;
    return 1;
}

MC_HD static inline int ts_can_spawn_blocks(const u16 *blocks, int x, int y, int z) {
    int below = mc_state_id(ts_get_blocks(blocks, x, y - 1, z));
    if (below == BLK_BEDROCK) return 0;
    if (!ts_is_normal_cube(below)) return 0;
    if (!ts_is_valid_empty_spawn(mc_state_id(ts_get_blocks(blocks, x, y, z)))) return 0;
    if (!ts_is_valid_empty_spawn(mc_state_id(ts_get_blocks(blocks, x, y + 1, z)))) return 0;
    return 1;
}

MC_HD static inline int ts_initial_air(const u16 *blocks, int x, int y, int z) {
    int id = mc_state_id(ts_get_blocks(blocks, x, y, z));
    if (ts_is_normal_cube(id)) return 0;
    if (id != BLK_AIR) {
        BptProps p = mc_bpt_props(id);
        if (p.flags & BF_LIQUID) return 0;
        if (p.flags & BF_SOLID) return 0;
    }
    return 1;
}

MC_HD static inline float ts_dist_sq(float ax, float ay, float az, float bx, float by, float bz) {
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return dx * dx + dy * dy + dz * dz;
}

MC_HD static inline int ts_hostile_spawn_cycle(u64 seed, i64 tick, const u16 *blocks,
                                               const TsAux *aux, u64 *decisions, int *n_out) {
    int eligible = 1;
    int cap = MSW_MONSTER_CAP * eligible / MSW_MOB_COUNT_DIV;
    int existing = 0;
    int base_x, base_y, base_z;
    u64 h;
    int k2, i4, l3;
    int ax, ay, az;
    float fx, fy, fz;
    int n = 0;

    *n_out = 0;
    if (existing > cap) return 0;

    h = mc_hash_seed(seed, tick, 0, 0, 0, MSW_PURPOSE_BASE);
    base_x = mc_hash_bound(h, TS_NX);
    h = mc_hash64(h + 1);
    base_z = mc_hash_bound(h, TS_NZ);
    {
        int hm = ts_height_at(blocks, base_x, base_z);
        int y_bound = hm + 16 - 1;
        if (y_bound < 1) y_bound = TS_FLOOR_Y + 1;
        if (y_bound >= TS_NY) y_bound = TS_NY - 1;
        h = mc_hash64(h + 2);
        base_y = mc_hash_bound(h, y_bound);
    }

    if (!ts_initial_air(blocks, base_x, base_y, base_z)) {
        if (n < TS_MAX_DECISIONS)
            decisions[n++] = msw_pack_decision(0, base_x, base_y, base_z,
                (int)aux->sky[ts_idx(base_x, base_y, base_z)],
                (int)aux->blk[ts_idx(base_x, base_y, base_z)],
                mc_state_id(ts_get_blocks(blocks, base_x, base_y, base_z)),
                MSW_RES_FAIL_INIT_SOLID, 0);
        *n_out = n;
        return n;
    }

    for (k2 = 0; k2 < 3; ++k2) {
        h = mc_hash_seed(seed, tick, base_x, base_y, base_z, MSW_PURPOSE_GROUP);
        h = mc_hash64(h ^ (u64)k2);
        l3 = 1 + mc_hash_bound(h, 4);

        ax = base_x;
        ay = base_y;
        az = base_z;

        for (i4 = 0; i4 < l3; ++i4) {
            int attempt = n;
            u64 hw = mc_hash_seed(seed, tick, ax, ay, az, MSW_PURPOSE_WALK);
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

            if (!ts_in(ax, ay, az)) {
                if (n < TS_MAX_DECISIONS)
                    decisions[n++] = msw_pack_decision(attempt, ax, ay, az, 0, 0, 0,
                                                       MSW_RES_FAIL_BLOCK, 0);
                continue;
            }

            fx = (float)ax + 0.5f;
            fy = (float)ay;
            fz = (float)az + 0.5f;

            if (ts_dist_sq(fx, fy, fz, TS_PLAYER_X, TS_PLAYER_Y, TS_PLAYER_Z) < 576.0f) {
                if (n < TS_MAX_DECISIONS)
                    decisions[n++] = msw_pack_decision(attempt, ax, ay, az,
                        (int)aux->sky[ts_idx(ax, ay, az)],
                        (int)aux->blk[ts_idx(ax, ay, az)],
                        mc_state_id(ts_get_blocks(blocks, ax, ay, az)),
                        MSW_RES_FAIL_PLAYER, 0);
                continue;
            }

            if (ts_dist_sq(fx, fy, fz,
                           (float)MSW_WORLD_SPAWN_X, (float)MSW_WORLD_SPAWN_Y,
                           (float)MSW_WORLD_SPAWN_Z) < 576.0f) {
                if (n < TS_MAX_DECISIONS)
                    decisions[n++] = msw_pack_decision(attempt, ax, ay, az,
                        (int)aux->sky[ts_idx(ax, ay, az)],
                        (int)aux->blk[ts_idx(ax, ay, az)],
                        mc_state_id(ts_get_blocks(blocks, ax, ay, az)),
                        MSW_RES_FAIL_SPAWN_PT, 0);
                continue;
            }

            if (!ts_can_spawn_blocks(blocks, ax, ay, az)) {
                if (n < TS_MAX_DECISIONS)
                    decisions[n++] = msw_pack_decision(attempt, ax, ay, az,
                        (int)aux->sky[ts_idx(ax, ay, az)],
                        (int)aux->blk[ts_idx(ax, ay, az)],
                        mc_state_id(ts_get_blocks(blocks, ax, ay, az)),
                        MSW_RES_FAIL_BLOCK, 0);
                continue;
            }

            {
                int sky = (int)aux->sky[ts_idx(ax, ay, az)];
                int bl = (int)aux->blk[ts_idx(ax, ay, az)];
                u64 hl = mc_hash_seed(seed, tick, ax, ay, az, MSW_PURPOSE_LIGHT);
                i32 sky_thr = mc_hash_bound(hl, 32);
                hl = mc_hash64(hl + 1);
                i32 blk_thr = mc_hash_bound(hl, 8);
                if (sky > sky_thr) {
                    if (n < TS_MAX_DECISIONS)
                        decisions[n++] = msw_pack_decision(attempt, ax, ay, az, sky, bl,
                            mc_state_id(ts_get_blocks(blocks, ax, ay, az)),
                            MSW_RES_FAIL_LIGHT_SKY, 0);
                    continue;
                }
                if (bl > blk_thr) {
                    if (n < TS_MAX_DECISIONS)
                        decisions[n++] = msw_pack_decision(attempt, ax, ay, az, sky, bl,
                            mc_state_id(ts_get_blocks(blocks, ax, ay, az)),
                            MSW_RES_FAIL_LIGHT_BLK, 0);
                    continue;
                }
            }

            {
                u8 mob = msw_pick_monster(seed, tick, ax, ay, az);
                int sky = (int)aux->sky[ts_idx(ax, ay, az)];
                int bl = (int)aux->blk[ts_idx(ax, ay, az)];
                if (n < TS_MAX_DECISIONS)
                    decisions[n++] = msw_pack_decision(attempt, ax, ay, az, sky, bl,
                        mc_state_id(ts_get_blocks(blocks, ax, ay, az)),
                        MSW_RES_SPAWN, (int)mob);
            }
        }
    }

    *n_out = n;
    return n;
}

MC_HD static inline u64 ts_spawn_hash(const u64 *decisions, int n) {
    u64 h = 0xcbf29ce484222325ULL;
    int i;
    int spawn_count = 0;
    for (i = 0; i < n; ++i) {
        h ^= decisions[i];
        h *= 0x100000001b3ULL;
        if (((decisions[i] >> 56) & 0xF) == MSW_RES_SPAWN) spawn_count++;
    }
    h ^= (u64)(u32)spawn_count;
    h *= 0x100000001b3ULL;
    h ^= (u64)(u32)n;
    h *= 0x100000001b3ULL;
    return h;
}

MC_HD static inline void ts_tick_env(Env *e, TsAux *aux, u16 *blocks,
                                      u64 *decisions, int *n_dec) {
    World *now = twc_now(e);
    World *next = twc_next(e);
    twc_copy_world(next, now);
    ts_extract_blocks(&now->chunk[0], blocks);
    ts_hostile_spawn_cycle(now->seed, now->tick, blocks, aux, decisions, n_dec);
    next->tick = now->tick + 1;
    twc_swap(e);
}

typedef TwcEmitFn TsEmitFn;

MC_HD static inline void ts_run(Env *e, TsAux *aux, u64 seed,
                                u16 *blocks, u8 *tmp_sky, u8 *tmp_blk,
                                u64 *decisions, TsEmitFn emit, void *ctx) {
    int t;
    ts_init_env(e, aux, seed, blocks, tmp_sky, tmp_blk);
    for (t = 0; t < TWC_NTICKS; ++t) {
        World *now;
        u64 tick_bits, spawn_hash, cur_bits;
        int n_dec = 0;
        ts_tick_env(e, aux, blocks, decisions, &n_dec);
        now = twc_now(e);
        tick_bits = (u64)now->tick;
        spawn_hash = ts_spawn_hash(decisions, n_dec);
        cur_bits = (u64)(u32)e->cur;
        if (emit) emit(tick_bits, spawn_hash, cur_bits, ctx);
    }
}

#endif /* MC_TICK_SPAWN_H */
