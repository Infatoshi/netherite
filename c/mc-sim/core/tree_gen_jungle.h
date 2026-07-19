/* tree_gen_jungle: WorldGenMegaJungle(false,10,20,JUNGLE_LOG,JUNGLE_LEAF) on REAL grass/dirt
 * from sbr_run (chunk 0,0). Vines via placeVine (BlockVine E/W/S/N -> TGJ_VINE_BASE+dir).
 *
 * Pipeline:
 *   1. sbr_run -> cpbw + replaceBiomeBlocks primer (surface_blocks_real.h)
 *   2. Fixed column (TGJ_PLANT_X,TGJ_PLANT_Z)=(8,8): plant at surfaceY+1
 *   3. WorldGenMegaJungle.generate verbatim (WorldGenHugeTrees helpers inlined)
 *
 * Block-state ids extend CB_* with decoration codes: LOG_JUNGLE=75, LEAVES_JUNGLE=76,
 * VINE_BASE=71 (+0=E,1=W,2=S,3=N). Predicates mirror populate.h PB_* for every id the
 * sbr primer can hold plus tree decoration writes. worldIn.getHeight()=256.
 *
 * READ-ONLY compose: surface_blocks_real.h, mc_math.h (MathHelper sin/cos for branches).
 * Output: full ChunkPrimer char[65536] in index order, %04x one per line. */
#ifndef MC_TREE_GEN_JUNGLE_H
#define MC_TREE_GEN_JUNGLE_H

#include "surface_blocks_real.h"
#include "mc_math.h"

#define TGJ_PLANT_X         8
#define TGJ_PLANT_Z         8
#define TGJ_CHUNK_Y         256
#define TGJ_BASE_HEIGHT     10
#define TGJ_EXTRA_RAND_H    20
#define TGJ_LOG_JUNGLE      75
#define TGJ_LEAVES_JUNGLE   76
#define TGJ_VINE_BASE       71   /* +0=E +1=W +2=S +3=N */
#define TGJ_VINE_E          0
#define TGJ_VINE_W          1
#define TGJ_VINE_S          2
#define TGJ_VINE_N          3

MC_HD MC_NOINLINE static int tgj_inb(int x, int y, int z) {
    return x >= 0 && x < 16 && y >= 0 && y < 256 && z >= 0 && z < 16;
}
MC_HD MC_NOINLINE static int tgj_get(const ChunkPrimer *p, int x, int y, int z) {
    return tgj_inb(x, y, z) ? cb_get(p, x, y, z) : CB_AIR;
}
MC_HD MC_NOINLINE static void tgj_set(ChunkPrimer *p, int x, int y, int z, int v) {
    if (tgj_inb(x, y, z)) cb_set(p, x, y, z, v);
}

MC_HD static inline int tgj_is_air(int c) { return c == CB_AIR; }
MC_HD static inline int tgj_is_vine(int c) { return c >= TGJ_VINE_BASE && c < TGJ_VINE_BASE + 4; }
MC_HD MC_NOINLINE static int tgj_is_leaves(int c) {
    return c == TGJ_LEAVES_JUNGLE;
}
MC_HD static inline int tgj_is_log(int c) { return c == TGJ_LOG_JUNGLE; }
MC_HD MC_NOINLINE static int tgj_is_dirt(int c) {
    return c == CB_DIRT || c == CB_PODZOL || c == CB_COARSE_DIRT;
}
MC_HD MC_NOINLINE static int tgj_can_grow_into(int c) {
    return tgj_is_air(c) || tgj_is_leaves(c) || c == CB_GRASS || tgj_is_dirt(c) ||
           tgj_is_log(c) || tgj_is_vine(c);
}
MC_HD MC_NOINLINE static int tgj_is_replaceable(const ChunkPrimer *p, int x, int y, int z) {
    int c = tgj_get(p, x, y, z);
    return tgj_is_air(c) || tgj_is_leaves(c) || tgj_is_log(c) || tgj_can_grow_into(c);
}
MC_HD MC_NOINLINE static int tgj_is_air_leaves(const ChunkPrimer *p, int x, int y, int z) {
    int c = tgj_get(p, x, y, z);
    return tgj_is_air(c) || tgj_is_leaves(c);
}
MC_HD MC_NOINLINE static int tgj_can_sustain_plant(int soil) {
    return soil == CB_GRASS || tgj_is_dirt(soil);
}
MC_HD MC_NOINLINE static void tgj_on_plant_grow(ChunkPrimer *p, int x, int y, int z) {
    if (tgj_get(p, x, y, z) == CB_GRASS) tgj_set(p, x, y, z, CB_DIRT);
}

