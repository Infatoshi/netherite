/* plant_growth: synthetic soil/crop column battery for plant tick math.
 *
 * PORT TARGETS (MC 1.11.2 decompiled, java/oracle-src/net/minecraft/block):
 *   BlockCrops.updateTick + getGrowthChance   (wheat age 0..7, light + farmland moisture)
 *   BlockFarmland.updateTick                  (hydrate to 7 / dry / turnToDirt)
 *   Entity.canTrample + BlockFarmland.onFallenUpon  (trample -> dirt)
 *   BlockSapling.updateTick + grow            (stage 0->1; stage1 -> LOG marker, no WorldGenTrees)
 *   BlockCactus.updateTick                    (age 0..15, height grow if stack < 3)
 *   BlockReed.updateTick                      (age 0..15, height grow if stack < 3)
 *   BlockStem.updateTick                      (pumpkin stem age + fruit place via HORIZONTAL.random)
 *   BlockNetherWart.updateTick                (age 0..3, nextInt(10)==0)
 *
 * RNG: JavaRandom (java.util.Random LCG) for all growth/trample rolls, sequential in fixed
 * cell order so java==cpu==cuda. Synthetic battery random-ticks EVERY plantable cell each
 * tick (not the 3/subchunk vanilla schedule) - the tick MATH is what we verify.
 *
 * Double-buffered 16x8x16 cube. Probe dump after PG_NTICKS. */
#ifndef MC_PLANT_GROWTH_H
#define MC_PLANT_GROWTH_H

#include "mc.h"
#include "mc_rng.h"
#include "mc_world.h"

#define PG_W       16
#define PG_H        8
#define PG_VOL     (PG_W * PG_W * PG_H)
#define PG_NTICKS  64
#define PG_DEFAULT_SEED 12345LL

/* Vanilla block ids (Block.java registerBlock). */
#define PG_AIR            0
#define PG_GRASS          2
#define PG_DIRT           3
#define PG_SAPLING        6
#define PG_WATER          9
#define PG_SAND          12
#define PG_LOG           17
#define PG_WHEAT         59
#define PG_FARMLAND      60
#define PG_CACTUS        81
#define PG_REEDS         83
#define PG_PUMPKIN       86
#define PG_SOUL_SAND     88
#define PG_PUMPKIN_STEM 104
#define PG_NETHER_WART  115

/* HORIZONTAL.facings() order: N E S W (EnumFacing.Plane.HORIZONTAL). */
MC_HD static inline int pg_hdx(int d) {
    /* 0=N 1=E 2=S 3=W */
    return d == 1 ? 1 : (d == 3 ? -1 : 0);
}
MC_HD static inline int pg_hdz(int d) {
    return d == 0 ? -1 : (d == 2 ? 1 : 0);
}

typedef struct {
    u16 blocks_a[PG_VOL];
    u16 blocks_b[PG_VOL];
    u8  light_above[PG_VOL]; /* getLightFromNeighbors(pos.up()) stand-in, 0..15 */
    int cur;
    i64 seed;
    i64 tick;
    JavaRandom rng;
} PgWorld;

MC_HD static inline int pg_idx(int x, int y, int z) {
    return (y * PG_W + z) * PG_W + x;
}

MC_HD static inline u16 *pg_now(PgWorld *w)  { return w->cur ? w->blocks_b : w->blocks_a; }
MC_HD static inline u16 *pg_next(PgWorld *w) { return w->cur ? w->blocks_a : w->blocks_b; }

MC_HD static inline int pg_in(int x, int y, int z) {
    return x >= 0 && x < PG_W && y >= 0 && y < PG_H && z >= 0 && z < PG_W;
}

MC_HD static inline u16 pg_get(const u16 *b, int x, int y, int z) {
    if (!pg_in(x, y, z)) return mc_state(PG_AIR, 0);
    return b[pg_idx(x, y, z)];
}

MC_HD static inline void pg_set(u16 *b, int x, int y, int z, u16 s) {
    if (!pg_in(x, y, z)) return;
    b[pg_idx(x, y, z)] = s;
}

MC_HD static inline int pg_id(u16 s)   { return mc_state_id(s); }
MC_HD static inline int pg_meta(u16 s) { return mc_state_meta(s); }

