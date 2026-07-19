/* terrain_shape: exact C port of MC 1.11.2 ChunkProviderOverworld.generateHeightmap
 * (net/minecraft/world/gen/ChunkProviderOverworld.java). Produces the overworld terrain density
 * field heightMap (double[825] = 5x33x5) for a chunk from the octave noise generators
 * (minLimit/maxLimit/main Perlin + depth) blended with the per-column biome depth/scale.
 *
 * Worldgen is the one vanilla-bit-exact subsystem (SPEC rule 2): checked verbatim-Java == CPU ==
 * CUDA via the Java LCG. Build with -ffp-contract=off / --fmad=false.
 *
 * REUSE: the verified core/mc_noise.h NoiseGeneratorOctaves (mc_oct_init / mc_oct_generate).
 *
 * RNG construction order matters. ChunkProviderOverworld's constructor draws from ONE Random in
 * this order: minLimit(16), maxLimit(16), main(8), surfaceNoise=NoiseGeneratorPerlin(4),
 * scaleNoise(10), depthNoise(16), forestNoise(8). depthNoise is created 5th-after, so we MUST
 * advance the RNG past surfaceNoise and scaleNoise to get depthNoise's permutations right.
 * NoiseGeneratorSimplex (used by NoiseGeneratorPerlin) consumes RNG identically to
 * NoiseGeneratorImproved (3 nextDouble + 256 nextInt(256-l)), so we advance with the same pattern.
 *
 * HARNESS SUBSTITUTION (deterministic input, NOT a formula change): generateHeightmap reads each
 * column's biome base-height/height-variation from biomesForGeneration. To decouple from the
 * genlayer_biomes unit we bake every neighbor cell to vanilla 1.11.2 Plains:
 *   baseHeight = 0.125f, heightVariation = 0.05f  (Biome.java:544, BiomeProperties for "plains").
 * The same fixed values are baked identically into the golden. All noise math, the d0/d1/d2
 * min/max/main blend, the depthNoise d7 handling, and the heightMap assignment are verbatim vanilla.
 *
 * C-vs-Java float traps preserved: every (float)/(double) cast and left-to-right operator order is
 * replicated to the bit; settings constants are the ChunkProviderSettings.Factory defaults. */
#ifndef MC_TERRAIN_SHAPE_H
#define MC_TERRAIN_SHAPE_H

#include <math.h>
#include "mc.h"
#include "mc_rng.h"
#include "mc_noise.h"

/* MathHelper.clampedLerp */
MC_HD MC_NOINLINE static double mc_clamped_lerp(double lowerBnd, double upperBnd, double slide) {
    return slide < 0.0 ? lowerBnd : (slide > 1.0 ? upperBnd : lowerBnd + (upperBnd - lowerBnd) * slide);
}

/* The 4 octave generators generateHeightmap actually consumes. Built in vanilla constructor order. */
typedef struct {
    NoiseOctaves minLimit;   /* 16 */
    NoiseOctaves maxLimit;   /* 16 */
    NoiseOctaves mainP;      /* 8  */
    NoiseOctaves depth;      /* 16 */
} TerrainNoise;

/* Advance the Random exactly as a NoiseGeneratorImproved/Simplex constructor would, without
 * storing the result (used for surfaceNoise + scaleNoise which we never sample but which sit
 * between mainPerlinNoise and depthNoise in the constructor's draw order). */
MC_HD MC_NOINLINE static void mc_ts_advance_improved(JavaRandom *r) {
    jrand_double(r);
    jrand_double(r);
    jrand_double(r);
    for (int l = 0; l < 256; ++l) jrand_int_bound(r, 256 - l);
}
MC_HD MC_NOINLINE static void mc_ts_advance_octaves(JavaRandom *r, int octaves) {
    for (int i = 0; i < octaves; ++i) mc_ts_advance_improved(r);
}

/* ChunkProviderOverworld constructor: build the noise generators in exact draw order. */
MC_HD MC_NOINLINE static void terrain_noise_init(TerrainNoise *t, i64 seed) {
    JavaRandom r; jrand_set(&r, seed);
    mc_oct_init(&t->minLimit, &r, 16);
    mc_oct_init(&t->maxLimit, &r, 16);
    mc_oct_init(&t->mainP, &r, 8);
    mc_ts_advance_octaves(&r, 4);    /* surfaceNoise = NoiseGeneratorPerlin(rand, 4) -> 4 simplex */
    mc_ts_advance_octaves(&r, 10);   /* scaleNoise   = NoiseGeneratorOctaves(rand, 10) */
    mc_oct_init(&t->depth, &r, 16);
    /* forestNoise(8) is created after depthNoise; not needed here. */
}

