/* caves: exact C port of MC 1.11.2 cave carving:
 *   net/minecraft/world/gen/MapGenBase.java   : generate() driver loop (per-chunk setSeed)
 *   net/minecraft/world/gen/MapGenCaves.java   : recursiveGenerate/addRoom/addTunnel + helpers
 *
 * Worldgen is the one vanilla-bit-exact subsystem (SPEC rule 2): checked verbatim-Java == CPU ==
 * CUDA via the Java LCG. addTunnel uses MathHelper.sin/cos (TABLE-based float) -> core/mc_math.h
 * (never libm on device). Build C with -ffp-contract=off / CUDA with --fmad=false.
 *
 * DEPENDENCY DECOUPLING (identical in this header and oracle/goldens/caves/Golden.java):
 *  - Synthetic primer: every ChunkPrimer cell = STONE (the clean all-stone carve target, same idea
 *    as ore_gen's stone cube). Caves only carve where canReplaceBlock is true; stone qualifies.
 *  - World/Biome registry -> a FIXED Plains biome: world.getBiome(...) == Plains everywhere, so
 *    topBlock = GRASS, fillerBlock = DIRT, and isExceptionBiome(Plains) == false. Source: BiomePlains
 *    inherits Biome.topBlock=Blocks.GRASS / Biome.fillerBlock=Blocks.DIRT (Biome.java:98,100); the
 *    only carving "exception biomes" are BEACH and DESERT (MapGenCaves.isExceptionBiome), so Plains
 *    is not one.
 *  - Block-state ids -> small integer constants (sanctioned substitution; meta is 0 throughout this
 *    synthetic test, so a block-state id reduces to its block id). Values are the vanilla numeric
 *    block ids (Block.getIdFromBlock). The ChunkPrimer cell stores this id.
 *
 * C-vs-Java traps handled: every place java.util.Random is drawn more than once per statement uses
 * ordered temporaries (the per-step `(nextFloat()-nextFloat())*nextFloat()` lines, the f2 lines, the
 * nested nextInt in recursiveGenerate, the nextLong/nextFloat pairs feeding the two recursive
 * addTunnel calls). jrand_long is already ordered. 64-bit wrap for the per-chunk setSeed math. */
#ifndef MC_CAVES_H
#define MC_CAVES_H

#include "mc.h"
#include "mc_rng.h"
#include "mc_math.h"

/* ===== block-state id substitution (vanilla numeric block ids; meta always 0 here) ===== */
enum {
    CV_AIR = 0, CV_STONE = 1, CV_GRASS = 2, CV_DIRT = 3,
    CV_FLOWING_WATER = 8, CV_WATER = 9, CV_FLOWING_LAVA = 10, CV_LAVA = 11,
    CV_SAND = 12, CV_GRAVEL = 13, CV_SANDSTONE = 24, CV_SNOW_LAYER = 78,
    CV_MYCELIUM = 110, CV_STAINED_HARDENED_CLAY = 159, CV_HARDENED_CLAY = 172,
    CV_RED_SANDSTONE = 179
};

/* MapGenCaves does not override MapGenBase.range (default 8). */
#define CV_RANGE 8

/* ===== ChunkPrimer (verbatim-equivalent of world/chunk/ChunkPrimer) =====
 * char[65536] in vanilla; index = x<<12 | z<<8 | y. Stores the block-state id (here = block id). */
typedef struct { u16 data[65536]; } ChunkPrimer;
MC_HD static inline int  cp_index(int x, int y, int z) { return x << 12 | z << 8 | y; }
MC_HD static inline u16  cp_get(const ChunkPrimer *p, int x, int y, int z) { return p->data[cp_index(x, y, z)]; }
MC_HD static inline void cp_set(ChunkPrimer *p, int x, int y, int z, u16 v) { p->data[cp_index(x, y, z)] = v; }

/* Material.WATER test on a block-state id (canReplaceBlock's up-block check). */
MC_HD static inline int cv_is_water_material(u16 id) { return id == CV_WATER || id == CV_FLOWING_WATER; }

