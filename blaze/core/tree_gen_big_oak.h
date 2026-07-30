/* tree_gen_big_oak: WorldGenBigTree on REAL grass/dirt from sbr_run (chunk 0,0).
 *
 * Pipeline:
 *   1. sbr_run -> cpbw + replaceBiomeBlocks primer (surface_blocks_real.h)
 *   2. Fixed column (TGOA_PLANT_X,TGOA_PLANT_Z)=(8,8): plant at surfaceY+1 (scan down from y=255)
 *   3. WorldGenBigTree.generate verbatim (same control flow/RNG/trig as core/populate.h wg_bigtree)
 *
 * Block-state ids extend chunk_provider CB_* with decoration codes: LOG_OAK=31, LOG_OAK_X=37,
 * LOG_OAK_Z=38, LEAVES_OAK=34. Uses its OWN Random seeded from one main-stream nextLong;
 * persistent heightLimit reset each tgoa_run (singleton state in vanilla Biome.BIG_TREE_FEATURE).
 * worldIn.getHeight()=256. generate() reads mid-loop writes -> single-threaded.
 *
 * READ-ONLY compose: surface_blocks_real.h, populate.h (algorithm reference only).
 * Output: full ChunkPrimer char[65536] in index order, %04x one per line. */
#ifndef MC_TREE_GEN_BIG_OAK_H
#define MC_TREE_GEN_BIG_OAK_H

#include <math.h>
#include "surface_blocks_real.h"
#include "mc_math.h"

#define TGOA_PLANT_X       8
#define TGOA_PLANT_Z       8
#define TGOA_CHUNK_Y       256
#define TGOA_LOG_OAK       31
#define TGOA_LEAVES_OAK    34
#define TGOA_LOG_OAK_X     37
#define TGOA_LOG_OAK_Z     38
#define TGOA_MAX_FOLIAGE   4096

typedef struct { int x, y, z, branchBase; } TgoaFoliageCoord;

MC_HD MC_NOINLINE static int tgoa_inb(int x, int y, int z) {
    return x >= 0 && x < 16 && y >= 0 && y < 256 && z >= 0 && z < 16;
}
MC_HD MC_NOINLINE static int tgoa_get(const ChunkPrimer *p, int x, int y, int z) {
    return tgoa_inb(x, y, z) ? cb_get(p, x, y, z) : CB_AIR;
}
MC_HD MC_NOINLINE static void tgoa_set(ChunkPrimer *p, int x, int y, int z, int v) {
    if (tgoa_inb(x, y, z)) cb_set(p, x, y, z, v);
}

MC_HD static inline int tgoa_is_air(int c) { return c == CB_AIR; }
MC_HD static inline int tgoa_is_leaves(int c) { return c == TGOA_LEAVES_OAK; }
MC_HD MC_NOINLINE static int tgoa_is_log(int c) {
    return c == TGOA_LOG_OAK || c == TGOA_LOG_OAK_X || c == TGOA_LOG_OAK_Z;
}
MC_HD MC_NOINLINE static int tgoa_is_dirt(int c) {
    return c == CB_DIRT || c == CB_PODZOL || c == CB_COARSE_DIRT;
}
MC_HD MC_NOINLINE static int tgoa_can_grow_into(int c) {
    return tgoa_is_air(c) || tgoa_is_leaves(c) || c == CB_GRASS || tgoa_is_dirt(c) || tgoa_is_log(c);
}
MC_HD MC_NOINLINE static int tgoa_is_replaceable(const ChunkPrimer *p, int x, int y, int z) {
    int c = tgoa_get(p, x, y, z);
    return tgoa_is_air(c) || tgoa_is_leaves(c) || tgoa_is_log(c) || tgoa_can_grow_into(c);
}
MC_HD MC_NOINLINE static int tgoa_can_sustain_plant(int soil) {
    return soil == CB_GRASS || tgoa_is_dirt(soil);
}

MC_HD MC_NOINLINE static int tgoa_plant_y(const ChunkPrimer *p, int x, int z) {
    for (int y = 255; y >= 0; --y) {
        if (tgoa_get(p, x, y, z) != CB_AIR) return y + 1;
    }
    return -1;
}

