/* chunk_provider_biome_wired: ChunkProviderOverworld.setBlocksInChunk with REAL genlayer biomes
 * (10x10 genBiomes/rivermix neighborhood) driving generateHeightmap via biome_props_full.h
 * mc_bpf_baseHeight/mc_bpf_heightVariation (NOT chunk_provider.h cb_* Plains shortcut).
 *
 * Pipeline for chunk (chunkX,chunkZ):
 *   1. gl_build + gl_getInts(rivermix, chunkX*4-2, chunkZ*4-2, 10, 10)  (low-res biomes)
 *   2. terrain_noise_init + cpbw_generateHeightmap (REAL per-biome blend, verbatim vanilla)
 *   3. cp_setBlocksInChunk (density -> STONE / oceanBlock WATER)
 *
 * Output: ChunkPrimer char[65536] in CB_* block-state ids (same substitution as chunk_provider).
 * READ-ONLY compose: chunk_provider.h (ChunkPrimer, cp_setBlocksInChunk, CpScratch),
 * biome_props_full.h, genlayer_biomes.h, terrain_shape.h, mc_noise.h, mc_rng.h. */
#ifndef MC_CHUNK_PROVIDER_BIOME_WIRED_H
#define MC_CHUNK_PROVIDER_BIOME_WIRED_H

#include <stdlib.h>
#include <math.h>
#include "chunk_provider.h"
#include "biome_props_full.h"
#include "genlayer_biomes.h"
#include "terrain_shape.h"

/* generateHeightmap verbatim, but mc_bpf_baseHeight/mc_bpf_heightVariation instead of cb_*. */
MC_HD MC_NOINLINE static void cpbw_generateHeightmap(CpScratch *sc, const TerrainNoise *t,
        const int *lowBiome, int p_1, int p_2, int p_3) {
    const float coordinateScale = 684.412f, heightScale = 684.412f;
    const float upperLimitScale = 512.0f, lowerLimitScale = 512.0f;
    const float depthNoiseScaleX = 200.0f, depthNoiseScaleZ = 200.0f;
    const float mainNoiseScaleX = 80.0f, mainNoiseScaleY = 160.0f, mainNoiseScaleZ = 80.0f;
    const float baseSize = 8.5f, stretchY = 12.0f;
    const float biomeDepthWeight = 1.0f, biomeDepthOffSet = 0.0f;
    const float biomeScaleWeight = 1.0f, biomeScaleOffset = 0.0f;

    float biomeWeights[25];
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j) {
            float f = 10.0f / (float)sqrt((double)((float)(i * i + j * j) + 0.2f));
            biomeWeights[i + 2 + (j + 2) * 5] = f;
        }

    mc_oct_generate(&t->depth, sc->depthRegion, p_1, 10, p_3, 5, 1, 5,
                    (double)depthNoiseScaleX, 1.0, (double)depthNoiseScaleZ);
    float f = coordinateScale, f1 = heightScale;
    mc_oct_generate(&t->mainP, sc->mainNoiseRegion, p_1, p_2, p_3, 5, 33, 5,
                    (double)(f / mainNoiseScaleX), (double)(f1 / mainNoiseScaleY), (double)(f / mainNoiseScaleZ));
    mc_oct_generate(&t->minLimit, sc->minLimitRegion, p_1, p_2, p_3, 5, 33, 5, (double)f, (double)f1, (double)f);
    mc_oct_generate(&t->maxLimit, sc->maxLimitRegion, p_1, p_2, p_3, 5, 33, 5, (double)f, (double)f1, (double)f);
    int i = 0, j = 0;
    for (int k = 0; k < 5; ++k) {
        for (int l = 0; l < 5; ++l) {
            float f2 = 0.0f, f3 = 0.0f, f4 = 0.0f;
            int biome = lowBiome[k + 2 + (l + 2) * 10];
            for (int j1 = -2; j1 <= 2; ++j1) {
                for (int k1 = -2; k1 <= 2; ++k1) {
                    int biome1 = lowBiome[k + j1 + 2 + (l + k1 + 2) * 10];
                    float f5 = biomeDepthOffSet + mc_bpf_baseHeight(biome1) * biomeDepthWeight;
                    float f6 = biomeScaleOffset + mc_bpf_heightVariation(biome1) * biomeScaleWeight;
                    float f7 = biomeWeights[j1 + 2 + (k1 + 2) * 5] / (f5 + 2.0f);
                    if (mc_bpf_baseHeight(biome1) > mc_bpf_baseHeight(biome)) f7 /= 2.0f;
                    f2 += f6 * f7;
                    f3 += f5 * f7;
                    f4 += f7;
                }
            }
            f2 = f2 / f4;
            f3 = f3 / f4;
            f2 = f2 * 0.9f + 0.1f;
            f3 = (f3 * 4.0f - 1.0f) / 8.0f;
            double d7 = sc->depthRegion[j] / 8000.0;
            if (d7 < 0.0) d7 = -d7 * 0.3;
            d7 = d7 * 3.0 - 2.0;
            if (d7 < 0.0) {
                d7 = d7 / 2.0;
                if (d7 < -1.0) d7 = -1.0;
                d7 = d7 / 1.4;
                d7 = d7 / 2.0;
            } else {
                if (d7 > 1.0) d7 = 1.0;
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
                if (d1 < 0.0) d1 *= 4.0;
                double d2 = sc->minLimitRegion[i] / (double)lowerLimitScale;
                double d3 = sc->maxLimitRegion[i] / (double)upperLimitScale;
                double d4 = (sc->mainNoiseRegion[i] / 10.0 + 1.0) / 2.0;
                double d5 = mc_clamped_lerp(d2, d3, d4) - d1;
                if (l1 > 29) {
                    double d6 = (double)((float)(l1 - 29) / 3.0f);
                    d5 = d5 * (1.0 - d6) + -10.0 * d6;
                }
                sc->heightMap[i] = d5;
                ++i;
            }
        }
    }
}

/* setBlocksInChunk only: genlayer low-res biomes -> heightmap -> stone/water primer. */
MC_HD MC_NOINLINE static void cpbw_run(ChunkPrimer *primer, CpScratch *sc, i64 seed,
                                  int chunkX, int chunkZ) {
    for (int idx = 0; idx < 65536; ++idx) primer->data[idx] = (u16)CB_AIR;

    GLNode nodes[GL_MAX_NODES];
    int voronoi;
    gl_build(nodes, seed, &voronoi);
    int rivermix = nodes[voronoi].parent;

    TerrainNoise *tnoise = &sc->tnoise;   /* preallocated (no in-kernel malloc) */
    terrain_noise_init(tnoise, seed);

    sc->arena.off = 0;   /* reset bump arena at top-level tree */
    int *lowBiome = gl_getInts(nodes, &sc->arena, rivermix, chunkX * 4 - 2, chunkZ * 4 - 2, 10, 10);
    cpbw_generateHeightmap(sc, tnoise, lowBiome, chunkX * 4, 0, chunkZ * 4);
    cp_setBlocksInChunk(primer, sc->heightMap);
}

#endif /* MC_CHUNK_PROVIDER_BIOME_WIRED_H */
