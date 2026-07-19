/* ravines: exact C port of MC 1.11.2 MapGenRavine (net/minecraft/world/gen/MapGenRavine.java)
 * driven through MapGenBase.generate (net/minecraft/world/gen/MapGenBase.java).
 *
 * Worldgen is the one vanilla-bit-exact subsystem (SPEC rule 2): verbatim-Java == CPU == CUDA via
 * the Java 48-bit LCG. Build C with -ffp-contract=off / CUDA with --fmad=false.
 *
 * addTunnel carves an ellipsoidal slot using MathHelper.sin/cos (TABLE-BASED float, core/mc_math.h),
 * not libm. Every (int)/(float)/(double) cast and operator order matches the decompiled Java. RNG
 * is called more than once in several statements (rs-fill f2, the yaw/pitch jitter, f2 in
 * recursiveGenerate, the d1 nested nextInt) - C does NOT sequence those operands, so each uses
 * ORDERED TEMPORARIES (jrand_long is already correctly ordered; reused).
 *
 * Faithful test model (see oracle/goldens/ravines/Golden.java, identical there): a synthetic
 * ChunkPrimer (char[65536], index = x<<12|z<<8|y) filled STONE for y in [1,127] and AIR elsewhere,
 * a FIXED Plains biome (top=GRASS, filler=DIRT, NOT an exception biome), and small integer
 * block-state ids substituted for the Block registry (sanctioned substitution, same in both):
 *   AIR=0, STONE=1, GRASS=2, DIRT=3, FLOWING_WATER=8, WATER=9, FLOWING_LAVA=10
 * (vanilla numeric block ids; the ravine logic only ever calls IBlockState.getBlock(), so block id
 * == cell value is exact). Because the primer has no GRASS/WATER, isTopBlock and isOceanBlock are
 * always false here, but they are ported verbatim so the path is bit-exact.
 *
 * Ravines are rare (recursiveGenerate gates on rand.nextInt(50)==0): for most seeds chunk (0,0) is
 * untouched (all the original primer), which STILL validates the seeding/iteration path bitwise. */
#ifndef MC_RAVINES_H
#define MC_RAVINES_H

#include "mc.h"
#include "mc_rng.h"
#include "mc_math.h"

/* ChunkPrimer: char[65536] in MC; here u16 cells holding the substituted block-state id. */
typedef struct { u16 data[65536]; } RavinePrimer;

/* block-state ids (vanilla numeric block ids; sanctioned integer substitution) */
enum {
    RV_AIR = 0, RV_STONE = 1, RV_GRASS = 2, RV_DIRT = 3,
    RV_FLOWING_WATER = 8, RV_WATER = 9, RV_FLOWING_LAVA = 10
};

/* fixed Plains biome facts (Biome.registerBiomes ids; Plains top=grass, filler=dirt) */
enum {
    RV_BIOME_PLAINS = 1, RV_BIOME_DESERT = 2, RV_BIOME_MUSHROOM = 14,
    RV_BIOME_MUSHROOM_SHORE = 15, RV_BIOME_BEACH = 16
};

/* ChunkPrimer.getBlockIndex = x << 12 | z << 8 | y */
MC_HD static inline int mc_ravine_idx(int x, int y, int z) { return x << 12 | z << 8 | y; }
MC_HD MC_NOINLINE static u16 mc_ravine_get(const RavinePrimer *p, int x, int y, int z) {
    return p->data[mc_ravine_idx(x, y, z)];
}
MC_HD MC_NOINLINE static void mc_ravine_set(RavinePrimer *p, int x, int y, int z, u16 v) {
    p->data[mc_ravine_idx(x, y, z)] = v;
}

typedef struct {
    JavaRandom rand;       /* this.rand (MapGenBase) */
    i64 worldSeed;         /* world.getSeed() */
    int range;             /* MapGenBase.range = 8 */
    float rs[1024];        /* MapGenRavine.rs */
    const McSinTable *st;  /* MathHelper SIN_TABLE */
} MapGenRavine;

/* isExceptionBiome: BEACH, DESERT, MUSHROOM_ISLAND, MUSHROOM_ISLAND_SHORE (Plains -> false). */
MC_HD MC_NOINLINE static int mc_ravine_is_exception_biome(int biome) {
    if (biome == RV_BIOME_BEACH) return 1;
    if (biome == RV_BIOME_DESERT) return 1;
    if (biome == RV_BIOME_MUSHROOM) return 1;
    if (biome == RV_BIOME_MUSHROOM_SHORE) return 1;
    return 0;
}

/* isOceanBlock: block == FLOWING_WATER || block == WATER. */
MC_HD MC_NOINLINE static int mc_ravine_is_ocean(const RavinePrimer *p, int x, int y, int z) {
    int block = mc_ravine_get(p, x, y, z);
    return block == RV_FLOWING_WATER || block == RV_WATER;
}

