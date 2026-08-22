/* randtick_live.h - live random-tick pass matching magma/game/randtick.c.
 *
 * Include after defining:
 *   RT_W              world pointer type
 *   rt_live_id(w,x,y,z)
 *   rt_live_meta(w,x,y,z)     meta 0..15
 *   rt_live_light(w,x,y,z)    max(sky, block) at the cell
 *   rt_live_set(w,x,y,z,id,meta)
 *
 * Magma live/RL substitutes the Java updateLCG + World.rand stream with
 * mc_hash_seed (randtick.h:10). Blaze must match that schedule for M1, not
 * the Java LCG. Java citations document the oracle; hash keys are magma's.
 *
 * Java:
 *   World.java:95-97 updateLCG / DIST_HASH_MAGIC 1013904223
 *   World.java:108 World.rand
 *   WorldServer.java:404 randomTickSpeed
 *   WorldServer.java:472-494 per-section LCG pick + Block.randomTick
 *   GameRules.java:13 doFireTick, :25 randomTickSpeed "3"
 *   Block.java:434 getTickRandomly, :595 randomTick -> updateTick
 *   ExtendedBlockStorage.java:86 getNeedsRandomTick
 *   BlockGrass.java:41-73, BlockCrops.java:72-90 / :111-164
 *   BlockFire.java:146-253 / :286-314, BlockLeaves.java:69-176
 *
 * Ported tickers (magma/game/randtick.c dispatch): grass, leaves/leaves2,
 * fire, wheat/carrot/potato. Sapling/farmland/ice/snow stay unported.
 */
#ifndef MC_RANDTICK_LIVE_H
#define MC_RANDTICK_LIVE_H

#include <string.h>

#include "mc.h"
#include "mc_rng.h"
#include "mc_blocks.h"
#include "block_props_table.h"
#include "mc_gamerules.h"

#ifndef RT_W
#error "randtick_live.h requires RT_W and rt_live_* accessors"
#endif

enum {
    RT_PURPOSE_POS   = 0x5254504Fu, /* 'RTPO' section cell pick */
    RT_PURPOSE_GRASS = 0x52544752u, /* 'RTGR' grass spread offsets */
    RT_PURPOSE_FIRE  = 0x52544652u, /* 'RTFR' fire age/spread stream */
    RT_PURPOSE_CROP  = 0x52544352u  /* 'RTCR' crop growth roll */
};

#define RT_BLK_FIRE     51
#define RT_BLK_WHEAT    59
#define RT_BLK_FARMLAND 60
#define RT_BLK_CARROT   141
#define RT_BLK_POTATO   142
#define RT_BLK_LEAVES   18
#define RT_BLK_LEAVES2  161
#define RT_BLK_LOG      17
#define RT_BLK_LOG2     162
#define RT_LIVE_SURR    32768

MC_HD static inline int rt_live_opacity(int id) {
    if (id == 0) return 0;
    return (int)mc_bpt_props(id).light_opacity;
}

MC_HD static inline int rt_live_fully_opaque(int id) {
    BptProps p;
    if (id == 0) return 0;
    p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) && !(p.flags & BF_LIQUID) && p.light_opacity >= 255;
}

/* BlockFire.init setFireInfo tables, magma/game/randtick.c:50-88. */
MC_HD static inline int rt_live_fire_flammability(int id) {
    switch (id) {
        case 5:
        case 125: case 126:
        case 107: case 183: case 184: case 185: case 186: case 187:
        case 85: case 188: case 189: case 190: case 191: case 192:
        case 53: case 134: case 135: case 136: case 163: case 164:
            return 20;
        case 17: case 162: return 5;
        case 18: case 161: return 60;
        case 47: return 20;
        case 46: return 100;
        case 31: case 175: case 37: case 38: case 32: return 100;
        case 35: return 60;
        case 106: return 100;
        case 173: return 5;
        case 170: return 20;
        case 171: return 20;
        default: return 0;
    }
}