MC_HD MC_NOINLINE static int tgoa_greatest_distance(int x, int y, int z) {
    int i = x < 0 ? -x : x, j = y < 0 ? -y : y, k = z < 0 ? -z : z;
    return k > i && k > j ? k : (j > i ? j : i);
}
MC_HD MC_NOINLINE static float tgoa_layer_size(int heightLimit, int y) {
    if ((float)y < (float)heightLimit * 0.3F) return -1.0F;
    float f = (float)heightLimit / 2.0F;
    float f1 = f - (float)y;
    float f2 = (float)sqrt((double)(f * f - f1 * f1));
    if (f1 == 0.0F) f2 = f;
    else if ((f1 < 0 ? -f1 : f1) >= f) return 0.0F;
    return f2 * 0.5F;
}
MC_HD MC_NOINLINE static float tgoa_leaf_size(int leafDistanceLimit, int y) {
    return (y >= 0 && y < leafDistanceLimit)
        ? (y != 0 && y != leafDistanceLimit - 1 ? 3.0F : 2.0F) : -1.0F;
}
MC_HD MC_NOINLINE static int tgoa_check_block_line(const ChunkPrimer *p, int x0, int y0, int z0,
        int x1, int y1, int z1) {
    int bx = x1 - x0, by = y1 - y0, bz = z1 - z0;
    int i = tgoa_greatest_distance(bx, by, bz);
    if (i == 0) return -1;
    float f = (float)bx / (float)i, f1 = (float)by / (float)i, f2 = (float)bz / (float)i;
    for (int j = 0; j <= i; ++j) {
        int px = x0 + mc_floor((double)(0.5F + (float)j * f));
        int py = y0 + mc_floor((double)(0.5F + (float)j * f1));
        int pz = z0 + mc_floor((double)(0.5F + (float)j * f2));
        if (!tgoa_is_replaceable(p, px, py, pz)) return j;
    }
    return -1;
}
MC_HD MC_NOINLINE static void tgoa_cross_section(ChunkPrimer *p, int x, int y, int z, float sz, int leaf) {
    int i = (int)((double)sz + 0.618);
    for (int j = -i; j <= i; ++j)
        for (int k = -i; k <= i; ++k) {
            double aj = (j < 0 ? -j : j) + 0.5, ak = (k < 0 ? -k : k) + 0.5;
            if (aj * aj + ak * ak <= (double)(sz * sz)) {
                int c = tgoa_get(p, x + j, y, z + k);
                if (tgoa_is_air(c) || tgoa_is_leaves(c)) tgoa_set(p, x + j, y, z + k, leaf);
            }
        }
}
MC_HD MC_NOINLINE static int tgoa_log_axis(int x0, int z0, int x1, int z1) {
    int i = x1 - x0; if (i < 0) i = -i;
    int j = z1 - z0; if (j < 0) j = -j;
    int k = i > j ? i : j;
    if (k > 0) { if (i == k) return TGOA_LOG_OAK_X; if (j == k) return TGOA_LOG_OAK_Z; }
    return TGOA_LOG_OAK;
}
MC_HD MC_NOINLINE static void tgoa_limb(ChunkPrimer *p, int x0, int y0, int z0, int x1, int y1, int z1) {
    int bx = x1 - x0, by = y1 - y0, bz = z1 - z0;
    int i = tgoa_greatest_distance(bx, by, bz);
    float f = (float)bx / (float)i, f1 = (float)by / (float)i, f2 = (float)bz / (float)i;
    for (int j = 0; j <= i; ++j) {
        int px = x0 + mc_floor((double)(0.5F + (float)j * f));
        int py = y0 + mc_floor((double)(0.5F + (float)j * f1));
        int pz = z0 + mc_floor((double)(0.5F + (float)j * f2));
        tgoa_set(p, px, py, pz, tgoa_log_axis(x0, z0, px, pz));
    }
}