/* isTopBlock: fixed Plains -> state.getBlock() == topBlock(GRASS). */
MC_HD MC_NOINLINE static int mc_ravine_is_top(const RavinePrimer *p, int x, int y, int z) {
    int biome = RV_BIOME_PLAINS;
    int state = mc_ravine_get(p, x, y, z);
    return mc_ravine_is_exception_biome(biome) ? (state == RV_GRASS) : (state == RV_GRASS);
}

/* digBlock: removes stone/top/filler. y-1<10 -> lava, else air (+ surface dirt->grass on foundTop). */
MC_HD MC_NOINLINE static void mc_ravine_dig(RavinePrimer *p, int x, int y, int z, int foundTop) {
    int biome = RV_BIOME_PLAINS;
    int state = mc_ravine_get(p, x, y, z);
    int top    = mc_ravine_is_exception_biome(biome) ? RV_GRASS : RV_GRASS;  /* Plains topBlock */
    int filler = mc_ravine_is_exception_biome(biome) ? RV_DIRT  : RV_DIRT;   /* Plains fillerBlock */

    if (state == RV_STONE || state == top || state == filler) {
        if (y - 1 < 10) {
            mc_ravine_set(p, x, y, z, RV_FLOWING_LAVA);
        } else {
            mc_ravine_set(p, x, y, z, RV_AIR);
            if (foundTop && mc_ravine_get(p, x, y - 1, z) == filler) {
                mc_ravine_set(p, x, y - 1, z, (u16)top);
            }
        }
    }
}

/* MapGenRavine.addTunnel (verbatim). p3=chunkX, p4=chunkZ (chunk being carved). */
MC_HD MC_NOINLINE static void mc_ravine_add_tunnel(MapGenRavine *mg, RavinePrimer *primer,
        i64 p1, int p3, int p4, double p6, double p8, double p10,
        float p12, float p13, float p14, int p15, int p16, double p17) {
    JavaRandom random; jrand_set(&random, p1);
    double d0 = (double)(p3 * 16 + 8);
    double d1 = (double)(p4 * 16 + 8);
    float f = 0.0F;
    float f1 = 0.0F;

    if (p16 <= 0) {
        int i = mg->range * 16 - 16;
        p16 = i - jrand_int_bound(&random, i / 4);
    }

    int flag1 = 0;

    if (p15 == -1) {
        p15 = p16 / 2;
        flag1 = 1;
    }

    float f2 = 1.0F;

    for (int j = 0; j < 256; ++j) {
        if (j == 0 || jrand_int_bound(&random, 3) == 0) {
            /* f2 = 1.0F + nextFloat() * nextFloat(); ordered temporaries */
            float a = jrand_float(&random);
            float b = jrand_float(&random);
            f2 = 1.0F + a * b;
        }

        mg->rs[j] = f2 * f2;
    }

    for (; p15 < p16; ++p15) {
        double d9 = 1.5 + (double)(mc_sin(mg->st, (float)p15 * (float)MC_PI / (float)p16) * p12);
        double d2 = d9 * p17;
        d9 = d9 * ((double)jrand_float(&random) * 0.25 + 0.75);
        d2 = d2 * ((double)jrand_float(&random) * 0.25 + 0.75);
        float f3 = mc_cos(mg->st, p14);
        float f4 = mc_sin(mg->st, p14);
        p6 += (double)(mc_cos(mg->st, p13) * f3);
        p8 += (double)f4;
        p10 += (double)(mc_sin(mg->st, p13) * f3);
        p14 = p14 * 0.7F;
        p14 = p14 + f1 * 0.05F;
        p13 += f * 0.05F;
        f1 = f1 * 0.8F;
        f = f * 0.5F;
        /* f1 += (nextFloat() - nextFloat()) * nextFloat() * 2.0F; ordered temporaries */
        {
            float a = jrand_float(&random);
            float b = jrand_float(&random);
            float c = jrand_float(&random);
            f1 = f1 + (a - b) * c * 2.0F;
        }
        /* f += (nextFloat() - nextFloat()) * nextFloat() * 4.0F; ordered temporaries */
        {
            float a = jrand_float(&random);
            float b = jrand_float(&random);
            float c = jrand_float(&random);
            f = f + (a - b) * c * 4.0F;
        }

        if (flag1 || jrand_int_bound(&random, 4) != 0) {
            double d3 = p6 - d0;
            double d4 = p10 - d1;
            double d5 = (double)(p16 - p15);
            double d6 = (double)(p12 + 2.0F + 16.0F);

            if (d3 * d3 + d4 * d4 - d5 * d5 > d6 * d6) {
                return;
            }

            if (p6 >= d0 - 16.0 - d9 * 2.0 && p10 >= d1 - 16.0 - d9 * 2.0 && p6 <= d0 + 16.0 + d9 * 2.0 && p10 <= d1 + 16.0 + d9 * 2.0) {
                int k2 = mc_floor(p6 - d9) - p3 * 16 - 1;
                int k = mc_floor(p6 + d9) - p3 * 16 + 1;
                int l2 = mc_floor(p8 - d2) - 1;
                int l = mc_floor(p8 + d2) + 1;
                int i3 = mc_floor(p10 - d9) - p4 * 16 - 1;
                int i1 = mc_floor(p10 + d9) - p4 * 16 + 1;

                if (k2 < 0) { k2 = 0; }
                if (k > 16) { k = 16; }
                if (l2 < 1) { l2 = 1; }
                if (l > 248) { l = 248; }
                if (i3 < 0) { i3 = 0; }
                if (i1 > 16) { i1 = 16; }

                int flag2 = 0;

                for (int j1 = k2; !flag2 && j1 < k; ++j1) {
                    for (int k1 = i3; !flag2 && k1 < i1; ++k1) {
                        for (int l1 = l + 1; !flag2 && l1 >= l2 - 1; --l1) {
                            if (l1 >= 0 && l1 < 256) {
                                if (mc_ravine_is_ocean(primer, j1, l1, k1)) {
                                    flag2 = 1;
                                }

                                if (l1 != l2 - 1 && j1 != k2 && j1 != k - 1 && k1 != i3 && k1 != i1 - 1) {
                                    l1 = l2;
                                }
                            }
                        }
                    }
                }

                if (!flag2) {
                    for (int j3 = k2; j3 < k; ++j3) {
                        double d10 = ((double)(j3 + p3 * 16) + 0.5 - p6) / d9;

                        for (int i2 = i3; i2 < i1; ++i2) {
                            double d7 = ((double)(i2 + p4 * 16) + 0.5 - p10) / d9;
                            int flag = 0;

                            if (d10 * d10 + d7 * d7 < 1.0) {
                                for (int j2 = l; j2 > l2; --j2) {
                                    double d8 = ((double)(j2 - 1) + 0.5 - p8) / d2;

                                    if ((d10 * d10 + d7 * d7) * (double)mg->rs[j2 - 1] + d8 * d8 / 6.0 < 1.0) {
                                        if (mc_ravine_is_top(primer, j3, j2, i2)) {
                                            flag = 1;
                                        }

                                        mc_ravine_dig(primer, j3, j2, i2, flag);
                                    }
                                }
                            }
                        }
                    }

                    if (flag1) {
                        break;
                    }
                }
            }
        }
    }
}