MC_HD static inline void pg_copy(u16 *dst, const u16 *src) {
    for (int i = 0; i < PG_VOL; ++i) dst[i] = src[i];
}

MC_HD static inline int pg_is_water(u16 s) {
    int id = pg_id(s);
    return id == PG_WATER || id == 8; /* flowing water */
}

MC_HD static inline int pg_is_farmland(u16 s) { return pg_id(s) == PG_FARMLAND; }
MC_HD static inline int pg_farmland_fertile(u16 s) {
    return pg_is_farmland(s) && pg_meta(s) > 0;
}

/* ---- BlockCrops.getGrowthChance (farmland sustain only) ---- */
MC_HD static inline float pg_growth_chance(const u16 *now, int x, int y, int z, int crop_id) {
    float f = 1.0f;
    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
            float f1 = 0.0f;
            u16 soil = pg_get(now, x + i, y - 1, z + j);
            if (pg_is_farmland(soil)) {
                f1 = 1.0f;
                if (pg_farmland_fertile(soil)) f1 = 3.0f;
            }
            if (i != 0 || j != 0) f1 /= 4.0f;
            f += f1;
        }
    }
    int flag = (pg_id(pg_get(now, x - 1, y, z)) == crop_id ||
                pg_id(pg_get(now, x + 1, y, z)) == crop_id);
    int flag1 = (pg_id(pg_get(now, x, y, z - 1)) == crop_id ||
                 pg_id(pg_get(now, x, y, z + 1)) == crop_id);
    if (flag && flag1) {
        f /= 2.0f;
    } else {
        int flag2 = (pg_id(pg_get(now, x - 1, y, z - 1)) == crop_id ||
                     pg_id(pg_get(now, x + 1, y, z - 1)) == crop_id ||
                     pg_id(pg_get(now, x + 1, y, z + 1)) == crop_id ||
                     pg_id(pg_get(now, x - 1, y, z + 1)) == crop_id);
        if (flag2) f /= 2.0f;
    }
    return f;
}

/* BlockCrops.canBlockStay simplified: light>=8 (or sky) + farmland soil.
 * Battery uses light_above as getLightFromNeighbors(up) for growth; stay uses light_above>=8. */
MC_HD static inline int pg_crop_can_stay(PgWorld *w, const u16 *now, int x, int y, int z) {
    int la = (int)w->light_above[pg_idx(x, y, z)];
    if (la < 8) return 0;
    return pg_is_farmland(pg_get(now, x, y - 1, z));
}

/* BlockCrops.updateTick (wheat). ForgeHooks.onCropsGrowPre just passes the nextInt bool. */
MC_HD static inline void pg_tick_wheat(PgWorld *w, const u16 *now, u16 *next, int x, int y, int z) {
    u16 s = pg_get(now, x, y, z);
    if (pg_id(s) != PG_WHEAT) return;
    if (!pg_crop_can_stay(w, now, x, y, z)) {
        pg_set(next, x, y, z, mc_state(PG_AIR, 0));
        return;
    }
    int la = (int)w->light_above[pg_idx(x, y, z)];
    if (la < 9) return;
    int age = pg_meta(s);
    if (age >= 7) return;
    float f = pg_growth_chance(now, x, y, z, PG_WHEAT);
    int bound = (int)(25.0f / f) + 1;
    if (bound < 1) bound = 1;
    if (jrand_int_bound(&w->rng, bound) == 0) {
        int na = age + 1;
        if (na > 7) na = 7;
        pg_set(next, x, y, z, mc_state(PG_WHEAT, na));
    }
}

/* BlockFarmland.hasWater: any water in [x-4..x+4] x [y..y+1] x [z-4..z+4]. */
MC_HD static inline int pg_farmland_has_water(const u16 *now, int x, int y, int z) {
    for (int dy = 0; dy <= 1; ++dy) {
        for (int dz = -4; dz <= 4; ++dz) {
            for (int dx = -4; dx <= 4; ++dx) {
                if (pg_is_water(pg_get(now, x + dx, y + dy, z + dz)))
                    return 1;
            }
        }
    }
    return 0;
}

