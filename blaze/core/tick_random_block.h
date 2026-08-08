/* tick_random_block: Wave 14 - block_tickers + block_tickers_crops on chunk slice each tick.
 *
 * INTERNAL verify (CPU==CUDA). Composes tick_world_copy (now->next copy + tick++) with a full
 * scan of grass/fire/falling + crops/ice/snow in a 16x32x16 slice (y=58..89) plus hash-RNG
 * random-tick attempts (randomTickSpeed=3 x 16 sections) on the full 16x256x16 chunk.
 * Per-tick dump: tick, block FNV hash, cur buffer index (16 ticks).
 * READ-ONLY deps: tick_world_copy.h, block_tickers.h, block_tickers_crops.h. */
#ifndef MC_TICK_RANDOM_BLOCK_H
#define MC_TICK_RANDOM_BLOCK_H

#include "tick_world_copy.h"
#include "block_tickers.h"
#include "block_tickers_crops.h"

#define TRB_SLICE_OY 58
#define TRB_SLICE_H  32
#define TRB_SLICE_W  16
#define TRB_SLICE_VOL (TRB_SLICE_W * TRB_SLICE_W * TRB_SLICE_H)
#define TRB_RANDOM_TICK_SPEED 3
#define TRB_RANDOM_ATTEMPTS (TRB_RANDOM_TICK_SPEED * MC_NSEC)
#define TRB_PURPOSE_RANDPOS 100

typedef struct {
    u8 light_above[TRB_SLICE_VOL];
    u8 block_light[TRB_SLICE_VOL];
} TrbAux;

MC_HD static inline int trb_lidx(int x, int y, int z) {
    return (y * TRB_SLICE_W + z) * TRB_SLICE_W + x;
}

MC_HD static inline u16 trb_get(const Chunk *c, int x, int y, int z) {
    if (x < 0 || x >= TRB_SLICE_W || y < 0 || y >= TRB_SLICE_H || z < 0 || z >= TRB_SLICE_W)
        return mc_state(BLK_AIR, 0);
    return mc_get(c, x, TRB_SLICE_OY + y, z);
}

MC_HD static inline void trb_set(Chunk *c, int x, int y, int z, u16 s) {
    if (x < 0 || x >= TRB_SLICE_W || y < 0 || y >= TRB_SLICE_H || z < 0 || z >= TRB_SLICE_W)
        return;
    mc_set(c, x, TRB_SLICE_OY + y, z, s);
}

MC_HD static inline int trb_id(u16 s) { return mc_state_id(s); }

MC_HD static inline int trb_can_fall_through(u16 s) {
    int id = trb_id(s);
    if (id == BLK_AIR) return 1;
    if (id == 51) return 1;
    BptProps p = mc_bpt_props(id);
    return (p.flags & BF_LIQUID) != 0;
}

MC_HD static inline int trb_is_flammable(int id) {
    return id == BLK_PLANKS || id == BLK_LOG || id == BLK_LEAVES || id == 31;
}

MC_HD static inline void trb_init_aux(TrbAux *a) {
    int i;
    for (i = 0; i < TRB_SLICE_VOL; ++i) {
        a->light_above[i] = 15;
        a->block_light[i] = 0;
    }
}

