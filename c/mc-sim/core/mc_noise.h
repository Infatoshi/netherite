/* mc_noise.h - exact C port of MC 1.11.2 NoiseGeneratorImproved + NoiseGeneratorOctaves
 * (net/minecraft/world/gen/). Perlin "improved" noise; the basis of all terrain shaping.
 * Verbatim numerical port: same operator order, same (int) truncation, same java.util.Random.
 * Worldgen is the one vanilla-bit-exact subsystem (SPEC rule 2), so this is checked against a
 * verbatim-Java golden AND across CPU==CUDA. Build with -ffp-contract=off / --fmad=false. */
#ifndef MC_NOISE_H
#define MC_NOISE_H

#include "mc_rng.h"

#define MC_NOISE_MAX_OCTAVES 16

/* MathHelper.lfloor */
MC_HD static inline i64 mc_lfloor(double v) {
    i64 i = (i64)v;
    return v < (double)i ? i - 1 : i;
}

typedef struct {
    int permutations[512];
    double xCoord, yCoord, zCoord;
} NoiseImproved;

typedef struct {
    NoiseImproved gen[MC_NOISE_MAX_OCTAVES];
    int octaves;
} NoiseOctaves;

MC_HD static inline double mc_ni_lerp(double t, double a, double b) { return a + t * (b - a); }
MC_HD static inline double mc_ni_grad2(int hash, double x, double z) {
    const double GX[16] = {1,-1,1,-1,1,-1,1,-1,0,0,0,0,1,0,-1,0};
    const double GZ[16] = {0,0,0,0,1,1,-1,-1,1,1,-1,-1,0,1,0,-1};
    int i = hash & 15; return GX[i] * x + GZ[i] * z;
}
MC_HD static inline double mc_ni_grad(int hash, double x, double y, double z) {
    const double GX[16] = {1,-1,1,-1,1,-1,1,-1,0,0,0,0,1,0,-1,0};
    const double GY[16] = {1,1,-1,-1,0,0,0,0,1,-1,1,-1,1,-1,1,-1};
    const double GZ[16] = {0,0,0,0,1,1,-1,-1,1,1,-1,-1,0,1,0,-1};
    int i = hash & 15; return GX[i] * x + GY[i] * y + GZ[i] * z;
}

/* NoiseGeneratorImproved(Random) */
MC_HD static inline void mc_ni_init(NoiseImproved *n, JavaRandom *r) {
    n->xCoord = jrand_double(r) * 256.0;
    n->yCoord = jrand_double(r) * 256.0;
    n->zCoord = jrand_double(r) * 256.0;
    for (int i = 0; i < 256; ++i) n->permutations[i] = i;   /* Java's perm[i]=i++ is identity; a[i]=i++ is UB in C */
    for (int l = 0; l < 256; ++l) {
        int j = jrand_int_bound(r, 256 - l) + l;
        int k = n->permutations[l];
        n->permutations[l] = n->permutations[j];
        n->permutations[j] = k;
        n->permutations[l + 256] = n->permutations[l];
    }
}

