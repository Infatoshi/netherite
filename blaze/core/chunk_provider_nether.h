/* chunk_provider_nether: MC 1.11.2 ChunkProviderHell.provideChunk (net/minecraft/world/gen/ChunkProviderHell.java)
 * MINUS new Chunk / biome array / populate. Pipeline: prepareHeights (netherrack + lava sea) ->
 * buildSurfaces (soul sand / gravel / bedrock) -> MapGenCavesHell -> MapGenNetherBridge (via
 * READ-ONLY map_gen_fortress.h). Block-state ids = vanilla numeric (meta 0), matching fortress.
 * Large noise state (CpnHellNoise ~450KB) and scratch on HEAP malloc, not device stack.
 * REUSES: mc_rng, mc_noise, mc_math (hell caves), map_gen_fortress (fortress placement). */
#ifndef MC_CHUNK_PROVIDER_NETHER_H
#define MC_CHUNK_PROVIDER_NETHER_H

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mc.h"
#include "mc_rng.h"
#include "mc_noise.h"
#include "mc_math.h"
#include "map_gen_fortress.h"

/* Vanilla 1.11.2 block ids (Block.java registerBlocks): 10=flowing_lava, 11=lava still.
 * prepareHeights/buildSurfaces place Blocks.LAVA (still); caves only test for either. */
enum {
    CPN_AIR = 0, CPN_STONE = 1, CPN_GRASS = 2, CPN_DIRT = 3, CPN_BEDROCK = 7,
    CPN_FLOWING_LAVA = 10, CPN_LAVA = 11, CPN_GRAVEL = 13,
    CPN_NETHERRACK = 87, CPN_SOUL_SAND = 88
};

#define CPN_SEA_LEVEL 63

typedef struct { u16 data[65536]; } CpnPrimer;

MC_HD static inline int cpn_idx(int x, int y, int z) { return x << 12 | z << 8 | y; }
MC_HD MC_NOINLINE static int cpn_get(const CpnPrimer *p, int x, int y, int z) {
    return (int)p->data[cpn_idx(x, y, z)];
}
MC_HD MC_NOINLINE static void cpn_set(CpnPrimer *p, int x, int y, int z, int v) {
    p->data[cpn_idx(x, y, z)] = (u16)v;
}

/* seven NoiseGeneratorOctaves from ChunkProviderHell ctor draw order */
typedef struct {
    NoiseOctaves lperlin1;       /* 16 */
    NoiseOctaves lperlin2;       /* 16 */
    NoiseOctaves perlin1;        /* 8  */
    NoiseOctaves slowsandGravel; /* 4  */
    NoiseOctaves netherrackExcl; /* 4  */
    NoiseOctaves scaleNoise;     /* 10 */
    NoiseOctaves depthNoise;     /* 16 */
} CpnHellNoise;

typedef struct {
    double buffer[425];
    double noiseData4[25];
    double dr[25];
    double pnr[425];
    double ar[425];
    double br[425];
    double slowsandNoise[256];
    double gravelNoise[256];
    double depthBuffer[256];
    FtGen ftgen;   /* was malloc(sizeof(FtGen)) in nether_full.h nf_run (no in-kernel malloc) */
} CpnHellScratch;

