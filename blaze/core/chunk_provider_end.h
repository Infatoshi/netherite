/* chunk_provider_end: REAL-CHUNK End pipeline = MC 1.11.2 ChunkProviderEnd.provideChunk
 * (net/minecraft/world/gen/ChunkProviderEnd.java), MINUS structures (MapGenEndCity),
 * MINUS new Chunk / biome array / skylight / populate. Composes setBlocksInChunk +
 * buildSurfaces for chunk (chunkX,chunkZ), bitwise-matching verbatim Java.
 *
 * End terrain: END_STONE where density > 0, void AIR elsewhere (no water/ocean).
 * Island shaping via getIslandHeightValue + NoiseGeneratorSimplex islandNoise.
 *
 * Block-state ids (sanctioned small-int substitution, identical in golden + candidate):
 *   CE_AIR=0, CE_END_STONE=1, CE_STONE=2 (Blocks.STONE predicate in buildSurfaces only;
 *   never emitted by setBlocksInChunk, so buildSurfaces is a no-op as in vanilla).
 *
 * Noise ctor draw order (new Random(worldSeed)): lperlin1(16), lperlin2(16), perlin1(8),
 * noiseGen5(10), noiseGen6(16), islandNoise Simplex. noiseGen5/6 are constructed but unused
 * in getHeights (same as vanilla).
 *
 * Large noise structs (~165KB EndNoise) are heap-allocated inside cpe_provide_chunk so the
 * CUDA device thread stack is not blown (same pattern as chunk_provider.cu).
 *
 * Build -ffp-contract=off / --fmad=false. */
#ifndef MC_CHUNK_PROVIDER_END_H
#define MC_CHUNK_PROVIDER_END_H

#include <math.h>
#include <stdlib.h>
#include "mc.h"
#include "mc_rng.h"
#include "mc_noise.h"

enum { CE_AIR = 0, CE_END_STONE = 1, CE_STONE = 2 };

#define CPE_BUF_SIZE (3 * 33 * 3)

typedef struct { u16 data[65536]; } CpePrimer;
MC_HD static inline int  cpe_index(int x, int y, int z) { return x << 12 | z << 8 | y; }
MC_HD static inline int  cpe_get(const CpePrimer *p, int x, int y, int z) { return (int)p->data[cpe_index(x, y, z)]; }
MC_HD static inline void cpe_set(CpePrimer *p, int x, int y, int z, int v) { p->data[cpe_index(x, y, z)] = (u16)v; }

typedef struct { int p[512]; double xo, yo, zo; } CpeSimplex;

typedef struct {
    NoiseOctaves lperlin1;
    NoiseOctaves lperlin2;
    NoiseOctaves perlin1;
    NoiseOctaves noiseGen5;
    NoiseOctaves noiseGen6;
    CpeSimplex islandNoise;
} EndNoise;

typedef struct {
    double buffer[CPE_BUF_SIZE];
    double pnr[CPE_BUF_SIZE];
    double ar[CPE_BUF_SIZE];
    double br[CPE_BUF_SIZE];
    EndNoise noise;   /* was malloc(sizeof(EndNoise)) in cpe_provide_chunk (no in-kernel malloc) */
} CpeScratch;

MC_HD static inline float cpe_mh_sqrt(float value) { return (float)sqrt((double)value); }
MC_HD static inline float cpe_mh_abs(float value) { return value >= 0.0f ? value : -value; }
MC_HD MC_NOINLINE static double cpe_mh_clamp(double num, double min, double max) {
    return num < min ? min : (num > max ? max : num);
}

MC_HD MC_NOINLINE static void cpe_simplex_init(CpeSimplex *s, JavaRandom *r) {
    s->xo = jrand_double(r) * 256.0;
    s->yo = jrand_double(r) * 256.0;
    s->zo = jrand_double(r) * 256.0;
    for (int i = 0; i < 256; ++i) s->p[i] = i;
    for (int l = 0; l < 256; ++l) {
        int j = jrand_int_bound(r, 256 - l) + l;
        int k = s->p[l];
        s->p[l] = s->p[j];
        s->p[j] = k;
        s->p[l + 256] = s->p[l];
    }
}

#define CPE_SQRT3      1.7320508075688772
#define CPE_F2         (0.5 * (CPE_SQRT3 - 1.0))
#define CPE_G2         ((3.0 - CPE_SQRT3) / 6.0)

MC_HD static inline int cpe_fastfloor(double v) { return v > 0.0 ? mc_d2i(v) : mc_d2i(v) - 1; }