/* hasCrops: plant above that farmland sustains (wheat / stem). */
MC_HD static inline int pg_farmland_has_crops(const u16 *now, int x, int y, int z) {
    int id = pg_id(pg_get(now, x, y + 1, z));
    return id == PG_WHEAT || id == PG_PUMPKIN_STEM || id == PG_SAPLING;
}

/* BlockFarmland.updateTick (no rain in battery). */
MC_HD static inline void pg_tick_farmland(PgWorld *w, const u16 *now, u16 *next, int x, int y, int z) {
    (void)w;
    u16 s = pg_get(now, x, y, z);
    if (pg_id(s) != PG_FARMLAND) return;
    int moisture = pg_meta(s) & 7;
    if (!pg_farmland_has_water(now, x, y, z)) {
        if (moisture > 0) {
            pg_set(next, x, y, z, mc_state(PG_FARMLAND, moisture - 1));
        } else if (!pg_farmland_has_crops(now, x, y, z)) {
            pg_set(next, x, y, z, mc_state(PG_DIRT, 0));
        }
    } else if (moisture < 7) {
        pg_set(next, x, y, z, mc_state(PG_FARMLAND, 7));
    }
}

/* Entity.canTrample for player-sized entity: nextFloat() < fallDistance - 0.5F. */
MC_HD static inline void pg_tick_trample(PgWorld *w, const u16 *now, u16 *next, int x, int y, int z,
                                         float fall_distance) {
    u16 s = pg_get(now, x, y, z);
    if (pg_id(s) != PG_FARMLAND) return;
    /* nextFloat is jrand_next(24) / 2^24 */
    float r = jrand_float(&w->rng);
    if (r < fall_distance - 0.5f)
        pg_set(next, x, y, z, mc_state(PG_DIRT, 0));
}

/* BlockSapling: stage in meta bit 3; type in low 3 bits. Oak type 0 only.
 * stage0 -> stage1; stage1 -> LOG marker (WorldGenTrees cut). */
MC_HD static inline void pg_tick_sapling(PgWorld *w, const u16 *now, u16 *next, int x, int y, int z) {
    u16 s = pg_get(now, x, y, z);
    if (pg_id(s) != PG_SAPLING) return;
    int la = (int)w->light_above[pg_idx(x, y, z)];
    if (la < 9) return;
    if (jrand_int_bound(&w->rng, 7) != 0) return;
    int meta = pg_meta(s);
    int stage = (meta >> 3) & 1;
    int type  = meta & 7;
    if (stage == 0) {
        pg_set(next, x, y, z, mc_state(PG_SAPLING, type | (1 << 3)));
    } else {
        /* tree-grown marker (no WorldGenTrees) */
        pg_set(next, x, y, z, mc_state(PG_LOG, type));
    }
}

/* BlockCactus.updateTick */
MC_HD static inline void pg_tick_cactus(PgWorld *w, const u16 *now, u16 *next, int x, int y, int z) {
    (void)w;
    u16 s = pg_get(now, x, y, z);
    if (pg_id(s) != PG_CACTUS) return;
    if (pg_id(pg_get(now, x, y + 1, z)) != PG_AIR) return;
    int i;
    for (i = 1; pg_id(pg_get(now, x, y - i, z)) == PG_CACTUS; ++i) { }
    if (i >= 3) return;
    int age = pg_meta(s);
    if (age == 15) {
        pg_set(next, x, y + 1, z, mc_state(PG_CACTUS, 0));
        pg_set(next, x, y, z, mc_state(PG_CACTUS, 0));
    } else {
        pg_set(next, x, y, z, mc_state(PG_CACTUS, age + 1));
    }
}

/* BlockReed.updateTick */
MC_HD static inline void pg_tick_reed(PgWorld *w, const u16 *now, u16 *next, int x, int y, int z) {
    (void)w;
    u16 s = pg_get(now, x, y, z);
    if (pg_id(s) != PG_REEDS) return;
    /* stay: reed below OR (grass/dirt/sand + horizontal water at soil) - battery places valid columns */
    int below = pg_id(pg_get(now, x, y - 1, z));
    if (below != PG_REEDS && below != PG_GRASS && below != PG_DIRT && below != PG_SAND)
        return;
    if (pg_id(pg_get(now, x, y + 1, z)) != PG_AIR) return;
    int i;
    for (i = 1; pg_id(pg_get(now, x, y - i, z)) == PG_REEDS; ++i) { }
    if (i >= 3) return;
    int age = pg_meta(s);
    if (age == 15) {
        pg_set(next, x, y + 1, z, mc_state(PG_REEDS, 0));
        pg_set(next, x, y, z, mc_state(PG_REEDS, 0));
    } else {
        pg_set(next, x, y, z, mc_state(PG_REEDS, age + 1));
    }
}