MC_HD static inline int rt_live_fire_encouragement(int id) {
    switch (id) {
        case 5:
        case 125: case 126:
        case 107: case 183: case 184: case 185: case 186: case 187:
        case 85: case 188: case 189: case 190: case 191: case 192:
        case 53: case 134: case 135: case 136: case 163: case 164:
        case 17: case 162: case 173:
            return 5;
        case 18: case 161: case 35: case 47: return 30;
        case 46: case 106: return 15;
        case 31: case 175: case 37: case 38: case 32:
        case 170: case 171:
            return 60;
        default: return 0;
    }
}

MC_HD static inline int rt_live_can_catch_fire(RT_W *w, int x, int y, int z) {
    return rt_live_fire_flammability(rt_live_id(w, x, y, z)) > 0;
}

MC_HD static inline int rt_live_can_neighbor_catch_fire(RT_W *w, int x, int y, int z) {
    static const int dx[] = {1, -1, 0, 0, 0, 0};
    static const int dy[] = {0, 0, -1, 1, 0, 0};
    static const int dz[] = {0, 0, 0, 0, 1, -1};
    int f;
    for (f = 0; f < 6; ++f)
        if (rt_live_can_catch_fire(w, x + dx[f], y + dy[f], z + dz[f])) return 1;
    return 0;
}

MC_HD static inline int rt_live_neighbor_encouragement(RT_W *w, int x, int y, int z) {
    static const int dx[] = {1, -1, 0, 0, 0, 0};
    static const int dy[] = {0, 0, -1, 1, 0, 0};
    static const int dz[] = {0, 0, 0, 0, 1, -1};
    int f, best = 0, e;
    if (rt_live_id(w, x, y, z) != 0) return 0;
    for (f = 0; f < 6; ++f) {
        e = rt_live_fire_encouragement(rt_live_id(w, x + dx[f], y + dy[f], z + dz[f]));
        if (e > best) best = e;
    }
    return best;
}

MC_HD static inline void rt_live_fire_rng(JavaRandom *r, long long seed, long long tick,
                                          int x, int y, int z) {
    u64 h = mc_hash_seed((u64)seed, tick, x, y, z, RT_PURPOSE_FIRE);
    jrand_set(r, (i64)h);
}

/* BlockFire.tryCatchFire :286-314; humidity/rain omitted (clear weather). */
MC_HD static inline void rt_live_try_catch_fire(RT_W *w, JavaRandom *rng, int x, int y, int z,
                                                int chance, int age) {
    int flam = rt_live_fire_flammability(rt_live_id(w, x, y, z));
    if (jrand_int_bound(rng, chance) < flam) {
        if (jrand_int_bound(rng, age + 10) < 5) {
            int j = age + jrand_int_bound(rng, 5) / 4;
            if (j > 15) j = 15;
            rt_live_set(w, x, y, z, RT_BLK_FIRE, j);
        } else {
            rt_live_set(w, x, y, z, 0, 0);
        }
    }
}

/* BlockFire.updateTick :146-253 overworld, clear weather, NORMAL=2. */
MC_HD MC_NOINLINE static void rt_live_tick_fire(RT_W *w, int x, int y, int z,
                                                long long seed, long long tick,
                                                const McGameRules *gr) {
    JavaRandom rng;
    int age, k, l, i1;
    if (!gr || !gr->doFireTick) return;
    if (rt_live_id(w, x, y, z) != RT_BLK_FIRE) return;

    rt_live_fire_rng(&rng, seed, tick, x, y, z);

    if (!rt_live_fully_opaque(rt_live_id(w, x, y - 1, z)) &&
        !rt_live_can_neighbor_catch_fire(w, x, y, z)) {
        rt_live_set(w, x, y, z, 0, 0);
        return;
    }

    age = rt_live_meta(w, x, y, z) & 15;
    if (age < 15) {
        int na = age + jrand_int_bound(&rng, 3) / 2;
        if (na > 15) na = 15;
        rt_live_set(w, x, y, z, RT_BLK_FIRE, na);
        age = na;
    }

    if (!rt_live_can_neighbor_catch_fire(w, x, y, z)) {
        if (!rt_live_fully_opaque(rt_live_id(w, x, y - 1, z)) || age > 3)
            rt_live_set(w, x, y, z, 0, 0);
        return;
    }
    if (!rt_live_can_catch_fire(w, x, y - 1, z) && age == 15 &&
        jrand_int_bound(&rng, 4) == 0) {
        rt_live_set(w, x, y, z, 0, 0);
        return;
    }

    rt_live_try_catch_fire(w, &rng, x + 1, y, z, 300, age);
    rt_live_try_catch_fire(w, &rng, x - 1, y, z, 300, age);
    rt_live_try_catch_fire(w, &rng, x, y - 1, z, 250, age);
    rt_live_try_catch_fire(w, &rng, x, y + 1, z, 250, age);
    rt_live_try_catch_fire(w, &rng, x, y, z + 1, 300, age);
    rt_live_try_catch_fire(w, &rng, x, y, z - 1, 300, age);

    for (k = -1; k <= 1; ++k)
        for (l = -1; l <= 1; ++l)
            for (i1 = -1; i1 <= 4; ++i1) {
                int j1, k1, l1, i2;
                if (k == 0 && i1 == 0 && l == 0) continue;
                j1 = 100;
                if (i1 > 1) j1 += (i1 - 1) * 100;
                k1 = rt_live_neighbor_encouragement(w, x + k, y + i1, z + l);
                if (k1 <= 0) continue;
                l1 = (k1 + 40 + 2 * 7) / (age + 30);
                if (l1 > 0 && jrand_int_bound(&rng, j1) <= l1) {
                    i2 = age + jrand_int_bound(&rng, 5) / 4;
                    if (i2 > 15) i2 = 15;
                    rt_live_set(w, x + k, y + i1, z + l, RT_BLK_FIRE, i2);
                }
            }
}

