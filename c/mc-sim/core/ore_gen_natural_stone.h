/* ore_gen_natural_stone: WorldGenMinable.generate on REAL caves_real primer (cvr_run) with
 * StonePredicate = all natural stone variants (stone, granite, diorite, andesite), not meta-0
 * stone only. Fixes the Wave-1 ore_gen harness gap (cell == naturalStone single-id check).
 *
 * Pipeline for chunk (0,0):
 *   1. cvr_run -> MapGenCaves on sbr_run primer + genlayer biomes
 *   2. WorldGenMinable.generate at fixed (OGNS_POS_*) with decoration RNG = world seed
 *
 * Block ids: CB_* from chunk_provider + populate-compatible ore/stone-variant codes (21-23, 28).
 * generate() reads mid-loop writes -> single-threaded.
 *
 * READ-ONLY compose: caves_real.h, ore_gen.h (algorithm reference), surface_blocks_real.h.
 * Output: ChunkPrimer char[65536] in index order, %04x one per line. */
#ifndef MC_ORE_GEN_NATURAL_STONE_H
#define MC_ORE_GEN_NATURAL_STONE_H

#include "caves_real.h"

#define OGNS_GRANITE     21
#define OGNS_DIORITE     22
#define OGNS_ANDESITE    23
#define OGNS_DIAMOND_ORE 28

#define OGNS_POS_X 8
#define OGNS_POS_Y 24
#define OGNS_POS_Z 8
#define OGNS_SIZE  33

MC_HD MC_NOINLINE static int ogns_is_natural_stone(int c) {
    return c == CB_STONE || c == OGNS_GRANITE || c == OGNS_DIORITE || c == OGNS_ANDESITE;
}

MC_HD MC_NOINLINE static int ogns_get(const ChunkPrimer *p, int x, int y, int z) {
    if (x < 0 || x >= 16 || y < 0 || y >= 256 || z < 0 || z >= 16) return CB_AIR;
    return cb_get(p, x, y, z);
}

MC_HD MC_NOINLINE static void ogns_set(ChunkPrimer *p, int x, int y, int z, int v) {
    if (x >= 0 && x < 16 && y >= 0 && y < 256 && z >= 0 && z < 16)
        cb_set(p, x, y, z, v);
}

/* Verbatim WorldGenMinable.generate (StonePredicate = natural stone), ChunkPrimer world. */
MC_HD MC_NOINLINE static void ogns_minable(ChunkPrimer *p, const McSinTable *st, JavaRandom *r,
                                      int posX, int posY, int posZ, int numberOfBlocks,
                                      int oreBlock) {
    float f = jrand_float(r) * (float)MC_PI;
    double d0 = (double)((float)(posX + 8) + mc_sin(st, f) * (float)numberOfBlocks / 8.0F);
    double d1 = (double)((float)(posX + 8) - mc_sin(st, f) * (float)numberOfBlocks / 8.0F);
    double d2 = (double)((float)(posZ + 8) + mc_cos(st, f) * (float)numberOfBlocks / 8.0F);
    double d3 = (double)((float)(posZ + 8) - mc_cos(st, f) * (float)numberOfBlocks / 8.0F);
    double d4 = (double)(posY + jrand_int_bound(r, 3) - 2);
    double d5 = (double)(posY + jrand_int_bound(r, 3) - 2);
    for (int i = 0; i < numberOfBlocks; ++i) {
        float f1 = (float)i / (float)numberOfBlocks;
        double d6 = d0 + (d1 - d0) * (double)f1;
        double d7 = d4 + (d5 - d4) * (double)f1;
        double d8 = d2 + (d3 - d2) * (double)f1;
        double d9 = jrand_double(r) * (double)numberOfBlocks / 16.0;
        double d10 = (double)(mc_sin(st, (float)MC_PI * f1) + 1.0F) * d9 + 1.0;
        double d11 = (double)(mc_sin(st, (float)MC_PI * f1) + 1.0F) * d9 + 1.0;
        int j = mc_floor(d6 - d10 / 2.0);
        int k = mc_floor(d7 - d11 / 2.0);
        int l = mc_floor(d8 - d10 / 2.0);
        int i1 = mc_floor(d6 + d10 / 2.0);
        int j1 = mc_floor(d7 + d11 / 2.0);
        int k1 = mc_floor(d8 + d10 / 2.0);
        for (int l1 = j; l1 <= i1; ++l1) {
            double d12 = ((double)l1 + 0.5 - d6) / (d10 / 2.0);
            if (d12 * d12 < 1.0) {
                for (int i2 = k; i2 <= j1; ++i2) {
                    double d13 = ((double)i2 + 0.5 - d7) / (d11 / 2.0);
                    if (d12 * d12 + d13 * d13 < 1.0) {
                        for (int j2 = l; j2 <= k1; ++j2) {
                            double d14 = ((double)j2 + 0.5 - d8) / (d10 / 2.0);
                            if (d12 * d12 + d13 * d13 + d14 * d14 < 1.0) {
                                if (ogns_is_natural_stone(ogns_get(p, l1, i2, j2)))
                                    ogns_set(p, l1, i2, j2, oreBlock);
                            }
                        }
                    }
                }
            }
        }
    }
}

MC_HD MC_NOINLINE static void ogns_run(ChunkPrimer *primer, CvrScratch *ctx, i64 seed,
                                const McSinTable *st) {
    cvr_run(primer, ctx, seed, 0, 0, st);
    JavaRandom r;
    jrand_set(&r, seed);
    ogns_minable(primer, st, &r, OGNS_POS_X, OGNS_POS_Y, OGNS_POS_Z, OGNS_SIZE, OGNS_DIAMOND_ORE);
}

#endif /* MC_ORE_GEN_NATURAL_STONE_H */