MC_HD static inline void trb_init_fixtures(Chunk *c, TrbAux *a, u64 seed) {
    u16 grass = mc_state(BLK_GRASS, 0);
    u16 dirt = mc_state(BLK_DIRT, 0);
    u16 stone = mc_state(BLK_STONE, 0);
    u16 sand = mc_state(BLK_SAND, 0);
    u16 gravel = mc_state(BLK_GRAVEL, 0);
    u16 fire = mc_state(51, 0);
    u16 planks = mc_state(BLK_PLANKS, 0);
    u16 air = mc_state(BLK_AIR, 0);

    (void)seed;
    trb_init_aux(a);

    /* block_tickers: grass decay under stone */
    trb_set(c, 8, 10, 8, grass);
    trb_set(c, 8, 11, 8, stone);
    a->light_above[trb_lidx(8, 10, 8)] = 0;
    /* grass spread onto dirt */
    trb_set(c, 4, 10, 4, dirt);
    trb_set(c, 4, 11, 4, air);
    a->light_above[trb_lidx(4, 10, 4)] = 12;
    trb_set(c, 3, 10, 4, grass);
    /* sand/gravel fall column */
    trb_set(c, 12, 8, 4, sand);
    trb_set(c, 12, 7, 4, sand);
    trb_set(c, 12, 6, 4, gravel);
    /* fire spread onto planks */
    trb_set(c, 2, 10, 2, fire);
    trb_set(c, 2, 10, 3, planks);
    trb_set(c, 3, 10, 2, planks);

    /* block_tickers_crops: wheat growth (separate cells from grass/fire) */
    trb_set(c, 10, 10, 10, btc_wheat_age(0));
    trb_set(c, 10, 9, 10, mc_state(BTC_BLK_FARMLAND, 7));
    a->light_above[trb_lidx(10, 10, 10)] = 15;
    trb_set(c, 4, 10, 12, btc_wheat_age(3));
    trb_set(c, 4, 9, 12, mc_state(BTC_BLK_FARMLAND, 7));
    trb_set(c, 4, 10, 11, btc_wheat_age(2));
    trb_set(c, 4, 9, 11, mc_state(BTC_BLK_FARMLAND, 7));
    trb_set(c, 5, 10, 12, btc_wheat_age(1));
    trb_set(c, 5, 9, 12, mc_state(BTC_BLK_FARMLAND, 7));
    a->light_above[trb_lidx(4, 10, 12)] = 12;
    a->light_above[trb_lidx(4, 10, 11)] = 12;
    a->light_above[trb_lidx(5, 10, 12)] = 12;
    trb_set(c, 12, 10, 12, btc_wheat_age(4));
    trb_set(c, 12, 9, 12, mc_state(BLK_STONE, 0));
    a->light_above[trb_lidx(12, 10, 12)] = 15;
    /* ice melt threshold */
    trb_set(c, 14, 10, 2, mc_state(BLK_ICE, 0));
    a->block_light[trb_lidx(14, 10, 2)] = 9;
    trb_set(c, 15, 10, 2, mc_state(BLK_ICE, 0));
    a->block_light[trb_lidx(15, 10, 2)] = 8;
    /* snow melt threshold */
    trb_set(c, 6, 10, 6, btc_snow_with_layers(4));
    a->block_light[trb_lidx(6, 10, 6)] = 12;
    trb_set(c, 7, 10, 6, btc_snow_with_layers(2));
    a->block_light[trb_lidx(7, 10, 6)] = 11;

    /* stone floor under slice fixtures */
    {
        int x, y, z;
        for (y = 0; y < 9; ++y)
            for (z = 0; z < TRB_SLICE_W; ++z)
                for (x = 0; x < TRB_SLICE_W; ++x)
                    if (trb_get(c, x, y, z) == air)
                        trb_set(c, x, y, z, stone);
    }
}

MC_HD static inline void trb_init_env(Env *e, TrbAux *aux, u64 seed) {
    twc_init_env(e, seed);
    trb_init_fixtures(&twc_now(e)->chunk[0], aux, seed);
    twc_copy_world(twc_next(e), twc_now(e));
}