/* ChunkProviderOverworld.generateHeightmap(p1=chunkX*4, p2=0, p3=chunkZ*4). Fills heightMap[825]. */
MC_HD MC_NOINLINE static void terrain_generate_heightmap(const TerrainNoise *t, double *heightMap,
        int p_185978_1_, int p_185978_2_, int p_185978_3_) {
    /* ChunkProviderSettings.Factory defaults (vanilla, no JSON override). */
    const float coordinateScale = 684.412f;
    const float heightScale = 684.412f;
    const float upperLimitScale = 512.0f;
    const float lowerLimitScale = 512.0f;
    const float depthNoiseScaleX = 200.0f;
    const float depthNoiseScaleZ = 200.0f;
    /* depthNoiseScaleExponent = 0.5f is passed but discarded by the 2D bouncer overload. */
    const float mainNoiseScaleX = 80.0f;
    const float mainNoiseScaleY = 160.0f;
    const float mainNoiseScaleZ = 80.0f;
    const float baseSize = 8.5f;
    const float stretchY = 12.0f;
    const float biomeDepthWeight = 1.0f;
    const float biomeDepthOffSet = 0.0f;
    const float biomeScaleWeight = 1.0f;
    const float biomeScaleOffset = 0.0f;
    /* fixed Plains biome (harness substitution; see header doc). */
    const float plainsBaseHeight = 0.125f;
    const float plainsHeightVariation = 0.05f;

    double depthRegion[25];
    double mainNoiseRegion[825];
    double minLimitRegion[825];
    double maxLimitRegion[825];

    /* biomeWeights[25] from the constructor: 10/sqrt(i^2+j^2+0.2). */
    float biomeWeights[25];
    for (int i = -2; i <= 2; ++i) {
        for (int j = -2; j <= 2; ++j) {
            float sq = (float)(i * i + j * j) + 0.2f;
            float f = 10.0f / (float)sqrt((double)sq);   /* MathHelper.sqrt(float) */
            biomeWeights[i + 2 + (j + 2) * 5] = f;
        }
    }

    /* depthNoise.generateNoiseOctaves(depthRegion, p1, p3, 5, 5, scaleX, scaleZ, exponent):
     * the 2D bouncer forwards as (..., p1, 10, p3, 5, 1, 5, scaleX, 1.0, scaleZ). */
    mc_oct_generate(&t->depth, depthRegion, p_185978_1_, 10, p_185978_3_, 5, 1, 5,
                    (double)depthNoiseScaleX, 1.0, (double)depthNoiseScaleZ);
    float f = coordinateScale;
    float f1 = heightScale;
    mc_oct_generate(&t->mainP, mainNoiseRegion, p_185978_1_, p_185978_2_, p_185978_3_, 5, 33, 5,
                    (double)(f / mainNoiseScaleX), (double)(f1 / mainNoiseScaleY), (double)(f / mainNoiseScaleZ));
    mc_oct_generate(&t->minLimit, minLimitRegion, p_185978_1_, p_185978_2_, p_185978_3_, 5, 33, 5,
                    (double)f, (double)f1, (double)f);
    mc_oct_generate(&t->maxLimit, maxLimitRegion, p_185978_1_, p_185978_2_, p_185978_3_, 5, 33, 5,
                    (double)f, (double)f1, (double)f);
    int i = 0;
    int j = 0;

    for (int k = 0; k < 5; ++k) {
        for (int l = 0; l < 5; ++l) {
            float f2 = 0.0f;
            float f3 = 0.0f;
            float f4 = 0.0f;
            /* biome (center) = Plains; getBaseHeight() = 0.125f */

            for (int j1 = -2; j1 <= 2; ++j1) {
                for (int k1 = -2; k1 <= 2; ++k1) {
                    /* biome1 = Plains for every neighbor cell */
                    float f5 = biomeDepthOffSet + plainsBaseHeight * biomeDepthWeight;
                    float f6 = biomeScaleOffset + plainsHeightVariation * biomeScaleWeight;
                    /* terrainType != AMPLIFIED -> no f5/f6 boost */
                    float f7 = biomeWeights[j1 + 2 + (k1 + 2) * 5] / (f5 + 2.0f);
                    /* biome1.getBaseHeight() > biome.getBaseHeight(): 0.125 > 0.125 is false */
                    f2 += f6 * f7;
                    f3 += f5 * f7;
                    f4 += f7;
                }
            }

            f2 = f2 / f4;
            f3 = f3 / f4;
            f2 = f2 * 0.9f + 0.1f;
            f3 = (f3 * 4.0f - 1.0f) / 8.0f;
            double d7 = depthRegion[j] / 8000.0;

            if (d7 < 0.0) {
                d7 = -d7 * 0.3;
            }

            d7 = d7 * 3.0 - 2.0;

            if (d7 < 0.0) {
                d7 = d7 / 2.0;

                if (d7 < -1.0) {
                    d7 = -1.0;
                }

                d7 = d7 / 1.4;
                d7 = d7 / 2.0;
            } else {
                if (d7 > 1.0) {
                    d7 = 1.0;
                }

                d7 = d7 / 8.0;
            }

            ++j;
            double d8 = (double)f3;
            double d9 = (double)f2;
            d8 = d8 + d7 * 0.2;
            d8 = d8 * (double)baseSize / 8.0;
            double d0 = (double)baseSize + d8 * 4.0;

            for (int l1 = 0; l1 < 33; ++l1) {
                double d1 = ((double)l1 - d0) * (double)stretchY * 128.0 / 256.0 / d9;

                if (d1 < 0.0) {
                    d1 *= 4.0;
                }

                double d2 = minLimitRegion[i] / (double)lowerLimitScale;
                double d3 = maxLimitRegion[i] / (double)upperLimitScale;
                double d4 = (mainNoiseRegion[i] / 10.0 + 1.0) / 2.0;
                double d5 = mc_clamped_lerp(d2, d3, d4) - d1;

                if (l1 > 29) {
                    double d6 = (double)((float)(l1 - 29) / 3.0f);
                    d5 = d5 * (1.0 - d6) + -10.0 * d6;
                }

                heightMap[i] = d5;
                ++i;
            }
        }
    }
}

#endif /* MC_TERRAIN_SHAPE_H */