/* BlockGrass.updateTick :41-73. Magma hashes the four spread draws. */
MC_HD static inline void rt_live_tick_grass(RT_W *w, int x, int y, int z,
                                            long long seed, long long tick) {
    int above_id, light_up, i;
    if (rt_live_id(w, x, y, z) != BLK_GRASS) return;
    above_id = rt_live_id(w, x, y + 1, z);
    light_up = rt_live_light(w, x, y + 1, z);

    if (light_up < 4 && rt_live_opacity(above_id) > 2) {
        rt_live_set(w, x, y, z, BLK_DIRT, 0);
        return;
    }
    if (light_up < 9) return;

    for (i = 0; i < 4; ++i) {
        u64 h = mc_hash_seed((u64)seed, tick, x, y, z, RT_PURPOSE_GRASS);
        i32 dx, dy, dz;
        int nx, ny, nz, tid, tmeta, a_id;
        h = mc_hash64(h ^ (u64)i);
        dx = mc_hash_bound(h, 3) - 1;
        h = mc_hash64(h + 1);
        dy = mc_hash_bound(h, 5) - 3;
        h = mc_hash64(h + 2);
        dz = mc_hash_bound(h, 3) - 1;
        nx = x + dx; ny = y + dy; nz = z + dz;
        if (ny < 0 || ny >= 256) continue;
        tid = rt_live_id(w, nx, ny, nz);
        tmeta = rt_live_meta(w, nx, ny, nz) & 15;
        if (tid != BLK_DIRT || tmeta != 0) continue;
        a_id = rt_live_id(w, nx, ny + 1, nz);
        if (rt_live_light(w, nx, ny + 1, nz) >= 4 && rt_live_opacity(a_id) <= 2)
            rt_live_set(w, nx, ny, nz, BLK_GRASS, 0);
    }
}

MC_HD static inline int rt_live_is_leaves(int id) {
    return id == RT_BLK_LEAVES || id == RT_BLK_LEAVES2;
}

MC_HD static inline int rt_live_is_log(int id) {
    return id == RT_BLK_LOG || id == RT_BLK_LOG2;
}