MC_HD static inline void trb_tick_grass(const Chunk *now, Chunk *next, TrbAux *aux,
                                        u64 seed, i64 tick, int x, int y, int z) {
    u16 s = trb_get(now, x, y, z);
    if (trb_id(s) != BLK_GRASS) return;
    {
        int la = (int)aux->light_above[trb_lidx(x, y, z)];
        u16 above = trb_get(now, x, y + 1, z);
        BptProps ap = mc_bpt_props(trb_id(above));
        if (la < 4 && ap.light_opacity > 2) {
            trb_set(next, x, y, z, mc_state(BLK_DIRT, 0));
            return;
        }
        if (la < 9) return;
        {
            int i;
            for (i = 0; i < 4; ++i) {
                u64 h = mc_hash_seed(seed, tick, x, TRB_SLICE_OY + y, z, BT_PURPOSE_GRASS);
                i32 dx, dy, dz;
                h = mc_hash64(h ^ (u64)i);
                dx = mc_hash_bound(h, 3) - 1;
                h = mc_hash64(h + 1);
                dy = mc_hash_bound(h, 5) - 3;
                h = mc_hash64(h + 2);
                dz = mc_hash_bound(h, 3) - 1;
                {
                    int nx = x + dx, ny = y + dy, nz = z + dz;
                    u16 ns, ab;
                    BptProps abp;
                    int nla;
                    if (ny < 0 || ny >= TRB_SLICE_H) continue;
                    ns = trb_get(now, nx, ny, nz);
                    if (trb_id(ns) != BLK_DIRT) continue;
                    ab = trb_get(now, nx, ny + 1, nz);
                    abp = mc_bpt_props(trb_id(ab));
                    nla = (ny + 1 < TRB_SLICE_H) ? (int)aux->light_above[trb_lidx(nx, ny, nz)] : 15;
                    if (nla >= 4 && abp.light_opacity <= 2)
                        trb_set(next, nx, ny, nz, mc_state(BLK_GRASS, 0));
                }
            }
        }
    }
}

MC_HD static inline void trb_tick_falling(Chunk *next) {
    int y, z, x;
    for (y = 1; y < TRB_SLICE_H; ++y)
        for (z = 0; z < TRB_SLICE_W; ++z)
            for (x = 0; x < TRB_SLICE_W; ++x) {
                u16 s = trb_get(next, x, y, z);
                int id = trb_id(s);
                u16 below;
                int ly;
                if (id != BLK_SAND && id != BLK_GRAVEL) continue;
                below = trb_get(next, x, y - 1, z);
                if (!trb_can_fall_through(below)) continue;
                ly = y - 1;
                while (ly > 0 && trb_can_fall_through(trb_get(next, x, ly - 1, z))) --ly;
                trb_set(next, x, y, z, mc_state(BLK_AIR, 0));
                trb_set(next, x, ly, z, s);
            }
}

MC_HD static inline void trb_tick_fire(const Chunk *now, Chunk *next, u64 seed, i64 tick) {
    int z, y, x;
    static const int dx[] = {1, -1, 0, 0, 0, 0};
    static const int dy[] = {0, 0, -1, 1, 0, 0};
    static const int dz[] = {0, 0, 0, 0, 1, -1};
    for (z = 0; z < TRB_SLICE_W; ++z)
        for (y = 0; y < TRB_SLICE_H; ++y)
            for (x = 0; x < TRB_SLICE_W; ++x) {
                u16 s = trb_get(now, x, y, z);
                if (trb_id(s) != 51) continue;
                {
                    int f;
                    for (f = 0; f < 6; ++f) {
                        int nx = x + dx[f], ny = y + dy[f], nz = z + dz[f];
                        u16 ns = trb_get(now, nx, ny, nz);
                        int nid = trb_id(ns);
                        u64 h;
                        if (!trb_is_flammable(nid)) continue;
                        h = mc_hash_seed(seed, tick, nx, TRB_SLICE_OY + ny, nz, BT_PURPOSE_FIRE);
                        if (mc_hash_bound(h, 100) > 15) continue;
                        if (trb_get(next, nx, ny, nz) == mc_state(BLK_AIR, 0))
                            trb_set(next, nx, ny, nz, mc_state(51, 0));
                    }
                }
            }
}

MC_HD static inline int trb_crop_can_stay(TrbAux *aux, const Chunk *now, int x, int y, int z) {
    int la = (y + 1 < TRB_SLICE_H) ? (int)aux->light_above[trb_lidx(x, y, z)] : 15;
    u16 soil;
    if (la < 8) return 0;
    soil = trb_get(now, x, y - 1, z);
    return btc_is_farmland(soil);
}