/* BlockNetherWart.updateTick: age++ if nextInt(10)==0 and age<3. */
MC_HD static inline void pg_tick_nether_wart(PgWorld *w, const u16 *now, u16 *next, int x, int y, int z) {
    u16 s = pg_get(now, x, y, z);
    if (pg_id(s) != PG_NETHER_WART) return;
    if (pg_id(pg_get(now, x, y - 1, z)) != PG_SOUL_SAND) {
        pg_set(next, x, y, z, mc_state(PG_AIR, 0));
        return;
    }
    int age = pg_meta(s);
    if (age < 3 && jrand_int_bound(&w->rng, 10) == 0)
        pg_set(next, x, y, z, mc_state(PG_NETHER_WART, age + 1));
}

/* BlockStem.updateTick for pumpkin stem. crop = PG_PUMPKIN. */
MC_HD static inline void pg_tick_stem(PgWorld *w, const u16 *now, u16 *next, int x, int y, int z) {
    u16 s = pg_get(now, x, y, z);
    if (pg_id(s) != PG_PUMPKIN_STEM) return;
    if (!pg_crop_can_stay(w, now, x, y, z)) {
        pg_set(next, x, y, z, mc_state(PG_AIR, 0));
        return;
    }
    int la = (int)w->light_above[pg_idx(x, y, z)];
    if (la < 9) return;
    float f = pg_growth_chance(now, x, y, z, PG_PUMPKIN_STEM);
    int bound = (int)(25.0f / f) + 1;
    if (bound < 1) bound = 1;
    if (jrand_int_bound(&w->rng, bound) != 0) return;
    int age = pg_meta(s);
    if (age < 7) {
        pg_set(next, x, y, z, mc_state(PG_PUMPKIN_STEM, age + 1));
    } else {
        /* already attached? */
        for (int d = 0; d < 4; ++d) {
            if (pg_id(pg_get(now, x + pg_hdx(d), y, z + pg_hdz(d))) == PG_PUMPKIN)
                return;
        }
        int dir = jrand_int_bound(&w->rng, 4);
        int fx = x + pg_hdx(dir);
        int fz = z + pg_hdz(dir);
        u16 soil = pg_get(now, fx, y - 1, fz);
        int sid = pg_id(soil);
        if (pg_id(pg_get(now, fx, y, fz)) == PG_AIR &&
            (pg_is_farmland(soil) || sid == PG_DIRT || sid == PG_GRASS)) {
            pg_set(next, fx, y, fz, mc_state(PG_PUMPKIN, 0));
        }
    }
}