/* MapGenCaves.canReplaceBlock (verbatim nested ternary; only stone/dirt/grass ever match here). */
MC_HD MC_NOINLINE static int cv_canReplaceBlock(u16 s, u16 up) {
    return s == CV_STONE ? 1 : (s == CV_DIRT ? 1 : (s == CV_GRASS ? 1 : (s == CV_HARDENED_CLAY ? 1 :
           (s == CV_STAINED_HARDENED_CLAY ? 1 : (s == CV_SANDSTONE ? 1 : (s == CV_RED_SANDSTONE ? 1 :
           (s == CV_MYCELIUM ? 1 : (s == CV_SNOW_LAYER ? 1 :
           ((s == CV_SAND || s == CV_GRAVEL) && !cv_is_water_material(up))))))))));
}

/* MapGenCaves.isOceanBlock: block == FLOWING_WATER || WATER. */
MC_HD MC_NOINLINE static int cv_isOceanBlock(const ChunkPrimer *p, int x, int y, int z) {
    u16 b = cp_get(p, x, y, z);
    return b == CV_FLOWING_WATER || b == CV_WATER;
}

/* MapGenCaves.isTopBlock with biome fixed to Plains (non-exception):
 *   state.getBlock() == biome.topBlock; topBlock = GRASS -> state == GRASS. */
MC_HD MC_NOINLINE static int cv_isTopBlock(const ChunkPrimer *p, int x, int y, int z) {
    u16 state = cp_get(p, x, y, z);
    return state == CV_GRASS;
}

/* MapGenCaves.digBlock with biome fixed to Plains: top = GRASS, filler = DIRT.
 * BLK_LAVA = Blocks.LAVA.getDefaultState() (still lava); air = Blocks.AIR.getDefaultState(). */
MC_HD MC_NOINLINE static void cv_digBlock(ChunkPrimer *p, int x, int y, int z, int foundTop, u16 state, u16 up) {
    if (cv_canReplaceBlock(state, up) || state == CV_GRASS || state == CV_DIRT) {
        if (y - 1 < 10) {
            cp_set(p, x, y, z, CV_LAVA);
        } else {
            cp_set(p, x, y, z, CV_AIR);

            if (foundTop && cp_get(p, x, y - 1, z) == CV_DIRT) {
                cp_set(p, x, y - 1, z, CV_GRASS);
            }
        }
    }
}

