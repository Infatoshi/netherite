/* game/randtick.c - live random block ticks (see randtick.h). */
#include "game/randtick.h"

#include "mc_blocks.h"
#include "mc_rng.h"
#include "block_props_table.h"

#include <string.h>

/* Purpose tags for mc_hash_seed streams (distinct from BT_PURPOSE_* in blaze). */
enum {
    RT_PURPOSE_POS    = 0x5254504Fu, /* 'RTPO' section cell pick */
    RT_PURPOSE_GRASS  = 0x52544752u, /* 'RTGR' grass spread offsets */
    RT_PURPOSE_FIRE   = 0x52544652u, /* 'RTFR' fire age/spread stream */
    RT_PURPOSE_CROP   = 0x52544352u  /* 'RTCR' crop growth roll */
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

/* ---- light / opacity helpers ---- */

static int rt_light_at(const GmWorld *w, int x, int y, int z) {
    int sky = gm_world_sky_light(w, x, y, z);
    int blk = gm_world_block_light(w, x, y, z);
    return sky > blk ? sky : blk;
}

static int rt_opacity(int id) {
    if (id == 0) return 0;
    return (int)mc_bpt_props(id).light_opacity;
}

static int rt_is_fully_opaque(int id) {
    BptProps p;
    if (id == 0) return 0;
    p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) && !(p.flags & BF_LIQUID) && p.light_opacity >= 255;
}

/* ---- BlockFire flammability / encouragement (BlockFire.init setFireInfo) ---- */