MC_HD MC_NOINLINE static int tgj_plant_y(const ChunkPrimer *p, int x, int z) {
    for (int y = 255; y >= 0; --y) {
        if (tgj_get(p, x, y, z) != CB_AIR) return y + 1;
    }
    return -1;
}

MC_HD MC_NOINLINE static int tgj_get_height(JavaRandom *r) {
    int i = jrand_int_bound(r, 3) + TGJ_BASE_HEIGHT;
    if (TGJ_EXTRA_RAND_H > 1) i += jrand_int_bound(r, TGJ_EXTRA_RAND_H);
    return i;
}

MC_HD MC_NOINLINE static int tgj_is_space_at(const ChunkPrimer *p, int px, int py, int pz, int height) {
    if (py < 1 || py + height + 1 > TGJ_CHUNK_Y) return 0;
    for (int i = 0; i <= 1 + height; ++i) {
        int j = 2;
        if (i == 0) j = 1;
        else if (i >= 1 + height - 2) j = 2;
        for (int k = -j; k <= j; ++k) {
            for (int l = -j; l <= j; ++l) {
                if (py + i < 0 || py + i >= TGJ_CHUNK_Y) return 0;
                if (!tgj_is_replaceable(p, px + k, py + i, pz + l)) return 0;
            }
        }
    }
    return 1;
}

MC_HD MC_NOINLINE static int tgj_ensure_dirts_underneath(ChunkPrimer *p, int px, int py, int pz) {
    if (py < 2) return 0;
    int bx = px, by = py - 1, bz = pz;
    if (!tgj_can_sustain_plant(tgj_get(p, bx, by, bz))) return 0;
    tgj_on_plant_grow(p, bx, by, bz);
    tgj_on_plant_grow(p, bx + 1, by, bz);
    tgj_on_plant_grow(p, bx, by, bz + 1);
    tgj_on_plant_grow(p, bx + 1, by, bz + 1);
    return 1;
}

MC_HD MC_NOINLINE static int tgj_ensure_growable(ChunkPrimer *p, JavaRandom *r, int px, int py, int pz, int h) {
    (void)r;
    return tgj_is_space_at(p, px, py, pz, h) && tgj_ensure_dirts_underneath(p, px, py, pz);
}

MC_HD MC_NOINLINE static void tgj_grow_leaves_layer_strict(ChunkPrimer *p, int cx, int cy, int cz, int width) {
    int i = width * width;
    for (int j = -width; j <= width + 1; ++j) {
        for (int k = -width; k <= width + 1; ++k) {
            int l = j - 1;
            int i1 = k - 1;
            if (j * j + k * k <= i || l * l + i1 * i1 <= i || j * j + i1 * i1 <= i || l * l + k * k <= i) {
                int cs = tgj_get(p, cx + j, cy, cz + k);
                if (tgj_is_air(cs) || tgj_is_leaves(cs))
                    tgj_set(p, cx + j, cy, cz + k, TGJ_LEAVES_JUNGLE);
            }
        }
    }
}

MC_HD MC_NOINLINE static void tgj_grow_leaves_layer(ChunkPrimer *p, int cx, int cy, int cz, int width) {
    int i = width * width;
    for (int j = -width; j <= width; ++j) {
        for (int k = -width; k <= width; ++k) {
            if (j * j + k * k <= i) {
                int cs = tgj_get(p, cx + j, cy, cz + k);
                if (tgj_is_air(cs) || tgj_is_leaves(cs))
                    tgj_set(p, cx + j, cy, cz + k, TGJ_LEAVES_JUNGLE);
            }
        }
    }
}

MC_HD MC_NOINLINE static void tgj_create_crown(ChunkPrimer *p, int cx, int cy, int cz, int w) {
    for (int j = -2; j <= 0; ++j)
        tgj_grow_leaves_layer_strict(p, cx, cy + j, cz, w + 1 - j);
}