/* MapGenCaves.addTunnel (verbatim; recursive once via the random-branch into two child tunnels). */
MC_HD MC_NOINLINE static void cv_addTunnel(ChunkPrimer *primer, i64 p_1, int p_3, int p_4,
                                      double p_6, double p_8, double p_10, float p_12, float p_13,
                                      float p_14, int p_15, int p_16, double p_17,
                                      const McSinTable *st) {
    double d0 = (double)(p_3 * 16 + 8);
    double d1 = (double)(p_4 * 16 + 8);
    float f = 0.0F;
    float f1 = 0.0F;
    JavaRandom random; jrand_set(&random, p_1);

    if (p_16 <= 0) {
        int i = CV_RANGE * 16 - 16;
        int t = jrand_int_bound(&random, i / 4);
        p_16 = i - t;
    }

    int flag2 = 0;

    if (p_15 == -1) {
        p_15 = p_16 / 2;
        flag2 = 1;
    }

    int j = jrand_int_bound(&random, p_16 / 2) + p_16 / 4;

    int flag = (jrand_int_bound(&random, 6) == 0);
    for (; p_15 < p_16; ++p_15) {
        double d2 = 1.5 + (double)(mc_sin(st, (float)p_15 * (float)MC_PI / (float)p_16) * p_12);
        double d3 = d2 * p_17;
        float f2 = mc_cos(st, p_14);
        float f3 = mc_sin(st, p_14);
        p_6 += (double)(mc_cos(st, p_13) * f2);
        p_8 += (double)f3;
        p_10 += (double)(mc_sin(st, p_13) * f2);

        if (flag) {
            p_14 = p_14 * 0.92F;
        } else {
            p_14 = p_14 * 0.7F;
        }

        p_14 = p_14 + f1 * 0.1F;
        p_13 += f * 0.1F;
        f1 = f1 * 0.9F;
        f = f * 0.75F;
        {
            float a = jrand_float(&random);
            float b = jrand_float(&random);
            float c = jrand_float(&random);
            f1 = f1 + (a - b) * c * 2.0F;
        }
        {
            float a = jrand_float(&random);
            float b = jrand_float(&random);
            float c = jrand_float(&random);
            f = f + (a - b) * c * 4.0F;
        }

        if (!flag2 && p_15 == j && p_12 > 1.0F && p_16 > 0) {
            i64 seed1 = jrand_long(&random);
            float sz1 = jrand_float(&random) * 0.5F + 0.5F;
            cv_addTunnel(primer, seed1, p_3, p_4, p_6, p_8, p_10, sz1,
                         p_13 - ((float)MC_PI / 2.0F), p_14 / 3.0F, p_15, p_16, 1.0, st);
            i64 seed2 = jrand_long(&random);
            float sz2 = jrand_float(&random) * 0.5F + 0.5F;
            cv_addTunnel(primer, seed2, p_3, p_4, p_6, p_8, p_10, sz2,
                         p_13 + ((float)MC_PI / 2.0F), p_14 / 3.0F, p_15, p_16, 1.0, st);
            return;
        }

        if (flag2 || jrand_int_bound(&random, 4) != 0) {
            double d4 = p_6 - d0;
            double d5 = p_10 - d1;
            double d6 = (double)(p_16 - p_15);
            double d7 = (double)(p_12 + 2.0F + 16.0F);

            if (d4 * d4 + d5 * d5 - d6 * d6 > d7 * d7) {
                return;
            }

            if (p_6 >= d0 - 16.0 - d2 * 2.0 && p_10 >= d1 - 16.0 - d2 * 2.0 &&
                p_6 <= d0 + 16.0 + d2 * 2.0 && p_10 <= d1 + 16.0 + d2 * 2.0) {
                int k2 = mc_floor(p_6 - d2) - p_3 * 16 - 1;
                int k = mc_floor(p_6 + d2) - p_3 * 16 + 1;
                int l2 = mc_floor(p_8 - d3) - 1;
                int l = mc_floor(p_8 + d3) + 1;
                int i3 = mc_floor(p_10 - d2) - p_4 * 16 - 1;
                int i1 = mc_floor(p_10 + d2) - p_4 * 16 + 1;

                if (k2 < 0) { k2 = 0; }
                if (k > 16) { k = 16; }
                if (l2 < 1) { l2 = 1; }
                if (l > 248) { l = 248; }
                if (i3 < 0) { i3 = 0; }
                if (i1 > 16) { i1 = 16; }

                int flag3 = 0;

                for (int j1 = k2; !flag3 && j1 < k; ++j1) {
                    for (int k1 = i3; !flag3 && k1 < i1; ++k1) {
                        for (int l1 = l + 1; !flag3 && l1 >= l2 - 1; --l1) {
                            if (l1 >= 0 && l1 < 256) {
                                if (cv_isOceanBlock(primer, j1, l1, k1)) {
                                    flag3 = 1;
                                }

                                if (l1 != l2 - 1 && j1 != k2 && j1 != k - 1 && k1 != i3 && k1 != i1 - 1) {
                                    l1 = l2;
                                }
                            }
                        }
                    }
                }

                if (!flag3) {
                    for (int j3 = k2; j3 < k; ++j3) {
                        double d10 = ((double)(j3 + p_3 * 16) + 0.5 - p_6) / d2;

                        for (int i2 = i3; i2 < i1; ++i2) {
                            double d8 = ((double)(i2 + p_4 * 16) + 0.5 - p_10) / d2;
                            int flag1 = 0;

                            if (d10 * d10 + d8 * d8 < 1.0) {
                                for (int j2 = l; j2 > l2; --j2) {
                                    double d9 = ((double)(j2 - 1) + 0.5 - p_8) / d3;

                                    if (d9 > -0.7 && d10 * d10 + d9 * d9 + d8 * d8 < 1.0) {
                                        u16 iblockstate1 = cp_get(primer, j3, j2, i2);
                                        u16 iblockstate2 = cp_get(primer, j3, j2 + 1, i2);

                                        if (cv_isTopBlock(primer, j3, j2, i2)) {
                                            flag1 = 1;
                                        }

                                        cv_digBlock(primer, j3, j2, i2, flag1, iblockstate1, iblockstate2);
                                    }
                                }
                            }
                        }
                    }

                    if (flag2) {
                        break;
                    }
                }
            }
        }
    }
}