MC_HD MC_NOINLINE static double cpe_simplex_getValue(const CpeSimplex *s, double x1, double x3) {
    const int CP_GRAD3[12][3] = {
        {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},
        {1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
    double d3 = 0.5 * (CPE_SQRT3 - 1.0);
    double d4 = (x1 + x3) * d3;
    int i = cpe_fastfloor(x1 + d4);
    int j = cpe_fastfloor(x3 + d4);
    double d5 = (3.0 - CPE_SQRT3) / 6.0;
    double d6 = (double)(i + j) * d5;
    double d7 = (double)i - d6;
    double d8 = (double)j - d6;
    double d9 = x1 - d7;
    double d10 = x3 - d8;
    int k, l;
    if (d9 > d10) { k = 1; l = 0; } else { k = 0; l = 1; }
    double d11 = d9 - (double)k + d5;
    double d12 = d10 - (double)l + d5;
    double d13 = d9 - 1.0 + 2.0 * d5;
    double d14 = d10 - 1.0 + 2.0 * d5;
    int i1 = i & 255;
    int j1 = j & 255;
    int k1 = s->p[i1 + s->p[j1]] % 12;
    int l1 = s->p[i1 + k + s->p[j1 + l]] % 12;
    int i2 = s->p[i1 + 1 + s->p[j1 + 1]] % 12;
    double d15 = 0.5 - d9 * d9 - d10 * d10;
    double d0;
    if (d15 < 0.0) d0 = 0.0;
    else { d15 = d15 * d15; d0 = d15 * d15 * ((double)CP_GRAD3[k1][0] * d9 + (double)CP_GRAD3[k1][1] * d10); }
    double d16 = 0.5 - d11 * d11 - d12 * d12;
    double d1;
    if (d16 < 0.0) d1 = 0.0;
    else { d16 = d16 * d16; d1 = d16 * d16 * ((double)CP_GRAD3[l1][0] * d11 + (double)CP_GRAD3[l1][1] * d12); }
    double d17 = 0.5 - d13 * d13 - d14 * d14;
    double d2;
    if (d17 < 0.0) d2 = 0.0;
    else { d17 = d17 * d17; d2 = d17 * d17 * ((double)CP_GRAD3[i2][0] * d13 + (double)CP_GRAD3[i2][1] * d14); }
    return 70.0 * (d0 + d1 + d2);
}

MC_HD MC_NOINLINE static void cpe_noise_init(EndNoise *n, i64 seed) {
    JavaRandom r; jrand_set(&r, seed);
    mc_oct_init(&n->lperlin1, &r, 16);
    mc_oct_init(&n->lperlin2, &r, 16);
    mc_oct_init(&n->perlin1, &r, 8);
    mc_oct_init(&n->noiseGen5, &r, 10);
    mc_oct_init(&n->noiseGen6, &r, 16);
    cpe_simplex_init(&n->islandNoise, &r);
}

MC_HD MC_NOINLINE static float cpe_getIslandHeightValue(const EndNoise *n,
        int p_185960_1_, int p_185960_2_, int p_185960_3_, int p_185960_4_) {
    float f = (float)(p_185960_1_ * 2 + p_185960_3_);
    float f1 = (float)(p_185960_2_ * 2 + p_185960_4_);
    float f2 = 100.0f - cpe_mh_sqrt(f * f + f1 * f1) * 8.0f;
    if (f2 > 80.0f) f2 = 80.0f;
    if (f2 < -100.0f) f2 = -100.0f;
    for (int i = -12; i <= 12; ++i) {
        for (int j = -12; j <= 12; ++j) {
            i64 k = (i64)(p_185960_1_ + i);
            i64 l = (i64)(p_185960_2_ + j);
            if (k * k + l * l > 4096LL &&
                    cpe_simplex_getValue(&n->islandNoise, (double)k, (double)l) < -0.8999999761581421) {
                float f3 = (cpe_mh_abs((float)k) * 3439.0f + cpe_mh_abs((float)l) * 147.0f);
                f3 = f3 - (float)((int)(f3 / 13.0f)) * 13.0f + 9.0f;  /* % 13.0F + 9.0F, float math */
                f = (float)(p_185960_3_ - i * 2);
                f1 = (float)(p_185960_4_ - j * 2);
                float f4 = 100.0f - cpe_mh_sqrt(f * f + f1 * f1) * f3;
                if (f4 > 80.0f) f4 = 80.0f;
                if (f4 < -100.0f) f4 = -100.0f;
                if (f4 > f2) f2 = f4;
            }
        }
    }
    return f2;
}

MC_HD MC_NOINLINE static void cpe_getHeights(const EndNoise *n, CpeScratch *sc,
        int p_185963_2_, int p_185963_3_, int p_185963_4_,
        int p_185963_5_, int p_185963_6_, int p_185963_7_) {
    double d0 = 684.412;
    d0 = d0 * 2.0;
    mc_oct_generate(&n->perlin1, sc->pnr, p_185963_2_, p_185963_3_, p_185963_4_,
                    p_185963_5_, p_185963_6_, p_185963_7_, d0 / 80.0, 4.277575000000001, d0 / 80.0);
    mc_oct_generate(&n->lperlin1, sc->ar, p_185963_2_, p_185963_3_, p_185963_4_,
                    p_185963_5_, p_185963_6_, p_185963_7_, d0, 684.412, d0);
    mc_oct_generate(&n->lperlin2, sc->br, p_185963_2_, p_185963_3_, p_185963_4_,
                    p_185963_5_, p_185963_6_, p_185963_7_, d0, 684.412, d0);
    int i = p_185963_2_ / 2;
    int j = p_185963_4_ / 2;
    int k = 0;
    for (int l = 0; l < p_185963_5_; ++l) {
        for (int i1 = 0; i1 < p_185963_7_; ++i1) {
            float f = cpe_getIslandHeightValue(n, i, j, l, i1);
            for (int j1 = 0; j1 < p_185963_6_; ++j1) {
                double d2 = sc->ar[k] / 512.0;
                double d3 = sc->br[k] / 512.0;
                double d5 = (sc->pnr[k] / 10.0 + 1.0) / 2.0;
                double d4;
                if (d5 < 0.0) d4 = d2;
                else if (d5 > 1.0) d4 = d3;
                else d4 = d2 + (d3 - d2) * d5;
                d4 = d4 - 8.0;
                d4 = d4 + (double)f;
                int k1 = 2;
                if (j1 > p_185963_6_ / 2 - k1) {
                    double d6 = (double)((float)(j1 - (p_185963_6_ / 2 - k1)) / 64.0f);
                    d6 = cpe_mh_clamp(d6, 0.0, 1.0);
                    d4 = d4 * (1.0 - d6) + -3000.0 * d6;
                }
                k1 = 8;
                if (j1 < k1) {
                    double d7 = (double)((float)(k1 - j1) / ((float)k1 - 1.0f));
                    d4 = d4 * (1.0 - d7) + -30.0 * d7;
                }
                sc->buffer[k] = d4;
                ++k;
            }
        }
    }
}

MC_HD MC_NOINLINE static void cpe_setBlocksInChunk(CpePrimer *primer, const double *buffer, int x, int z) {
    (void)x; (void)z;
    for (int i1 = 0; i1 < 2; ++i1) {
        for (int j1 = 0; j1 < 2; ++j1) {
            for (int k1 = 0; k1 < 32; ++k1) {
                double d1 = buffer[((i1 + 0) * 3 + j1 + 0) * 33 + k1 + 0];
                double d2 = buffer[((i1 + 0) * 3 + j1 + 1) * 33 + k1 + 0];
                double d3 = buffer[((i1 + 1) * 3 + j1 + 0) * 33 + k1 + 0];
                double d4 = buffer[((i1 + 1) * 3 + j1 + 1) * 33 + k1 + 0];
                double d5 = (buffer[((i1 + 0) * 3 + j1 + 0) * 33 + k1 + 1] - d1) * 0.25;
                double d6 = (buffer[((i1 + 0) * 3 + j1 + 1) * 33 + k1 + 1] - d2) * 0.25;
                double d7 = (buffer[((i1 + 1) * 3 + j1 + 0) * 33 + k1 + 1] - d3) * 0.25;
                double d8 = (buffer[((i1 + 1) * 3 + j1 + 1) * 33 + k1 + 1] - d4) * 0.25;
                for (int l1 = 0; l1 < 4; ++l1) {
                    double d10 = d1;
                    double d11 = d2;
                    double d12 = (d3 - d1) * 0.125;
                    double d13 = (d4 - d2) * 0.125;
                    for (int i2 = 0; i2 < 8; ++i2) {
                        double d15 = d10;
                        double d16 = (d11 - d10) * 0.125;
                        for (int j2 = 0; j2 < 8; ++j2) {
                            int block = CE_AIR;
                            if (d15 > 0.0) block = CE_END_STONE;
                            int k2 = i2 + i1 * 8;
                            int l2 = l1 + k1 * 4;
                            int i3 = j2 + j1 * 8;
                            cpe_set(primer, k2, l2, i3, block);
                            d15 += d16;
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

MC_HD MC_NOINLINE static void cpe_buildSurfaces(CpePrimer *primer) {
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            int l = -1;
            int iblockstate = CE_END_STONE;
            int iblockstate1 = CE_END_STONE;
            for (int i1 = 127; i1 >= 0; --i1) {
                int iblockstate2 = cpe_get(primer, i, i1, j);
                if (iblockstate2 == CE_AIR) {
                    l = -1;
                } else if (iblockstate2 == CE_STONE) {
                    if (l == -1) {
                        l = 1;
                        if (i1 >= 0) cpe_set(primer, i, i1, j, iblockstate);
                        else cpe_set(primer, i, i1, j, iblockstate1);
                    } else if (l > 0) {
                        --l;
                        cpe_set(primer, i, i1, j, iblockstate1);
                    }
                }
            }
        }
    }
}

MC_HD MC_NOINLINE static void cpe_provide_chunk(CpePrimer *primer, CpeScratch *sc, i64 seed, int chunkX, int chunkZ) {
    for (int i = 0; i < 65536; ++i) primer->data[i] = (u16)CE_AIR;

    EndNoise *noise = &sc->noise;   /* preallocated (no in-kernel malloc) */
    cpe_noise_init(noise, seed);

    cpe_getHeights(noise, sc, chunkX * 2, 0, chunkZ * 2, 3, 33, 3);
    cpe_setBlocksInChunk(primer, sc->buffer, chunkX, chunkZ);
    cpe_buildSurfaces(primer);
}

#endif /* MC_CHUNK_PROVIDER_END_H */