/* BlockLeaves.updateTick :69-176. surroundings is Java `new int[32768]`. */
MC_HD MC_NOINLINE static void rt_live_tick_leaves(RT_W *w, int x, int y, int z,
                                                  int *surroundings) {
    int id = rt_live_id(w, x, y, z);
    int meta, check_decay, decayable;
    int i2, j2, k2, i3, j3, k3, l3, l2;

    if (!surroundings) return;
    if (!rt_live_is_leaves(id)) return;
    meta = rt_live_meta(w, x, y, z) & 15;
    check_decay = (meta & 8) != 0;
    decayable = (meta & 4) == 0;
    if (!check_decay || !decayable) return;

    memset(surroundings, 0, (size_t)RT_LIVE_SURR * sizeof(int));

    for (i2 = -4; i2 <= 4; ++i2)
        for (j2 = -4; j2 <= 4; ++j2)
            for (k2 = -4; k2 <= 4; ++k2) {
                int bid = rt_live_id(w, x + i2, y + j2, z + k2);
                int idx = (i2 + 16) * 1024 + (j2 + 16) * 32 + (k2 + 16);
                if (rt_live_is_log(bid))
                    surroundings[idx] = 0;
                else if (rt_live_is_leaves(bid))
                    surroundings[idx] = -2;
                else
                    surroundings[idx] = -1;
            }

    for (i3 = 1; i3 <= 4; ++i3)
        for (j3 = -4; j3 <= 4; ++j3)
            for (k3 = -4; k3 <= 4; ++k3)
                for (l3 = -4; l3 <= 4; ++l3) {
                    int base = (j3 + 16) * 1024 + (k3 + 16) * 32 + (l3 + 16);
                    if (surroundings[base] != i3 - 1) continue;
                    if (surroundings[(j3 + 16 - 1) * 1024 + (k3 + 16) * 32 + l3 + 16] == -2)
                        surroundings[(j3 + 16 - 1) * 1024 + (k3 + 16) * 32 + l3 + 16] = i3;
                    if (surroundings[(j3 + 16 + 1) * 1024 + (k3 + 16) * 32 + l3 + 16] == -2)
                        surroundings[(j3 + 16 + 1) * 1024 + (k3 + 16) * 32 + l3 + 16] = i3;
                    if (surroundings[(j3 + 16) * 1024 + (k3 + 16 - 1) * 32 + l3 + 16] == -2)
                        surroundings[(j3 + 16) * 1024 + (k3 + 16 - 1) * 32 + l3 + 16] = i3;
                    if (surroundings[(j3 + 16) * 1024 + (k3 + 16 + 1) * 32 + l3 + 16] == -2)
                        surroundings[(j3 + 16) * 1024 + (k3 + 16 + 1) * 32 + l3 + 16] = i3;
                    if (surroundings[(j3 + 16) * 1024 + (k3 + 16) * 32 + (l3 + 16 - 1)] == -2)
                        surroundings[(j3 + 16) * 1024 + (k3 + 16) * 32 + (l3 + 16 - 1)] = i3;
                    if (surroundings[(j3 + 16) * 1024 + (k3 + 16) * 32 + l3 + 16 + 1] == -2)
                        surroundings[(j3 + 16) * 1024 + (k3 + 16) * 32 + l3 + 16 + 1] = i3;
                }

    l2 = surroundings[16912];
    if (l2 >= 0)
        rt_live_set(w, x, y, z, id, meta & ~8);
    else
        rt_live_set(w, x, y, z, 0, 0);
}

MC_HD static inline int rt_live_is_crop(int id) {
    return id == RT_BLK_WHEAT || id == RT_BLK_CARROT || id == RT_BLK_POTATO;
}

/* BlockCrops.getGrowthChance :111-164; farmland-only sustain as magma. */
MC_HD static inline float rt_live_growth_chance(RT_W *w, int x, int y, int z, int crop_id) {
    float f = 1.0f;
    int i, j;
    for (i = -1; i <= 1; ++i) {
        for (j = -1; j <= 1; ++j) {
            float f1 = 0.0f;
            int soil = rt_live_id(w, x + i, y - 1, z + j);
            int sm = rt_live_meta(w, x + i, y - 1, z + j) & 15;
            if (soil == RT_BLK_FARMLAND) {
                f1 = 1.0f;
                if (sm > 0) f1 = 3.0f;
            }
            if (i != 0 || j != 0) f1 /= 4.0f;
            f += f1;
        }
    }
    {
        int flag = (rt_live_id(w, x - 1, y, z) == crop_id ||
                    rt_live_id(w, x + 1, y, z) == crop_id);
        int flag1 = (rt_live_id(w, x, y, z - 1) == crop_id ||
                     rt_live_id(w, x, y, z + 1) == crop_id);
        if (flag && flag1) {
            f /= 2.0f;
        } else {
            int flag2 = (rt_live_id(w, x - 1, y, z - 1) == crop_id ||
                         rt_live_id(w, x + 1, y, z - 1) == crop_id ||
                         rt_live_id(w, x + 1, y, z + 1) == crop_id ||
                         rt_live_id(w, x - 1, y, z + 1) == crop_id);
            if (flag2) f /= 2.0f;
        }
    }
    return f;
}