MC_HD static inline void trb_tick_crop(TrbAux *aux, const Chunk *now, Chunk *next,
                                      u64 seed, i64 tick, int x, int y, int z) {
    u16 s = trb_get(now, x, y, z);
    int age, la, bound;
    float f;
    u64 h;
    if (trb_id(s) != BTC_BLK_WHEAT) return;
    if (!trb_crop_can_stay(aux, now, x, y, z)) {
        trb_set(next, x, y, z, mc_state(BLK_AIR, 0));
        return;
    }
    la = (int)aux->light_above[trb_lidx(x, y, z)];
    if (la < 9) return;
    age = btc_crop_age(s);
    if (age < 0 || age >= 7) return;
    {
        float gf = 1.0f;
        int i, j;
        for (i = -1; i <= 1; ++i) {
            for (j = -1; j <= 1; ++j) {
                float f1 = 0.0f;
                u16 soil = trb_get(now, x + i, y - 1, z + j);
                if (btc_is_farmland(soil)) {
                    f1 = 1.0f;
                    if (btc_farmland_fertile(soil)) f1 = 3.0f;
                }
                if (i != 0 || j != 0) f1 /= 4.0f;
                gf += f1;
            }
        }
        {
            int flag = (trb_id(trb_get(now, x - 1, y, z)) == BTC_BLK_WHEAT ||
                        trb_id(trb_get(now, x + 1, y, z)) == BTC_BLK_WHEAT);
            int flag1 = (trb_id(trb_get(now, x, y, z - 1)) == BTC_BLK_WHEAT ||
                         trb_id(trb_get(now, x, y, z + 1)) == BTC_BLK_WHEAT);
            if (flag && flag1) {
                gf /= 2.0f;
            } else {
                int flag2 = (trb_id(trb_get(now, x - 1, y, z - 1)) == BTC_BLK_WHEAT ||
                             trb_id(trb_get(now, x + 1, y, z - 1)) == BTC_BLK_WHEAT ||
                             trb_id(trb_get(now, x + 1, y, z + 1)) == BTC_BLK_WHEAT ||
                             trb_id(trb_get(now, x - 1, y, z + 1)) == BTC_BLK_WHEAT);
                if (flag2) gf /= 2.0f;
            }
        }
        f = gf;
    }
    bound = (int)(25.0f / f) + 1;
    if (bound < 1) bound = 1;
    h = mc_hash_seed(seed, tick, x, TRB_SLICE_OY + y, z, BTC_PURPOSE_CROP);
    if (mc_hash_bound(h, bound) == 0)
        trb_set(next, x, y, z, btc_wheat_age(age + 1));
}

MC_HD static inline void trb_tick_ice(TrbAux *aux, const Chunk *now, Chunk *next, int x, int y, int z) {
    u16 s = trb_get(now, x, y, z);
    int bl;
    BptProps p;
    if (trb_id(s) != BLK_ICE) return;
    bl = (int)aux->block_light[trb_lidx(x, y, z)];
    p = mc_bpt_props(BLK_ICE);
    if (bl > 11 - (int)p.light_opacity)
        trb_set(next, x, y, z, mc_state(BLK_WATER, 0));
}

MC_HD static inline void trb_tick_snow(TrbAux *aux, const Chunk *now, Chunk *next, int x, int y, int z) {
    u16 s = trb_get(now, x, y, z);
    int bl;
    if (trb_id(s) != BLK_SNOW_LAYER) return;
    bl = (int)aux->block_light[trb_lidx(x, y, z)];
    if (bl > 11)
        trb_set(next, x, y, z, mc_state(BLK_AIR, 0));
}

MC_HD static inline void trb_tick_slice(const Chunk *now, Chunk *next, TrbAux *aux,
                                        u64 seed, i64 tick) {
    int z, y, x;
    for (z = 0; z < TRB_SLICE_W; ++z)
        for (y = 0; y < TRB_SLICE_H; ++y)
            for (x = 0; x < TRB_SLICE_W; ++x) {
                trb_tick_grass(now, next, aux, seed, tick, x, y, z);
                trb_tick_crop(aux, now, next, seed, tick, x, y, z);
                trb_tick_ice(aux, now, next, x, y, z);
                trb_tick_snow(aux, now, next, x, y, z);
            }
    trb_tick_falling(next);
    trb_tick_fire(now, next, seed, tick);
}