MC_HD MC_NOINLINE static void tgj_place_vine(ChunkPrimer *p, JavaRandom *r, int x, int y, int z, int dir) {
    if (jrand_int_bound(r, 3) > 0 && tgj_is_air(tgj_get(p, x, y, z)))
        tgj_set(p, x, y, z, TGJ_VINE_BASE + dir);
}

MC_HD MC_NOINLINE static int tgj_mega_jungle(ChunkPrimer *p, const McSinTable *st, JavaRandom *r,
        int posX, int posY, int posZ) {
    int i = tgj_get_height(r);
    if (!tgj_ensure_growable(p, r, posX, posY, posZ, i)) return 0;

    tgj_create_crown(p, posX, posY + i, posZ, 2);

    for (int j = posY + i - 2 - jrand_int_bound(r, 4); j > posY + i / 2; j -= 2 + jrand_int_bound(r, 4)) {
        float f = jrand_float(r) * ((float)MC_PI * 2.0F);
        int k = posX + (int)(0.5F + mc_cos(st, f) * 4.0F);
        int l = posZ + (int)(0.5F + mc_sin(st, f) * 4.0F);

        for (int i1 = 0; i1 < 5; ++i1) {
            k = posX + (int)(1.5F + mc_cos(st, f) * (float)i1);
            l = posZ + (int)(1.5F + mc_sin(st, f) * (float)i1);
            tgj_set(p, k, j - 3 + i1 / 2, l, TGJ_LOG_JUNGLE);
        }

        int j2 = 1 + jrand_int_bound(r, 2);
        int j1 = j;
        for (int k1 = j - j2; k1 <= j1; ++k1) {
            int l1 = k1 - j1;
            tgj_grow_leaves_layer(p, k, k1, l, 1 - l1);
        }
    }

    for (int i2 = 0; i2 < i; ++i2) {
        int bx = posX, by = posY + i2, bz = posZ;
        if (tgj_is_air_leaves(p, bx, by, bz)) {
            tgj_set(p, bx, by, bz, TGJ_LOG_JUNGLE);
            if (i2 > 0) {
                tgj_place_vine(p, r, bx - 1, by, bz, TGJ_VINE_E);
                tgj_place_vine(p, r, bx, by, bz - 1, TGJ_VINE_S);
            }
        }
        if (i2 < i - 1) {
            int ex = bx + 1, ey = by, ez = bz;
            if (tgj_is_air_leaves(p, ex, ey, ez)) {
                tgj_set(p, ex, ey, ez, TGJ_LOG_JUNGLE);
                if (i2 > 0) {
                    tgj_place_vine(p, r, ex + 1, ey, ez, TGJ_VINE_W);
                    tgj_place_vine(p, r, ex, ey, ez - 1, TGJ_VINE_S);
                }
            }
            int sx = bx + 1, sy = by, sz = bz + 1;
            if (tgj_is_air_leaves(p, sx, sy, sz)) {
                tgj_set(p, sx, sy, sz, TGJ_LOG_JUNGLE);
                if (i2 > 0) {
                    tgj_place_vine(p, r, sx + 1, sy, sz, TGJ_VINE_W);
                    tgj_place_vine(p, r, sx, sy, sz + 1, TGJ_VINE_N);
                }
            }
            int nx = bx, ny = by, nz = bz + 1;
            if (tgj_is_air_leaves(p, nx, ny, nz)) {
                tgj_set(p, nx, ny, nz, TGJ_LOG_JUNGLE);
                if (i2 > 0) {
                    tgj_place_vine(p, r, nx - 1, ny, nz, TGJ_VINE_E);
                    tgj_place_vine(p, r, nx, ny, nz + 1, TGJ_VINE_N);
                }
            }
        }
    }
    return 1;
}

MC_HD MC_NOINLINE static void tgj_run(ChunkPrimer *primer, CpScratch *sc, const McSinTable *st, i64 seed) {
    sbr_run(primer, sc, seed, 0, 0);
    int plantY = tgj_plant_y(primer, TGJ_PLANT_X, TGJ_PLANT_Z);
    if (plantY > 0) {
        JavaRandom r;
        jrand_set(&r, seed);
        tgj_mega_jungle(primer, st, &r, TGJ_PLANT_X, plantY, TGJ_PLANT_Z);
    }
}

#endif /* MC_TREE_GEN_JUNGLE_H */