MC_HD static inline void pg_init(PgWorld *w, i64 seed) {
    w->cur = 0;
    w->seed = seed;
    w->tick = 0;
    jrand_set(&w->rng, seed);
    u16 air = mc_state(PG_AIR, 0);
    for (int i = 0; i < PG_VOL; ++i) {
        w->blocks_a[i] = air;
        w->light_above[i] = 15;
    }
    u16 *b = w->blocks_a;

    /* y=1 soil bed, y=2 farmland/base, y=3 crop tops.
     * Water radius is 4: keep wet and dry zones >=5 apart on xz.
     *
     * WET zone (z=1..3) - shared water at (4,2,1):
     *   A (3,2,2): wheat0 wet farmland, light15 -> grows.
     *   C (5,2,2): wheat3 wet, light8 -> no age advance.
     *   D (7,2,2): farmland moisture0 -> hydrate to 7.
     *   K (3,2,3): pumpkin stem0 wet farmland, light15.
     *
     * DRY zone (z=10+):
     *   B (3,2,10): wheat0 dry farmland0, light15 -> slower / no hydrate.
     *   E (5,2,10): farmland7 + wheat1, no water -> moisture steps down.
     *   F (7,2,10): farmland0 no crop no water -> dirt.
     *   L (9,2,10): farmland7 trample target (fallDistance=1.0F).
     *
     * Other plants (z=6):
     *   G (1,2,6): cactus age14 on sand.
     *   H (4,2,6): reed age14 on dirt + local water (5,1,6).
     *   I (7,2,6): oak sapling stage0, light15.
     *   J (10,2,6): nether wart0 on soul sand.
     */

    /* shared wet-zone water (covers A/C/D/K at z=2..3; dry zone z=10 is out of range 4) */
    pg_set(b, 4, 2, 1, mc_state(PG_WATER, 0));

    /* A wheat wet */
    pg_set(b, 3, 1, 2, mc_state(PG_DIRT, 0));
    pg_set(b, 3, 2, 2, mc_state(PG_FARMLAND, 7));
    pg_set(b, 3, 3, 2, mc_state(PG_WHEAT, 0));
    w->light_above[pg_idx(3, 3, 2)] = 15;

    /* C wheat low light (wet) */
    pg_set(b, 5, 1, 2, mc_state(PG_DIRT, 0));
    pg_set(b, 5, 2, 2, mc_state(PG_FARMLAND, 7));
    pg_set(b, 5, 3, 2, mc_state(PG_WHEAT, 3));
    w->light_above[pg_idx(5, 3, 2)] = 8;

    /* D farmland hydrate */
    pg_set(b, 7, 1, 2, mc_state(PG_DIRT, 0));
    pg_set(b, 7, 2, 2, mc_state(PG_FARMLAND, 0));

    /* K pumpkin stem (wet) */
    pg_set(b, 3, 1, 3, mc_state(PG_DIRT, 0));
    pg_set(b, 3, 2, 3, mc_state(PG_FARMLAND, 7));
    pg_set(b, 3, 3, 3, mc_state(PG_PUMPKIN_STEM, 0));
    w->light_above[pg_idx(3, 3, 3)] = 15;
    pg_set(b, 3, 2, 4, mc_state(PG_DIRT, 0)); /* S pad for fruit (dz+1) */
    pg_set(b, 2, 2, 3, mc_state(PG_DIRT, 0)); /* W */
    pg_set(b, 4, 2, 3, mc_state(PG_DIRT, 0)); /* E */
    /* N pad is farmland A - also valid fruit soil */

    /* B wheat dry soil */
    pg_set(b, 3, 1, 10, mc_state(PG_DIRT, 0));
    pg_set(b, 3, 2, 10, mc_state(PG_FARMLAND, 0));
    pg_set(b, 3, 3, 10, mc_state(PG_WHEAT, 0));
    w->light_above[pg_idx(3, 3, 10)] = 15;

    /* E farmland dry with crop */
    pg_set(b, 5, 1, 10, mc_state(PG_DIRT, 0));
    pg_set(b, 5, 2, 10, mc_state(PG_FARMLAND, 7));
    pg_set(b, 5, 3, 10, mc_state(PG_WHEAT, 1));
    w->light_above[pg_idx(5, 3, 10)] = 15;

    /* F farmland dry no crop -> dirt */
    pg_set(b, 7, 1, 10, mc_state(PG_DIRT, 0));
    pg_set(b, 7, 2, 10, mc_state(PG_FARMLAND, 0));

    /* L trample */
    pg_set(b, 9, 1, 10, mc_state(PG_DIRT, 0));
    pg_set(b, 9, 2, 10, mc_state(PG_FARMLAND, 7));

    /* G cactus */
    pg_set(b, 1, 1, 6, mc_state(PG_SAND, 0));
    pg_set(b, 1, 2, 6, mc_state(PG_CACTUS, 14));

    /* H reed: dirt + water neighbor at soil y=1 (not farmland hydrate plane) */
    pg_set(b, 4, 1, 6, mc_state(PG_DIRT, 0));
    pg_set(b, 5, 1, 6, mc_state(PG_WATER, 0));
    pg_set(b, 4, 2, 6, mc_state(PG_REEDS, 14));

    /* I sapling */
    pg_set(b, 7, 1, 6, mc_state(PG_DIRT, 0));
    pg_set(b, 7, 2, 6, mc_state(PG_SAPLING, 0));
    w->light_above[pg_idx(7, 2, 6)] = 15;

    /* J nether wart */
    pg_set(b, 10, 1, 6, mc_state(PG_SOUL_SAND, 0));
    pg_set(b, 10, 2, 6, mc_state(PG_NETHER_WART, 0));

    pg_copy(w->blocks_b, w->blocks_a);
}