/* Verbatim WorldGenBigTree.generate, ChunkPrimer world. Returns 1/0. */
MC_HD MC_NOINLINE static int tgoa_bigtree(ChunkPrimer *p, JavaRandom *mainr, int posX, int posY, int posZ,
        TgoaFoliageCoord *fol, int *heightLimitOut) {
    int leafDistanceLimit = 5;
    JavaRandom rr;
    jrand_set(&rr, jrand_long(mainr));
    if (*heightLimitOut == 0) *heightLimitOut = 5 + jrand_int_bound(&rr, 12);
    int heightLimit = *heightLimitOut;
    int soil = tgoa_get(p, posX, posY - 1, posZ);
    if (!tgoa_can_sustain_plant(soil)) return 0;
    int chk = tgoa_check_block_line(p, posX, posY, posZ, posX, posY + heightLimit - 1, posZ);
    if (chk == -1) { /* ok */ }
    else if (chk < 6) return 0;
    else heightLimit = chk;
    *heightLimitOut = heightLimit;
    int height = (int)((double)heightLimit * 0.618);
    if (height >= heightLimit) height = heightLimit - 1;
    int ii = (int)(1.382 + pow((double)heightLimit / 13.0, 2.0));
    if (ii < 1) ii = 1;
    int jj = posY + height;
    int kk = heightLimit - leafDistanceLimit;
    int nf = 0;
    fol[nf].x = posX; fol[nf].y = posY + kk; fol[nf].z = posZ; fol[nf].branchBase = jj; ++nf;
    for (; kk >= 0; --kk) {
        float f = tgoa_layer_size(heightLimit, kk);
        if (f >= 0.0F) {
            for (int l = 0; l < ii; ++l) {
                double d0 = 1.0 * (double)f * ((double)jrand_float(&rr) + 0.328);
                double d1 = (double)(jrand_float(&rr) * 2.0F) * MC_PI;
                double d2 = d0 * sin(d1) + 0.5;
                double d3 = d0 * cos(d1) + 0.5;
                int bx = posX + mc_floor(d2);
                int by = posY + (kk - 1);
                int bz = posZ + mc_floor(d3);
                if (tgoa_check_block_line(p, bx, by, bz, bx, by + leafDistanceLimit, bz) == -1) {
                    int i1 = posX - bx, j1 = posZ - bz;
                    double d4 = (double)by - sqrt((double)(i1 * i1 + j1 * j1)) * 0.381;
                    int k1 = d4 > (double)jj ? jj : (int)d4;
                    if (tgoa_check_block_line(p, posX, k1, posZ, bx, by, bz) == -1 && nf < TGOA_MAX_FOLIAGE) {
                        fol[nf].x = bx; fol[nf].y = by; fol[nf].z = bz; fol[nf].branchBase = k1; ++nf;
                    }
                }
            }
        }
    }
    for (int n = 0; n < nf; ++n)
        for (int i = 0; i < leafDistanceLimit; ++i)
            tgoa_cross_section(p, fol[n].x, fol[n].y + i, fol[n].z,
                               tgoa_leaf_size(leafDistanceLimit, i), TGOA_LEAVES_OAK);
    tgoa_limb(p, posX, posY, posZ, posX, posY + height, posZ);
    for (int n = 0; n < nf; ++n) {
        int i = fol[n].branchBase;
        int bx = posX, by = i, bz = posZ;
        if (!(bx == fol[n].x && by == fol[n].y && bz == fol[n].z) &&
            (double)(i - posY) >= (double)heightLimit * 0.2)
            tgoa_limb(p, bx, by, bz, fol[n].x, fol[n].y, fol[n].z);
    }
    return 1;
}

MC_HD MC_NOINLINE static void tgoa_run(ChunkPrimer *primer, CpScratch *sc, i64 seed) {
    sbr_run(primer, sc, seed, 0, 0);
    int plantY = tgoa_plant_y(primer, TGOA_PLANT_X, TGOA_PLANT_Z);
    if (plantY > 0) {
        JavaRandom r;
        jrand_set(&r, seed);
        int heightLimit = 0;
        TgoaFoliageCoord fol[TGOA_MAX_FOLIAGE];
        tgoa_bigtree(primer, &r, TGOA_PLANT_X, plantY, TGOA_PLANT_Z, fol, &heightLimit);
    }
}

#endif /* MC_TREE_GEN_BIG_OAK_H */