/* MapGenRavine.recursiveGenerate (verbatim). p4=x, p5=z (central chunk being generated). */
MC_HD MC_NOINLINE static void mc_ravine_recursive(MapGenRavine *mg, int chunkX, int chunkZ,
                                             int p4, int p5, RavinePrimer *primer) {
    if (jrand_int_bound(&mg->rand, 50) == 0) {
        double d0 = (double)(chunkX * 16 + jrand_int_bound(&mg->rand, 16));
        /* d1: nextInt(nextInt(40)+8)+20; inner first (Java evals arg before outer call) */
        int inner = jrand_int_bound(&mg->rand, 40);
        double d1 = (double)(jrand_int_bound(&mg->rand, inner + 8) + 20);
        double d2 = (double)(chunkZ * 16 + jrand_int_bound(&mg->rand, 16));

        for (int j = 0; j < 1; ++j) {
            float f = jrand_float(&mg->rand) * ((float)MC_PI * 2.0F);
            float f1 = (jrand_float(&mg->rand) - 0.5F) * 2.0F / 8.0F;
            /* f2 = (nextFloat()*2 + nextFloat())*2; ordered temporaries */
            float a = jrand_float(&mg->rand);
            float b = jrand_float(&mg->rand);
            float f2 = (a * 2.0F + b) * 2.0F;
            i64 tunnelSeed = jrand_long(&mg->rand);
            mc_ravine_add_tunnel(mg, primer, tunnelSeed, p4, p5, d0, d1, d2, f2, f, f1, 0, 0, 3.0);
        }
    }
}

/* MapGenBase.generate (verbatim): range loop with per-chunk setSeed(l*j ^ i1*k ^ worldSeed). */
MC_HD MC_NOINLINE static void mc_ravine_generate(MapGenRavine *mg, RavinePrimer *primer, int x, int z) {
    int i = mg->range;
    jrand_set(&mg->rand, mg->worldSeed);
    i64 j = jrand_long(&mg->rand);
    i64 k = jrand_long(&mg->rand);

    for (int l = x - i; l <= x + i; ++l) {
        for (int i1 = z - i; i1 <= z + i; ++i1) {
            i64 j1 = (i64)l * j;
            i64 k1 = (i64)i1 * k;
            jrand_set(&mg->rand, j1 ^ k1 ^ mg->worldSeed);
            mc_ravine_recursive(mg, l, i1, x, z, primer);
        }
    }
}

#endif /* MC_RAVINES_H */
