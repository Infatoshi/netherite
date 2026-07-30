/* block_tickers_crops: BlockCrops growth stages, BlockIce melt, BlockSnow melt.
 * PORT: BlockCrops.updateTick (+ BlockBush.checkAndDropBlock), BlockIce.updateTick,
 * BlockSnow.updateTick. Runtime randomness = mc_hash_rng (SPEC rule 1). Double-buffered
 * 16^2 x 32 cube. Synthetic block_light[] drives melt threshold (EnumSkyBlock.BLOCK). */
#ifndef MC_BLOCK_TICKERS_CROPS_H
#define MC_BLOCK_TICKERS_CROPS_H

#include "mc.h"
#include "mc_world.h"
#include "mc_rng.h"
#include "mc_blocks.h"
#include "block_props_table.h"

#define BTC_W 16
#define BTC_H 32
#define BTC_VOL (BTC_W * BTC_W * BTC_H)
#define BTC_NTICKS 24

#define BTC_BLK_WHEAT     59
#define BTC_BLK_FARMLAND  60

enum { BTC_PURPOSE_CROP = 1 };

typedef struct {
    u16 blocks_a[BTC_VOL];
    u16 blocks_b[BTC_VOL];
    u8  block_light[BTC_VOL]; /* synthetic block light 0..15 at cell */
    u8  light_above[BTC_VOL]; /* skylight at y+1 for crop growth (>=9 to grow) */
    int cur;
    u64 seed;
    i64 tick;
} BtcWorld;

MC_HD static inline int btc_idx(int x, int y, int z) {
    return (y * BTC_W + z) * BTC_W + x;
}

MC_HD static inline u16 *btc_now(BtcWorld *w) { return w->cur ? w->blocks_b : w->blocks_a; }
MC_HD static inline u16 *btc_next(BtcWorld *w) { return w->cur ? w->blocks_a : w->blocks_b; }

MC_HD static inline u16 btc_get(const u16 *b, int x, int y, int z) {
    if (x < 0 || x >= BTC_W || y < 0 || y >= BTC_H || z < 0 || z >= BTC_W) return mc_state(BLK_AIR, 0);
    return b[btc_idx(x, y, z)];
}

MC_HD static inline void btc_set(u16 *b, int x, int y, int z, u16 s) {
    if (x < 0 || x >= BTC_W || y < 0 || y >= BTC_H || z < 0 || z >= BTC_W) return;
    b[btc_idx(x, y, z)] = s;
}

MC_HD static inline int btc_id(u16 s) { return mc_state_id(s); }
MC_HD static inline int btc_meta(u16 s) { return mc_state_meta(s); }

MC_HD static inline void btc_copy(u16 *dst, const u16 *src) {
    for (int i = 0; i < BTC_VOL; ++i) dst[i] = src[i];
}

MC_HD static inline int btc_is_farmland(u16 s) { return btc_id(s) == BTC_BLK_FARMLAND; }

MC_HD static inline int btc_farmland_fertile(u16 s) {
    return btc_is_farmland(s) && btc_meta(s) > 0;
}

MC_HD static inline int btc_crop_age(u16 s) {
    return btc_id(s) == BTC_BLK_WHEAT ? btc_meta(s) : -1;
}

MC_HD static inline u16 btc_wheat_age(int age) {
    if (age < 0) age = 0;
    if (age > 7) age = 7;
    return mc_state(BTC_BLK_WHEAT, age);
}

MC_HD static inline int btc_snow_layers(u16 s) {
    if (btc_id(s) != BLK_SNOW_LAYER) return -1;
    return btc_meta(s) + 1;
}

MC_HD static inline u16 btc_snow_with_layers(int layers) {
    if (layers < 1) layers = 1;
    if (layers > 8) layers = 8;
    return mc_state(BLK_SNOW_LAYER, layers - 1);
}

/* BlockCrops.getGrowthChance (farmland sustain only; no grass/dirt bush soil). */
MC_HD static inline float btc_growth_chance(const u16 *now, int x, int y, int z) {
    float f = 1.0f;
    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
            float f1 = 0.0f;
            u16 soil = btc_get(now, x + i, y - 1, z + j);
            if (btc_is_farmland(soil)) {
                f1 = 1.0f;
                if (btc_farmland_fertile(soil)) f1 = 3.0f;
            }
            if (i != 0 || j != 0) f1 /= 4.0f;
            f += f1;
        }
    }
    int flag = (btc_id(btc_get(now, x - 1, y, z)) == BTC_BLK_WHEAT ||
                btc_id(btc_get(now, x + 1, y, z)) == BTC_BLK_WHEAT);
    int flag1 = (btc_id(btc_get(now, x, y, z - 1)) == BTC_BLK_WHEAT ||
                 btc_id(btc_get(now, x, y, z + 1)) == BTC_BLK_WHEAT);
    if (flag && flag1) {
        f /= 2.0f;
    } else {
        int flag2 = (btc_id(btc_get(now, x - 1, y, z - 1)) == BTC_BLK_WHEAT ||
                     btc_id(btc_get(now, x + 1, y, z - 1)) == BTC_BLK_WHEAT ||
                     btc_id(btc_get(now, x + 1, y, z + 1)) == BTC_BLK_WHEAT ||
                     btc_id(btc_get(now, x - 1, y, z + 1)) == BTC_BLK_WHEAT);
        if (flag2) f /= 2.0f;
    }
    return f;
}