/* MapGenCaves.addRoom: one wide tunnel; the size nextFloat is drawn from the instance rand. */
MC_HD MC_NOINLINE static void cv_addRoom(ChunkPrimer *primer, JavaRandom *rand, i64 seed,
                                    int p_3, int p_4, double x, double y, double z,
                                    const McSinTable *st) {
    float sz = 1.0F + jrand_float(rand) * 6.0F;
    cv_addTunnel(primer, seed, p_3, p_4, x, y, z, sz, 0.0F, 0.0F, -1, -1, 0.5, st);
}

/* MapGenCaves.recursiveGenerate. */
MC_HD MC_NOINLINE static void cv_recursiveGenerate(ChunkPrimer *primer, JavaRandom *rand,
                                              int chunkX, int chunkZ, int p_4, int p_5,
                                              const McSinTable *st) {
    int n0 = jrand_int_bound(rand, 15);
    int n1 = jrand_int_bound(rand, n0 + 1);
    int i = jrand_int_bound(rand, n1 + 1);

    if (jrand_int_bound(rand, 7) != 0) {
        i = 0;
    }

    for (int jj = 0; jj < i; ++jj) {
        int r0 = jrand_int_bound(rand, 16);
        double d0 = (double)(chunkX * 16 + r0);
        int nn = jrand_int_bound(rand, 120);
        double d1 = (double)jrand_int_bound(rand, nn + 8);
        int r2 = jrand_int_bound(rand, 16);
        double d2 = (double)(chunkZ * 16 + r2);
        int k = 1;

        if (jrand_int_bound(rand, 4) == 0) {
            i64 roomSeed = jrand_long(rand);
            cv_addRoom(primer, rand, roomSeed, p_4, p_5, d0, d1, d2, st);
            k += jrand_int_bound(rand, 4);
        }

        for (int l = 0; l < k; ++l) {
            float f = jrand_float(rand) * ((float)MC_PI * 2.0F);
            float f1 = (jrand_float(rand) - 0.5F) * 2.0F / 8.0F;
            float fa = jrand_float(rand);
            float fb = jrand_float(rand);
            float f2 = fa * 2.0F + fb;

            if (jrand_int_bound(rand, 10) == 0) {
                float fc = jrand_float(rand);
                float fd = jrand_float(rand);
                f2 *= fc * fd * 3.0F + 1.0F;
            }

            i64 tunSeed = jrand_long(rand);
            cv_addTunnel(primer, tunSeed, p_4, p_5, d0, d1, d2, f2, f, f1, 0, 0, 1.0, st);
        }
    }
}

/* MapGenBase.generate: seed the instance rand from the world seed, draw j/k, then for every chunk
 * in the [x-range, x+range] x [z-range, z+range] neighborhood setSeed and recursiveGenerate. */
MC_HD MC_NOINLINE static void cv_generate(ChunkPrimer *primer, i64 worldSeed, int x, int z,
                                     const McSinTable *st) {
    int i = CV_RANGE;
    JavaRandom rand; jrand_set(&rand, worldSeed);
    i64 j = jrand_long(&rand);
    i64 k = jrand_long(&rand);

    for (int l = x - i; l <= x + i; ++l) {
        for (int i1 = z - i; i1 <= z + i; ++i1) {
            i64 j1 = (i64)l * j;
            i64 k1 = (i64)i1 * k;
            jrand_set(&rand, j1 ^ k1 ^ worldSeed);
            cv_recursiveGenerate(primer, &rand, l, i1, x, z, st);
        }
    }
}

#endif /* MC_CAVES_H */