MC_HD MC_NOINLINE static double cpn_clamp(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

MC_HD MC_NOINLINE static void cpn_noise_init(CpnHellNoise *n, i64 seed) {
    JavaRandom r; jrand_set(&r, seed);
    mc_oct_init(&n->lperlin1, &r, 16);
    mc_oct_init(&n->lperlin2, &r, 16);
    mc_oct_init(&n->perlin1, &r, 8);
    mc_oct_init(&n->slowsandGravel, &r, 4);
    mc_oct_init(&n->netherrackExcl, &r, 4);
    mc_oct_init(&n->scaleNoise, &r, 10);
    mc_oct_init(&n->depthNoise, &r, 16);
}

/* ChunkProviderHell.getHeights (Forge event skipped; scale/depth sampled but unused in vanilla loop). */
MC_HD MC_NOINLINE static void cpn_get_heights(CpnHellScratch *sc, const CpnHellNoise *n,
        int p2, int p3, int p4) {
    mc_oct_generate(&n->scaleNoise, sc->noiseData4, p2, p3, p4, 5, 1, 5, 1.0, 0.0, 1.0);
    mc_oct_generate(&n->depthNoise, sc->dr, p2, p3, p4, 5, 1, 5, 100.0, 0.0, 100.0);
    mc_oct_generate(&n->perlin1, sc->pnr, p2, p3, p4, 5, 17, 5,
                    8.555150000000001, 34.2206, 8.555150000000001);
    mc_oct_generate(&n->lperlin1, sc->ar, p2, p3, p4, 5, 17, 5, 684.412, 2053.236, 684.412);
    mc_oct_generate(&n->lperlin2, sc->br, p2, p3, p4, 5, 17, 5, 684.412, 2053.236, 684.412);

    double adouble[17];
    for (int j = 0; j < 17; ++j) {
        adouble[j] = cos((double)j * MC_PI * 6.0 / 17.0) * 2.0;
        double d2 = (double)j;
        if (j > 17 / 2) d2 = (double)(17 - 1 - j);
        if (d2 < 4.0) {
            d2 = 4.0 - d2;
            adouble[j] -= d2 * d2 * d2 * 10.0;
        }
    }

    int i = 0;
    for (int l = 0; l < 5; ++l) {
        for (int i1 = 0; i1 < 5; ++i1) {
            for (int k = 0; k < 17; ++k) {
                double d4 = adouble[k];
                double d5 = sc->ar[i] / 512.0;
                double d6 = sc->br[i] / 512.0;
                double d7 = (sc->pnr[i] / 10.0 + 1.0) / 2.0;
                double d8;
                if (d7 < 0.0) d8 = d5;
                else if (d7 > 1.0) d8 = d6;
                else d8 = d5 + (d6 - d5) * d7;
                d8 = d8 - d4;
                if (k > 17 - 4) {
                    double d9 = (double)((float)(k - (17 - 4)) / 3.0f);
                    d8 = d8 * (1.0 - d9) + -10.0 * d9;
                }
                if ((double)k < 0.0) {
                    double d10 = (0.0 - (double)k) / 4.0;
                    d10 = cpn_clamp(d10, 0.0, 1.0);
                    d8 = d8 * (1.0 - d10) + -10.0 * d10;
                }
                sc->buffer[i] = d8;
                ++i;
            }
        }
    }
}

/* ChunkProviderHell.prepareHeights */
MC_HD MC_NOINLINE static void cpn_prepare_heights(CpnHellScratch *sc, const CpnHellNoise *n,
        int chunkX, int chunkZ, CpnPrimer *primer) {
    int j = CPN_SEA_LEVEL / 2 + 1;
    cpn_get_heights(sc, n, chunkX * 4, 0, chunkZ * 4);

    for (int j1 = 0; j1 < 4; ++j1) {
        for (int k1 = 0; k1 < 4; ++k1) {
            for (int l1 = 0; l1 < 16; ++l1) {
                double d1 = sc->buffer[((j1 + 0) * 5 + k1 + 0) * 17 + l1 + 0];
                double d2 = sc->buffer[((j1 + 0) * 5 + k1 + 1) * 17 + l1 + 0];
                double d3 = sc->buffer[((j1 + 1) * 5 + k1 + 0) * 17 + l1 + 0];
                double d4 = sc->buffer[((j1 + 1) * 5 + k1 + 1) * 17 + l1 + 0];
                double d5 = (sc->buffer[((j1 + 0) * 5 + k1 + 0) * 17 + l1 + 1] - d1) * 0.125;
                double d6 = (sc->buffer[((j1 + 0) * 5 + k1 + 1) * 17 + l1 + 1] - d2) * 0.125;
                double d7 = (sc->buffer[((j1 + 1) * 5 + k1 + 0) * 17 + l1 + 1] - d3) * 0.125;
                double d8 = (sc->buffer[((j1 + 1) * 5 + k1 + 1) * 17 + l1 + 1] - d4) * 0.125;

                for (int i2 = 0; i2 < 8; ++i2) {
                    double d10 = d1;
                    double d11 = d2;
                    double d12 = (d3 - d1) * 0.25;
                    double d13 = (d4 - d2) * 0.25;
                    for (int j2 = 0; j2 < 4; ++j2) {
                        double d16 = (d11 - d10) * 0.25;
                        double lvt = d10 - d16;
                        for (int k2 = 0; k2 < 4; ++k2) {
                            int block = CPN_AIR;
                            if (l1 * 8 + i2 < j) block = CPN_LAVA;
                            if ((lvt += d16) > 0.0) block = CPN_NETHERRACK;
                            cpn_set(primer, j2 + j1 * 4, i2 + l1 * 8, k2 + k1 * 4, block);
                        }
                        d10 += d12;
                        d11 += d13;
                    }
                    d1 += d5; d2 += d6; d3 += d7; d4 += d8;
                }
            }
        }
    }
}

/* ChunkProviderHell.buildSurfaces (Forge onReplaceBiomeBlocks skipped). */
MC_HD MC_NOINLINE static void cpn_build_surfaces(CpnHellScratch *sc, const CpnHellNoise *n,
        JavaRandom *rand, int chunkX, int chunkZ, CpnPrimer *primer) {
    int i = CPN_SEA_LEVEL + 1;
    mc_oct_generate(&n->slowsandGravel, sc->slowsandNoise, chunkX * 16, chunkZ * 16, 0,
                    16, 16, 1, 0.03125, 0.03125, 1.0);
    mc_oct_generate(&n->slowsandGravel, sc->gravelNoise, chunkX * 16, 109, chunkZ * 16,
                    16, 1, 16, 0.03125, 1.0, 0.03125);
    mc_oct_generate(&n->netherrackExcl, sc->depthBuffer, chunkX * 16, chunkZ * 16, 0,
                    16, 16, 1, 0.0625, 0.0625, 0.0625);

    for (int j = 0; j < 16; ++j) {
        for (int k = 0; k < 16; ++k) {
            int flag = sc->slowsandNoise[j + k * 16] + jrand_double(rand) * 0.2 > 0.0;
            int flag1 = sc->gravelNoise[j + k * 16] + jrand_double(rand) * 0.2 > 0.0;
            int l = (int)(sc->depthBuffer[j + k * 16] / 3.0 + 3.0 + jrand_double(rand) * 0.25);
            int i1 = -1;
            int iblockstate = CPN_NETHERRACK;
            int iblockstate1 = CPN_NETHERRACK;

            for (int j1 = 127; j1 >= 0; --j1) {
                if (j1 < 127 - jrand_int_bound(rand, 5) && j1 > jrand_int_bound(rand, 5)) {
                    int s2 = cpn_get(primer, k, j1, j);
                    if (s2 != CPN_AIR) {
                        if (s2 == CPN_NETHERRACK) {
                            if (i1 == -1) {
                                if (l <= 0) {
                                    iblockstate = CPN_AIR;
                                    iblockstate1 = CPN_NETHERRACK;
                                } else if (j1 >= i - 4 && j1 <= i + 1) {
                                    iblockstate = CPN_NETHERRACK;
                                    iblockstate1 = CPN_NETHERRACK;
                                    if (flag1) {
                                        iblockstate = CPN_GRAVEL;
                                        iblockstate1 = CPN_NETHERRACK;
                                    }
                                    if (flag) {
                                        iblockstate = CPN_SOUL_SAND;
                                        iblockstate1 = CPN_SOUL_SAND;
                                    }
                                }
                                if (j1 < i && iblockstate == CPN_AIR)
                                    iblockstate = CPN_LAVA;
                                i1 = l;
                                if (j1 >= i - 1)
                                    cpn_set(primer, k, j1, j, iblockstate);
                                else
                                    cpn_set(primer, k, j1, j, iblockstate1);
                            } else if (i1 > 0) {
                                --i1;
                                cpn_set(primer, k, j1, j, iblockstate1);
                            }
                        }
                    } else {
                        i1 = -1;
                    }
                } else {
                    cpn_set(primer, k, j1, j, CPN_BEDROCK);
                }
            }
        }
    }
}

/* ===== MapGenCavesHell (verbatim tunnel/room carve into netherrack) ===== */
typedef struct { CpnPrimer *primer; const McSinTable *st; } CpnHellCaveCtx;

MC_HD MC_NOINLINE static void cpn_hell_add_tunnel(CpnHellCaveCtx *c, i64 p1, int p3, int p4,
        double p6, double p8, double p10, float p12, float p13, float p14,
        int p15, int p16, double p17) {
    double d0 = (double)(p3 * 16 + 8);
    double d1 = (double)(p4 * 16 + 8);
    float f = 0.0f, f1 = 0.0f;
    JavaRandom random; jrand_set(&random, p1);
    if (p16 <= 0) {
        int i = 8 * 16 - 16;
        p16 = i - jrand_int_bound(&random, i / 4);
    }
    int flag1 = 0;
    if (p15 == -1) { p15 = p16 / 2; flag1 = 1; }
    int jj = jrand_int_bound(&random, p16 / 2) + p16 / 4;
    int flag = (jrand_int_bound(&random, 6) == 0);
    for (; p15 < p16; ++p15) {
        double d2 = 1.5 + (double)(mc_sin(c->st, (float)p15 * (float)MC_PI / (float)p16) * p12);
        double d3 = d2 * p17;
        float f2 = mc_cos(c->st, p14);
        float f3 = mc_sin(c->st, p14);
        p6 += (double)(mc_cos(c->st, p13) * f2);
        p8 += (double)f3;
        p10 += (double)(mc_sin(c->st, p13) * f2);
        if (flag) p14 = p14 * 0.92f; else p14 = p14 * 0.7f;
        p14 = p14 + f1 * 0.1f;
        p13 += f * 0.1f;
        f1 = f1 * 0.9f;
        f = f * 0.75f;
        { float a = jrand_float(&random); float b = jrand_float(&random); float cc = jrand_float(&random);
          f1 = f1 + (a - b) * cc * 2.0f; }
        { float a = jrand_float(&random); float b = jrand_float(&random); float cc = jrand_float(&random);
          f = f + (a - b) * cc * 4.0f; }
        if (!flag1 && p15 == jj && p12 > 1.0f) {
            i64 s1 = jrand_long(&random); float z1 = jrand_float(&random) * 0.5f + 0.5f;
            cpn_hell_add_tunnel(c, s1, p3, p4, p6, p8, p10, z1, p13 - ((float)MC_PI / 2.0f), p14 / 3.0f, p15, p16, 1.0);
            i64 s2 = jrand_long(&random); float z2 = jrand_float(&random) * 0.5f + 0.5f;
            cpn_hell_add_tunnel(c, s2, p3, p4, p6, p8, p10, z2, p13 + ((float)MC_PI / 2.0f), p14 / 3.0f, p15, p16, 1.0);
            return;
        }
        if (flag1 || jrand_int_bound(&random, 4) != 0) {
            double d4 = p6 - d0;
            double d5 = p10 - d1;
            double d6 = (double)(p16 - p15);
            double d7 = (double)(p12 + 2.0f + 16.0f);
            if (d4 * d4 + d5 * d5 - d6 * d6 > d7 * d7) return;
            if (p6 >= d0 - 16.0 - d2 * 2.0 && p10 >= d1 - 16.0 - d2 * 2.0 &&
                p6 <= d0 + 16.0 + d2 * 2.0 && p10 <= d1 + 16.0 + d2 * 2.0) {
                int k2 = mc_floor(p6 - d2) - p3 * 16 - 1;
                int k = mc_floor(p6 + d2) - p3 * 16 + 1;
                int l2 = mc_floor(p8 - d3) - 1;
                int l = mc_floor(p8 + d3) + 1;
                int i3 = mc_floor(p10 - d2) - p4 * 16 - 1;
                int i1 = mc_floor(p10 + d2) - p4 * 16 + 1;
                if (k2 < 0) k2 = 0;
                if (k > 16) k = 16;
                if (l2 < 1) l2 = 1;
                if (l > 120) l = 120;
                if (i3 < 0) i3 = 0;
                if (i1 > 16) i1 = 16;
                int flag2 = 0;
                for (int j1 = k2; !flag2 && j1 < k; ++j1)
                    for (int k1 = i3; !flag2 && k1 < i1; ++k1)
                        for (int l1 = l + 1; !flag2 && l1 >= l2 - 1; --l1)
                            if (l1 >= 0 && l1 < 128) {
                                int b = cpn_get(c->primer, j1, l1, k1);
                                if (b == CPN_FLOWING_LAVA || b == CPN_LAVA) flag2 = 1;
                                if (l1 != l2 - 1 && j1 != k2 && j1 != k - 1 && k1 != i3 && k1 != i1 - 1)
                                    l1 = l2;
                            }
                if (!flag2) {
                    for (int i3a = k2; i3a < k; ++i3a) {
                        double d10 = ((double)(i3a + p3 * 16) + 0.5 - p6) / d2;
                        for (int j3 = i3; j3 < i1; ++j3) {
                            double d8 = ((double)(j3 + p4 * 16) + 0.5 - p10) / d2;
                            for (int i2 = l; i2 > l2; --i2) {
                                double d9 = ((double)(i2 - 1) + 0.5 - p8) / d3;
                                if (d9 > -0.7 && d10 * d10 + d9 * d9 + d8 * d8 < 1.0) {
                                    int s = cpn_get(c->primer, i3a, i2, j3);
                                    if (s == CPN_NETHERRACK || s == CPN_DIRT || s == CPN_GRASS)
                                        cpn_set(c->primer, i3a, i2, j3, CPN_AIR);
                                }
                            }
                        }
                    }
                    if (flag1) break;
                }
            }
        }
    }
}

MC_HD MC_NOINLINE static void cpn_hell_add_room(CpnHellCaveCtx *c, JavaRandom *rand, i64 seed,
        int p3, int p4, double x, double y, double z) {
    float sz = 1.0f + jrand_float(rand) * 6.0f;
    cpn_hell_add_tunnel(c, seed, p3, p4, x, y, z, sz, 0.0f, 0.0f, -1, -1, 0.5);
}

MC_HD MC_NOINLINE static void cpn_hell_recursive(CpnHellCaveCtx *c, JavaRandom *rand,
        int chunkX, int chunkZ, int p4, int p5) {
    int i = jrand_int_bound(rand, jrand_int_bound(rand, jrand_int_bound(rand, 10) + 1) + 1);
    if (jrand_int_bound(rand, 5) != 0) i = 0;
    for (int j = 0; j < i; ++j) {
        double d0 = (double)(chunkX * 16 + jrand_int_bound(rand, 16));
        double d1 = (double)jrand_int_bound(rand, 128);
        double d2 = (double)(chunkZ * 16 + jrand_int_bound(rand, 16));
        int k = 1;
        if (jrand_int_bound(rand, 4) == 0) {
            cpn_hell_add_room(c, rand, jrand_long(rand), p4, p5, d0, d1, d2);
            k += jrand_int_bound(rand, 4);
        }
        for (int l = 0; l < k; ++l) {
            float ff = jrand_float(rand) * ((float)MC_PI * 2.0f);
            float f1 = (jrand_float(rand) - 0.5f) * 2.0f / 8.0f;
            float fa = jrand_float(rand);
            float fb = jrand_float(rand);
            float f2 = fa * 2.0f + fb;
            cpn_hell_add_tunnel(c, jrand_long(rand), p4, p5, d0, d1, d2, f2 * 2.0f, ff, f1, 0, 0, 0.5);
        }
    }
}

MC_HD MC_NOINLINE static void cpn_hell_cave_generate(CpnHellCaveCtx *c, i64 worldSeed, int x, int z) {
    int range = 8;
    JavaRandom rand; jrand_set(&rand, worldSeed);
    i64 j = jrand_long(&rand);
    i64 k = jrand_long(&rand);
    for (int l = x - range; l <= x + range; ++l) {
        for (int i1 = z - range; i1 <= z + range; ++i1) {
            i64 j1 = (i64)l * j;
            i64 k1 = (i64)i1 * k;
            jrand_set(&rand, j1 ^ k1 ^ worldSeed);
            cpn_hell_recursive(c, &rand, l, i1, x, z);
        }
    }
}

/* apply fortress structures via verified map_gen_fortress (ChunkPrimer layout identical). */
MC_HD MC_NOINLINE static void cpn_fortress_generate(CpnPrimer *primer, i64 seed, int chunkX, int chunkZ) {
    ChunkPrimer *fp = (ChunkPrimer *)primer;
    FtWorld w; w.primer = fp; w.chunkX = chunkX; w.chunkZ = chunkZ; w.worldSeed = seed;
    FtGen g;
    memset(&g, 0, sizeof(g));
    ft_generate_map(&g, seed, chunkX, chunkZ);
    ft_generate_structure(&w, &g, chunkX, chunkZ);
}

/* ChunkProviderHell.provideChunk minus Chunk/biomes/populate; generateStructures=true. */
MC_HD MC_NOINLINE static void cpn_provide_chunk(CpnPrimer *primer, CpnHellScratch *sc,
        const McSinTable *st, CpnHellNoise *noise, i64 seed, int chunkX, int chunkZ) {
    for (int i = 0; i < 65536; ++i) primer->data[i] = (u16)CPN_AIR;

    JavaRandom rand;
    jrand_set(&rand, (i64)chunkX * 341873128712LL + (i64)chunkZ * 132897987541LL);

    cpn_prepare_heights(sc, noise, chunkX, chunkZ, primer);
    cpn_build_surfaces(sc, noise, &rand, chunkX, chunkZ, primer);

    CpnHellCaveCtx cctx; cctx.primer = primer; cctx.st = st;
    cpn_hell_cave_generate(&cctx, seed, chunkX, chunkZ);

    cpn_fortress_generate(primer, seed, chunkX, chunkZ);
}

#endif /* MC_CHUNK_PROVIDER_NETHER_H */