/* BlockCrops.updateTick :72-90. Magma hashes the growth roll. */
MC_HD static inline void rt_live_tick_crop(RT_W *w, int x, int y, int z,
                                           long long seed, long long tick) {
    int id = rt_live_id(w, x, y, z);
    int age, soil, bound;
    float gf;
    u64 h;
    if (!rt_live_is_crop(id)) return;
    soil = rt_live_id(w, x, y - 1, z);
    if (soil != RT_BLK_FARMLAND) {
        rt_live_set(w, x, y, z, 0, 0);
        return;
    }
    if (rt_live_light(w, x, y + 1, z) < 9) return;
    age = rt_live_meta(w, x, y, z) & 15;
    if (age >= 7) return;
    gf = rt_live_growth_chance(w, x, y, z, id);
    bound = (int)(25.0f / gf) + 1;
    if (bound < 1) bound = 1;
    h = mc_hash_seed((u64)seed, tick, x, y, z, RT_PURPOSE_CROP);
    if (mc_hash_bound(h, bound) == 0)
        rt_live_set(w, x, y, z, id, age + 1);
}

MC_HD static inline void rt_live_tick_block(RT_W *w, int wx, int wy, int wz,
                                            long long seed, long long tick,
                                            const McGameRules *gr,
                                            int *leaf_surr) {
    int id;
    McGameRules def;
    if (!w) return;
    if (!gr) { def = mc_gamerules_default(); gr = &def; }
    if (wy < 0 || wy >= 256) return;
    id = rt_live_id(w, wx, wy, wz);
    switch (id) {
        case BLK_GRASS:
            rt_live_tick_grass(w, wx, wy, wz, seed, tick);
            break;
        case RT_BLK_LEAVES:
        case RT_BLK_LEAVES2:
            rt_live_tick_leaves(w, wx, wy, wz, leaf_surr);
            break;
        case RT_BLK_FIRE:
            rt_live_tick_fire(w, wx, wy, wz, seed, tick, gr);
            break;
        case RT_BLK_WHEAT:
        case RT_BLK_CARROT:
        case RT_BLK_POTATO:
            rt_live_tick_crop(w, wx, wy, wz, seed, tick);
            break;
        default:
            break;
    }
}

/* WorldServer.updateBlocks randomTick loop :474-494, magma hash schedule.
 * radius is Chebyshev chunk radius around (ccx,ccz). */
MC_HD static inline void rt_live_pass(RT_W *w, long long seed, long long tick,
                                      int ccx, int ccz, int radius,
                                      const McGameRules *gr, int *leaf_surr) {
    McGameRules def;
    int rts, cx, cz, sec, att;
    if (!w) return;
    if (!gr) { def = mc_gamerules_default(); gr = &def; }
    rts = gr->randomTickSpeed;
    if (rts <= 0) return;
    if (radius < 0) radius = 0;

    for (cz = ccz - radius; cz <= ccz + radius; ++cz) {
        for (cx = ccx - radius; cx <= ccx + radius; ++cx) {
            for (sec = 0; sec < 16; ++sec) {
                int base_y = sec * 16;
                for (att = 0; att < rts; ++att) {
                    u64 h = mc_hash_seed((u64)seed, tick, cx, sec, cz,
                                         RT_PURPOSE_POS ^ (u32)att);
                    int lx = (int)mc_hash_bound(h, 16);
                    int ly, lz, wx, wy, wz;
                    h = mc_hash64(h + 1ULL);
                    ly = (int)mc_hash_bound(h, 16);
                    h = mc_hash64(h + 2ULL);
                    lz = (int)mc_hash_bound(h, 16);
                    wx = cx * 16 + lx;
                    wy = base_y + ly;
                    wz = cz * 16 + lz;
                    rt_live_tick_block(w, wx, wy, wz, seed, tick, gr, leaf_surr);
                }
            }
        }
    }
}

#endif /* MC_RANDTICK_LIVE_H */