static int rt_fire_flammability(int id) {
    switch (id) {
        case 5: /* planks */
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

static int rt_fire_encouragement(int id) {
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

static int rt_can_catch_fire(const GmWorld *w, int x, int y, int z) {
    return rt_fire_flammability(gm_world_block(w, x, y, z)) > 0;
}

static int rt_can_neighbor_catch_fire(const GmWorld *w, int x, int y, int z) {
    static const int dx[] = {1, -1, 0, 0, 0, 0};
    static const int dy[] = {0, 0, -1, 1, 0, 0};
    static const int dz[] = {0, 0, 0, 0, 1, -1};
    int f;
    for (f = 0; f < 6; ++f)
        if (rt_can_catch_fire(w, x + dx[f], y + dy[f], z + dz[f])) return 1;
    return 0;
}

static int rt_neighbor_encouragement(const GmWorld *w, int x, int y, int z) {
    static const int dx[] = {1, -1, 0, 0, 0, 0};
    static const int dy[] = {0, 0, -1, 1, 0, 0};
    static const int dz[] = {0, 0, 0, 0, 1, -1};
    int f, best = 0, e;
    if (gm_world_block(w, x, y, z) != 0) return 0;
    for (f = 0; f < 6; ++f) {
        e = rt_fire_encouragement(gm_world_block(w, x + dx[f], y + dy[f], z + dz[f]));
        if (e > best) best = e;
    }
    return best;
}

/* Per-cell JavaRandom stream for multi-draw fire (age + tryCatchFire + spread). */
static void rt_fire_rng(JavaRandom *r, long long seed, long long tick,
                        int x, int y, int z) {
    u64 h = mc_hash_seed((u64)seed, tick, x, y, z, RT_PURPOSE_FIRE);
    jrand_set(r, (i64)h);
}

/* BlockFire.tryCatchFire: always draws nextInt(chance); on catch, fire or air. */
static void rt_try_catch_fire(GmWorld *w, JavaRandom *rng, int x, int y, int z,
                              int chance, int age) {
    int flam = rt_fire_flammability(gm_world_block(w, x, y, z));
    if (jrand_int_bound(rng, chance) < flam) {
        if (jrand_int_bound(rng, age + 10) < 5) {
            int j = age + jrand_int_bound(rng, 5) / 4;
            if (j > 15) j = 15;
            gm_world_set_block_meta(w, x, y, z, RT_BLK_FIRE, j);
        } else {
            gm_world_set_block(w, x, y, z, 0);
        }
    }
}

/* BlockFire.updateTick (overworld, clear weather, normal difficulty). */
static void rt_tick_fire(GmWorld *w, int x, int y, int z,
                         long long seed, long long tick, const McGameRules *gr) {
    JavaRandom rng;
    int age, k, l, i1;
    if (!gr || !gr->doFireTick) return;
    if (gm_world_block(w, x, y, z) != RT_BLK_FIRE) return;

    rt_fire_rng(&rng, seed, tick, x, y, z);

    /* canPlaceBlockAt: opaque below OR flammable neighbor. */
    if (!rt_is_fully_opaque(gm_world_block(w, x, y - 1, z)) &&
        !rt_can_neighbor_catch_fire(w, x, y, z)) {
        gm_world_set_block(w, x, y, z, 0);
        return;
    }

    age = gm_world_meta(w, x, y, z) & 15;
    /* isFireSource(below): false for overworld fuels (netherrack/magma unported). */
    /* rain branch short-circuits when not raining (live clear weather). */

    if (age < 15) {
        int na = age + jrand_int_bound(&rng, 3) / 2;
        if (na > 15) na = 15;
        gm_world_set_block_meta(w, x, y, z, RT_BLK_FIRE, na);
        age = na;
    }
    /* scheduleUpdate omitted: live path re-enters via random ticks. */

    if (!rt_can_neighbor_catch_fire(w, x, y, z)) {
        if (!rt_is_fully_opaque(gm_world_block(w, x, y - 1, z)) || age > 3)
            gm_world_set_block(w, x, y, z, 0);
        return;
    }
    if (!rt_can_catch_fire(w, x, y - 1, z) && age == 15 &&
        jrand_int_bound(&rng, 4) == 0) {
        gm_world_set_block(w, x, y, z, 0);
        return;
    }

    /* tryCatchFire E W D U N S (chances 300/250, humidity 0). */
    rt_try_catch_fire(w, &rng, x + 1, y, z, 300, age);
    rt_try_catch_fire(w, &rng, x - 1, y, z, 300, age);
    rt_try_catch_fire(w, &rng, x, y - 1, z, 250, age);
    rt_try_catch_fire(w, &rng, x, y + 1, z, 250, age);
    rt_try_catch_fire(w, &rng, x, y, z + 1, 300, age);
    rt_try_catch_fire(w, &rng, x, y, z - 1, 300, age);

    /* spread into air in the -1..+1 (x,z) x -1..+4 (y) box. Difficulty NORMAL=2. */
    for (k = -1; k <= 1; ++k)
        for (l = -1; l <= 1; ++l)
            for (i1 = -1; i1 <= 4; ++i1) {
                int j1, k1, l1, i2;
                if (k == 0 && i1 == 0 && l == 0) continue;
                j1 = 100;
                if (i1 > 1) j1 += (i1 - 1) * 100;
                k1 = rt_neighbor_encouragement(w, x + k, y + i1, z + l);
                if (k1 <= 0) continue;
                l1 = (k1 + 40 + 2 * 7) / (age + 30);
                if (l1 > 0 && jrand_int_bound(&rng, j1) <= l1) {
                    i2 = age + jrand_int_bound(&rng, 5) / 4;
                    if (i2 > 15) i2 = 15;
                    gm_world_set_block_meta(w, x + k, y + i1, z + l, RT_BLK_FIRE, i2);
                }
            }
}

/* ---- BlockGrass.updateTick ---- */

static void rt_tick_grass(GmWorld *w, int x, int y, int z,
                          long long seed, long long tick) {
    int above_id, light_up, i;
    if (gm_world_block(w, x, y, z) != BLK_GRASS) return;
    above_id = gm_world_block(w, x, y + 1, z);
    light_up = rt_light_at(w, x, y + 1, z);

    if (light_up < 4 && rt_opacity(above_id) > 2) {
        gm_world_set_block(w, x, y, z, BLK_DIRT);
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
        tid = gm_world_block(w, nx, ny, nz);
        tmeta = gm_world_meta(w, nx, ny, nz) & 15;
        /* only DirtType.DIRT (meta 0), not coarse/podzol */
        if (tid != BLK_DIRT || tmeta != 0) continue;
        a_id = gm_world_block(w, nx, ny + 1, nz);
        if (rt_light_at(w, nx, ny + 1, nz) >= 4 && rt_opacity(a_id) <= 2)
            gm_world_set_block(w, nx, ny, nz, BLK_GRASS);
    }
}

/* ---- BlockLeaves decay (BFS distance-4 log reachability) ---- */

static int rt_is_leaves(int id) {
    return id == RT_BLK_LEAVES || id == RT_BLK_LEAVES2;
}

static int rt_is_log(int id) {
    return id == RT_BLK_LOG || id == RT_BLK_LOG2;
}

/* DECAYABLE = (meta & 4) == 0; CHECK_DECAY = (meta & 8) != 0 */
static void rt_tick_leaves(GmWorld *w, int x, int y, int z) {
    int id = gm_world_block(w, x, y, z);
    int meta, check_decay, decayable;
    int surroundings[32 * 32 * 32];
    int i2, j2, k2, i3, j3, k3, l3, l2;

    if (!rt_is_leaves(id)) return;
    meta = gm_world_meta(w, x, y, z) & 15;
    check_decay = (meta & 8) != 0;
    decayable = (meta & 4) == 0;
    if (!check_decay || !decayable) return;

    /* Java `new int[32768]` zero-fills; out-of-window cells act as distance-0
     * (log). Match that so the BFS edge behavior is identical. */
    memset(surroundings, 0, sizeof surroundings);

    /* surroundings indexed like BlockLeaves: (dx+16)*1024 + (dy+16)*32 + (dz+16)
     * over a 9^3 window centered on the leaf (offsets -4..+4). */
    for (i2 = -4; i2 <= 4; ++i2)
        for (j2 = -4; j2 <= 4; ++j2)
            for (k2 = -4; k2 <= 4; ++k2) {
                int bid = gm_world_block(w, x + i2, y + j2, z + k2);
                int idx = (i2 + 16) * 1024 + (j2 + 16) * 32 + (k2 + 16);
                if (rt_is_log(bid))
                    surroundings[idx] = 0;
                else if (rt_is_leaves(bid))
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

    /* center cell index: (0+16)*1024 + (0+16)*32 + (0+16) = 16912 */
    l2 = surroundings[16912];
    if (l2 >= 0) {
        /* still attached: clear CHECK_DECAY */
        gm_world_set_block_meta(w, x, y, z, id, meta & ~8);
    } else {
        /* destroy: drop gap - no sapling/item entity path here; decay to air. */
        gm_world_set_block(w, x, y, z, 0);
    }
}

/* ---- BlockCrops (wheat / carrots / potatoes; max age 7) ---- */

static int rt_is_crop(int id) {
    return id == RT_BLK_WHEAT || id == RT_BLK_CARROT || id == RT_BLK_POTATO;
}

static int rt_crop_max_age(int id) {
    (void)id;
    return 7; /* beetroot (max 3 + throttled updateTick) not wired */
}

static float rt_growth_chance(const GmWorld *w, int x, int y, int z, int crop_id) {
    float f = 1.0f;
    int i, j;
    for (i = -1; i <= 1; ++i) {
        for (j = -1; j <= 1; ++j) {
            float f1 = 0.0f;
            int soil = gm_world_block(w, x + i, y - 1, z + j);
            int sm = gm_world_meta(w, x + i, y - 1, z + j) & 15;
            if (soil == RT_BLK_FARMLAND) {
                f1 = 1.0f;
                if (sm > 0) f1 = 3.0f;
            }
            if (i != 0 || j != 0) f1 /= 4.0f;
            f += f1;
        }
    }
    {
        int flag = (gm_world_block(w, x - 1, y, z) == crop_id ||
                    gm_world_block(w, x + 1, y, z) == crop_id);
        int flag1 = (gm_world_block(w, x, y, z - 1) == crop_id ||
                     gm_world_block(w, x, y, z + 1) == crop_id);
        if (flag && flag1) {
            f /= 2.0f;
        } else {
            int flag2 = (gm_world_block(w, x - 1, y, z - 1) == crop_id ||
                         gm_world_block(w, x + 1, y, z - 1) == crop_id ||
                         gm_world_block(w, x + 1, y, z + 1) == crop_id ||
                         gm_world_block(w, x - 1, y, z + 1) == crop_id);
            if (flag2) f /= 2.0f;
        }
    }
    return f;
}

static void rt_tick_crop(GmWorld *w, int x, int y, int z,
                         long long seed, long long tick) {
    int id = gm_world_block(w, x, y, z);
    int age, max_age, soil, bound;
    float gf;
    u64 h;
    if (!rt_is_crop(id)) return;
    soil = gm_world_block(w, x, y - 1, z);
    if (soil != RT_BLK_FARMLAND) {
        /* BlockBush.checkAndDropBlock stand-in: unsupported soil -> air (drop gap). */
        gm_world_set_block(w, x, y, z, 0);
        return;
    }
    if (rt_light_at(w, x, y + 1, z) < 9) return;
    age = gm_world_meta(w, x, y, z) & 15;
    max_age = rt_crop_max_age(id);
    if (age >= max_age) return;
    gf = rt_growth_chance(w, x, y, z, id);
    bound = (int)(25.0f / gf) + 1;
    if (bound < 1) bound = 1;
    h = mc_hash_seed((u64)seed, tick, x, y, z, RT_PURPOSE_CROP);
    if (mc_hash_bound(h, bound) == 0)
        gm_world_set_block_meta(w, x, y, z, id, age + 1);
}

/* ---- dispatch + pass ---- */

void gm_randtick_block(GmWorld *w, int wx, int wy, int wz,
                       long long seed, long long tick, const McGameRules *gr) {
    int id;
    McGameRules def;
    if (!w) return;
    if (!gr) { def = mc_gamerules_default(); gr = &def; }
    if (wy < 0 || wy >= 256) return;
    id = gm_world_block(w, wx, wy, wz);
    switch (id) {
        case BLK_GRASS:
            rt_tick_grass(w, wx, wy, wz, seed, tick);
            break;
        case RT_BLK_LEAVES:
        case RT_BLK_LEAVES2:
            rt_tick_leaves(w, wx, wy, wz);
            break;
        case RT_BLK_FIRE:
            rt_tick_fire(w, wx, wy, wz, seed, tick, gr);
            break;
        case RT_BLK_WHEAT:
        case RT_BLK_CARROT:
        case RT_BLK_POTATO:
            rt_tick_crop(w, wx, wy, wz, seed, tick);
            break;
        default:
            break;
    }
}

void gm_randtick_pass(GmWorld *w, long long seed, long long tick,
                      int ccx, int ccz, int radius, const McGameRules *gr) {
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
                    gm_randtick_block(w, wx, wy, wz, seed, tick, gr);
                }
            }
        }
    }
}