/* One synthetic random-tick sweep: fixed xyz order, one shared JavaRandom stream. */
MC_HD static inline void pg_tick(PgWorld *w) {
    const u16 *now = pg_now(w);
    u16 *next = pg_next(w);
    pg_copy(next, now);

    for (int z = 0; z < PG_W; ++z) {
        for (int y = 0; y < PG_H; ++y) {
            for (int x = 0; x < PG_W; ++x) {
                int id = pg_id(pg_get(now, x, y, z));
                switch (id) {
                case PG_WHEAT:        pg_tick_wheat(w, now, next, x, y, z); break;
                case PG_FARMLAND:     pg_tick_farmland(w, now, next, x, y, z); break;
                case PG_SAPLING:      pg_tick_sapling(w, now, next, x, y, z); break;
                case PG_CACTUS:       pg_tick_cactus(w, now, next, x, y, z); break;
                case PG_REEDS:        pg_tick_reed(w, now, next, x, y, z); break;
                case PG_NETHER_WART:  pg_tick_nether_wart(w, now, next, x, y, z); break;
                case PG_PUMPKIN_STEM: pg_tick_stem(w, now, next, x, y, z); break;
                default: break;
                }
            }
        }
    }
    /* Trample event on col L after block ticks (player fallDistance=1.0F). */
    {
        int x = 9, y = 2, z = 10;
        u16 s = pg_get(next, x, y, z);
        if (pg_id(s) == PG_FARMLAND) {
            float r = jrand_float(&w->rng);
            if (r < 1.0f - 0.5f)
                pg_set(next, x, y, z, mc_state(PG_DIRT, 0));
        }
    }

    w->tick++;
    w->cur ^= 1;
}

MC_HD static inline void pg_run(PgWorld *w, i64 seed, int nticks) {
    pg_init(w, seed);
    for (int t = 0; t < nticks; ++t)
        pg_tick(w);
}

/* Probe cells dumped in fixed order (id, meta). */
#define PG_NPROBES 16

typedef struct { int x, y, z; } PgProbe;

MC_HD static inline void pg_probes(PgProbe *p) {
    p[0].x = 3;  p[0].y = 3; p[0].z = 2;    /* A wheat wet */
    p[1].x = 3;  p[1].y = 3; p[1].z = 10;   /* B wheat dry */
    p[2].x = 5;  p[2].y = 3; p[2].z = 2;    /* C wheat low light */
    p[3].x = 7;  p[3].y = 2; p[3].z = 2;    /* D farmland hydrate */
    p[4].x = 5;  p[4].y = 2; p[4].z = 10;   /* E farmland dry+crop moisture */
    p[5].x = 7;  p[5].y = 2; p[5].z = 10;   /* F dry no crop */
    p[6].x = 5;  p[6].y = 3; p[6].z = 10;   /* E wheat age */
    p[7].x = 1;  p[7].y = 2; p[7].z = 6;    /* cactus base */
    p[8].x = 1;  p[8].y = 3; p[8].z = 6;    /* cactus grown */
    p[9].x = 4;  p[9].y = 2; p[9].z = 6;    /* reed base */
    p[10].x = 4; p[10].y = 3; p[10].z = 6;  /* reed grown */
    p[11].x = 7; p[11].y = 2; p[11].z = 6;  /* sapling / log */
    p[12].x = 10; p[12].y = 2; p[12].z = 6; /* nether wart */
    p[13].x = 3; p[13].y = 3; p[13].z = 3;  /* stem */
    p[14].x = 3; p[14].y = 3; p[14].z = 4;  /* stem S fruit pad */
    p[15].x = 9; p[15].y = 2; p[15].z = 10; /* trample */
}

#endif /* MC_PLANT_GROWTH_H */