MC_HD static inline int btc_crop_can_stay(BtcWorld *w, const u16 *now, int x, int y, int z) {
    int la = (y + 1 < BTC_H) ? (int)w->light_above[btc_idx(x, y, z)] : 15;
    if (la < 8) return 0;
    u16 soil = btc_get(now, x, y - 1, z);
    return btc_is_farmland(soil);
}

MC_HD static inline void btc_tick_crop(BtcWorld *w, const u16 *now, u16 *next, int x, int y, int z) {
    u16 s = btc_get(now, x, y, z);
    if (btc_id(s) != BTC_BLK_WHEAT) return;
    if (!btc_crop_can_stay(w, now, x, y, z)) {
        btc_set(next, x, y, z, mc_state(BLK_AIR, 0));
        return;
    }
    int la = (int)w->light_above[btc_idx(x, y, z)];
    if (la < 9) return;
    int age = btc_crop_age(s);
    if (age < 0 || age >= 7) return;
    float f = btc_growth_chance(now, x, y, z);
    int bound = (int)(25.0f / f) + 1;
    if (bound < 1) bound = 1;
    u64 h = mc_hash_seed(w->seed, w->tick, x, y, z, BTC_PURPOSE_CROP);
    if (mc_hash_bound(h, bound) == 0)
        btc_set(next, x, y, z, btc_wheat_age(age + 1));
}

MC_HD static inline void btc_tick_ice(BtcWorld *w, const u16 *now, u16 *next, int x, int y, int z) {
    (void)w;
    u16 s = btc_get(now, x, y, z);
    if (btc_id(s) != BLK_ICE) return;
    int bl = (int)w->block_light[btc_idx(x, y, z)];
    BptProps p = mc_bpt_props(BLK_ICE);
    if (bl > 11 - (int)p.light_opacity)
        btc_set(next, x, y, z, mc_state(BLK_WATER, 0));
}

MC_HD static inline void btc_tick_snow(BtcWorld *w, const u16 *now, u16 *next, int x, int y, int z) {
    (void)w;
    u16 s = btc_get(now, x, y, z);
    if (btc_id(s) != BLK_SNOW_LAYER) return;
    int bl = (int)w->block_light[btc_idx(x, y, z)];
    if (bl > 11)
        btc_set(next, x, y, z, mc_state(BLK_AIR, 0));
}

MC_HD static inline void btc_init(BtcWorld *w, u64 seed) {
    w->cur = 0; w->seed = seed; w->tick = 0;
    u16 air = mc_state(BLK_AIR, 0);
    u16 *b = w->blocks_a;
    for (int i = 0; i < BTC_VOL; ++i) {
        b[i] = air;
        w->block_light[i] = 0;
        w->light_above[i] = 15;
    }
    /* wheat growth: age 0 on wet farmland, bright light */
    btc_set(b, 8, 10, 8, btc_wheat_age(0));
    btc_set(b, 8, 9, 8, mc_state(BTC_BLK_FARMLAND, 7));
    w->light_above[btc_idx(8, 10, 8)] = 15;
    /* wheat cross-pattern penalty: age 3, neighbors N+E */
    btc_set(b, 4, 10, 4, btc_wheat_age(3));
    btc_set(b, 4, 9, 4, mc_state(BTC_BLK_FARMLAND, 7));
    btc_set(b, 4, 10, 3, btc_wheat_age(2));
    btc_set(b, 4, 9, 3, mc_state(BTC_BLK_FARMLAND, 7));
    btc_set(b, 5, 10, 4, btc_wheat_age(1));
    btc_set(b, 5, 9, 4, mc_state(BTC_BLK_FARMLAND, 7));
    w->light_above[btc_idx(4, 10, 4)] = 12;
    w->light_above[btc_idx(4, 10, 3)] = 12;
    w->light_above[btc_idx(5, 10, 4)] = 12;
    /* crop drop: wheat over stone (no farmland) */
    btc_set(b, 12, 10, 12, btc_wheat_age(4));
    btc_set(b, 12, 9, 12, mc_state(BLK_STONE, 0));
    w->light_above[btc_idx(12, 10, 12)] = 15;
    /* ice melt: block light 9 melts (threshold > 8), 8 does not */
    btc_set(b, 2, 10, 2, mc_state(BLK_ICE, 0));
    w->block_light[btc_idx(2, 10, 2)] = 9;
    btc_set(b, 3, 10, 2, mc_state(BLK_ICE, 0));
    w->block_light[btc_idx(3, 10, 2)] = 8;
    /* snow melt: block light 12 melts (>11), 11 does not */
    btc_set(b, 6, 10, 6, btc_snow_with_layers(4));
    w->block_light[btc_idx(6, 10, 6)] = 12;
    btc_set(b, 7, 10, 6, btc_snow_with_layers(2));
    w->block_light[btc_idx(7, 10, 6)] = 11;
    btc_copy(w->blocks_b, w->blocks_a);
}

MC_HD static inline void btc_tick(BtcWorld *w) {
    const u16 *now = btc_now(w);
    u16 *next = btc_next(w);
    btc_copy(next, now);
    for (int z = 0; z < BTC_W; ++z)
        for (int y = 0; y < BTC_H; ++y)
            for (int x = 0; x < BTC_W; ++x) {
                btc_tick_crop(w, now, next, x, y, z);
                btc_tick_ice(w, now, next, x, y, z);
                btc_tick_snow(w, now, next, x, y, z);
            }
    w->tick++;
    w->cur ^= 1;
}

MC_HD static inline void btc_run(BtcWorld *w) {
    btc_init(w, w->seed);
    for (int t = 0; t < BTC_NTICKS; ++t) btc_tick(w);
}

#endif /* MC_BLOCK_TICKERS_CROPS_H */