/* NoiseGeneratorImproved.populateNoiseArray (the 3D ySize!=1 path and the 2D ySize==1 path) */
MC_HD static inline void mc_ni_populate(const NoiseImproved *n, double *noiseArray,
        double xOffset, double yOffset, double zOffset,
        int xSize, int ySize, int zSize,
        double xScale, double yScale, double zScale, double noiseScale) {
    const int *P = n->permutations;
    if (ySize == 1) {
        int l5 = 0;
        double d16 = 1.0 / noiseScale;
        for (int j2 = 0; j2 < xSize; ++j2) {
            double d17 = xOffset + (double)j2 * xScale + n->xCoord;
            int i6 = (int)d17; if (d17 < (double)i6) --i6;
            int k2 = i6 & 255; d17 = d17 - (double)i6;
            double d18 = d17 * d17 * d17 * (d17 * (d17 * 6.0 - 15.0) + 10.0);
            for (int j6 = 0; j6 < zSize; ++j6) {
                double d19 = zOffset + (double)j6 * zScale + n->zCoord;
                int k6 = (int)d19; if (d19 < (double)k6) --k6;
                int l6 = k6 & 255; d19 = d19 - (double)k6;
                double d20 = d19 * d19 * d19 * (d19 * (d19 * 6.0 - 15.0) + 10.0);
                int i5 = P[k2] + 0;
                int j5 = P[i5] + l6;
                int j  = P[k2 + 1] + 0;
                int k5 = P[j] + l6;
                double d14 = mc_ni_lerp(d18, mc_ni_grad2(P[j5], d17, d19), mc_ni_grad(P[k5], d17 - 1.0, 0.0, d19));
                double d15 = mc_ni_lerp(d18, mc_ni_grad(P[j5 + 1], d17, 0.0, d19 - 1.0), mc_ni_grad(P[k5 + 1], d17 - 1.0, 0.0, d19 - 1.0));
                double d21 = mc_ni_lerp(d20, d14, d15);
                noiseArray[l5++] += d21 * d16;
            }
        }
    } else {
        int i = 0;
        double d0 = 1.0 / noiseScale;
        int k = -1;
        int l = 0, i1 = 0, j1 = 0, k1 = 0, l1 = 0, i2 = 0;
        double d1 = 0, d2 = 0, d3 = 0, d4 = 0;
        for (int l2 = 0; l2 < xSize; ++l2) {
            double d5 = xOffset + (double)l2 * xScale + n->xCoord;
            int i3 = (int)d5; if (d5 < (double)i3) --i3;
            int j3 = i3 & 255; d5 = d5 - (double)i3;
            double d6 = d5 * d5 * d5 * (d5 * (d5 * 6.0 - 15.0) + 10.0);
            for (int k3 = 0; k3 < zSize; ++k3) {
                double d7 = zOffset + (double)k3 * zScale + n->zCoord;
                int l3 = (int)d7; if (d7 < (double)l3) --l3;
                int i4 = l3 & 255; d7 = d7 - (double)l3;
                double d8 = d7 * d7 * d7 * (d7 * (d7 * 6.0 - 15.0) + 10.0);
                for (int j4 = 0; j4 < ySize; ++j4) {
                    double d9 = yOffset + (double)j4 * yScale + n->yCoord;
                    int k4 = (int)d9; if (d9 < (double)k4) --k4;
                    int l4 = k4 & 255; d9 = d9 - (double)k4;
                    double d10 = d9 * d9 * d9 * (d9 * (d9 * 6.0 - 15.0) + 10.0);
                    if (j4 == 0 || l4 != k) {
                        k = l4;
                        l  = P[j3] + l4;
                        i1 = P[l] + i4;
                        j1 = P[l + 1] + i4;
                        k1 = P[j3 + 1] + l4;
                        l1 = P[k1] + i4;
                        i2 = P[k1 + 1] + i4;
                        d1 = mc_ni_lerp(d6, mc_ni_grad(P[i1], d5, d9, d7), mc_ni_grad(P[l1], d5 - 1.0, d9, d7));
                        d2 = mc_ni_lerp(d6, mc_ni_grad(P[j1], d5, d9 - 1.0, d7), mc_ni_grad(P[i2], d5 - 1.0, d9 - 1.0, d7));
                        d3 = mc_ni_lerp(d6, mc_ni_grad(P[i1 + 1], d5, d9, d7 - 1.0), mc_ni_grad(P[l1 + 1], d5 - 1.0, d9, d7 - 1.0));
                        d4 = mc_ni_lerp(d6, mc_ni_grad(P[j1 + 1], d5, d9 - 1.0, d7 - 1.0), mc_ni_grad(P[i2 + 1], d5 - 1.0, d9 - 1.0, d7 - 1.0));
                    }
                    double d11 = mc_ni_lerp(d10, d1, d2);
                    double d12 = mc_ni_lerp(d10, d3, d4);
                    double d13 = mc_ni_lerp(d8, d11, d12);
                    noiseArray[i++] += d13 * d0;
                }
            }
        }
    }
}

/* NoiseGeneratorOctaves(Random, octaves) */
MC_HD static inline void mc_oct_init(NoiseOctaves *o, JavaRandom *r, int octaves) {
    o->octaves = octaves;
    for (int i = 0; i < octaves; ++i) mc_ni_init(&o->gen[i], r);
}

/* NoiseGeneratorOctaves.generateNoiseOctaves (full 3D form). Caller zeroes noiseArray. */
MC_HD static inline void mc_oct_generate(const NoiseOctaves *o, double *noiseArray,
        int xOffset, int yOffset, int zOffset, int xSize, int ySize, int zSize,
        double xScale, double yScale, double zScale) {
    int total = xSize * ySize * zSize;
    for (int i = 0; i < total; ++i) noiseArray[i] = 0.0;
    double d3 = 1.0;
    for (int j = 0; j < o->octaves; ++j) {
        double d0 = (double)xOffset * d3 * xScale;
        double d1 = (double)yOffset * d3 * yScale;
        double d2 = (double)zOffset * d3 * zScale;
        i64 k = mc_lfloor(d0);
        i64 l = mc_lfloor(d2);
        d0 = d0 - (double)k;
        d2 = d2 - (double)l;
        k = k % 16777216L;
        l = l % 16777216L;
        d0 = d0 + (double)k;
        d2 = d2 + (double)l;
        mc_ni_populate(&o->gen[j], noiseArray, d0, d1, d2, xSize, ySize, zSize,
                       xScale * d3, yScale * d3, zScale * d3, d3);
        d3 /= 2.0;
    }
}

#endif /* MC_NOISE_H */