MC_HD static inline void trb_tick_grass_at(const Chunk *now, Chunk *next, u64 seed, i64 tick,
                                           int x, int y, int z) {
    u16 s = mc_get(now, x, y, z);
    int id = trb_id(s);
    u16 above;
    BptProps ap;
    u8 sky;
    if (id != BLK_GRASS) return;
    above = mc_get(now, x, y + 1, z);
    ap = mc_bpt_props(trb_id(above));
    sky = now->light[mc_idx(x, y, z)];
    if (mc_light_sky(sky) < 4 && ap.light_opacity > 2) {
        mc_set(next, x, y, z, mc_state(BLK_DIRT, 0));
        return;
    }
    if (mc_light_sky(sky) < 9) return;
    {
        int i;
        for (i = 0; i < 4; ++i) {
            u64 h = mc_hash_seed(seed, tick, x, y, z, BT_PURPOSE_GRASS);
            i32 dx, dy, dz;
            h = mc_hash64(h ^ (u64)i);
            dx = mc_hash_bound(h, 3) - 1;
            h = mc_hash64(h + 1);
            dy = mc_hash_bound(h, 5) - 3;
            h = mc_hash64(h + 2);
            dz = mc_hash_bound(h, 3) - 1;
            {
                int nx = x + dx, ny = y + dy, nz = z + dz;
                u16 ns, ab;
                BptProps abp;
                u8 nsky;
                if (ny < 0 || ny >= MC_CY) continue;
                ns = mc_get(now, nx, ny, nz);
                if (trb_id(ns) != BLK_DIRT) continue;
                ab = mc_get(now, nx, ny + 1, nz);
                abp = mc_bpt_props(trb_id(ab));
                nsky = now->light[mc_idx(nx, ny, nz)];
                if (mc_light_sky(nsky) >= 4 && abp.light_opacity <= 2)
                    mc_set(next, nx, ny, nz, mc_state(BLK_GRASS, 0));
            }
        }
    }
}

MC_HD static inline void trb_random_attempts(const Chunk *now, Chunk *next, u64 seed, i64 tick) {
    int ri;
    for (ri = 0; ri < TRB_RANDOM_ATTEMPTS; ++ri) {
        u64 h = mc_hash_seed(seed, tick, ri, 0, 0, TRB_PURPOSE_RANDPOS);
        int x = (int)mc_hash_bound(h, MC_CX);
        int y, z;
        h = mc_hash64(h + 1ULL);
        y = (int)mc_hash_bound(h, MC_CY);
        h = mc_hash64(h + 2ULL);
        z = (int)mc_hash_bound(h, MC_CZ);
        trb_tick_grass_at(now, next, seed, tick, x, y, z);
    }
}

MC_HD static inline void trb_tick_env(Env *e, TrbAux *aux) {
    World *now = twc_now(e);
    World *next = twc_next(e);
    twc_copy_world(next, now);
    trb_tick_slice(&now->chunk[0], &next->chunk[0], aux, now->seed, now->tick);
    trb_random_attempts(&now->chunk[0], &next->chunk[0], now->seed, now->tick);
    next->tick = now->tick + 1;
    twc_swap(e);
}

typedef TwcEmitFn TrbEmitFn;

MC_HD static inline void trb_run(Env *e, TrbAux *aux, u64 seed, TrbEmitFn emit, void *ctx) {
    int t;
    trb_init_env(e, aux, seed);
    for (t = 0; t < TWC_NTICKS; ++t) {
        World *now;
        u64 tick_bits, block_hash, cur_bits;
        trb_tick_env(e, aux);
        now = twc_now(e);
        tick_bits = (u64)now->tick;
        block_hash = twc_blocks_hash(now);
        cur_bits = (u64)(u32)e->cur;
        if (emit) emit(tick_bits, block_hash, cur_bits, ctx);
    }
}

#endif /* MC_TICK_RANDOM_BLOCK_H */
