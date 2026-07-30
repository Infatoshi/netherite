/* ravines_real: MapGenRavine on REAL terrain primer from cpbw_run + replaceBiomeBlocks
 * (chunk_provider_biome_wired + biome_props_full genlayer biomes), not the Wave-1 all-stone harness.
 *
 * Pipeline for chunk (chunkX,chunkZ):
 *   1. cpbw_run -> stone/water ChunkPrimer (real heightmap blend)
 *   2. full-res voronoi biomes + surfaceNoise + sbr_genTerrainBlocks (mc_bpf_* top/filler)
 *   3. MapGenRavine.generate (cp_rav_* from chunk_provider.h) with curTop[]/curFiller[] after surface
 *
 * READ-ONLY compose: surface_blocks_real.h (sbr_genTerrainBlocks), chunk_provider.h (cp_rav_*),
 * biome_props_full.h, genlayer_biomes.h.
 * Output: ChunkPrimer char[65536] in CB_* ids, index order, %04x one per line. */
#ifndef MC_RAVINES_REAL_H
#define MC_RAVINES_REAL_H

#include "surface_blocks_real.h"
#include "chunk_provider.h"

MC_HD MC_NOINLINE static void rr_run(ChunkPrimer *primer, CpScratch *sc, const McSinTable *st,
        i64 seed, int chunkX, int chunkZ) {
    cpbw_run(primer, sc, seed, chunkX, chunkZ);

    GLNode nodes[GL_MAX_NODES];
    int voronoi;
    gl_build(nodes, seed, &voronoi);

    CpPerlin *surfaceNoise = &sc->surfaceNoise;   /* preallocated (no in-kernel malloc) */
    CpPerlin *grassNoise = &sc->grassNoise;
    cp_surface_noise_init(surfaceNoise, seed);
    cp_grass_noise_init(grassNoise);

    int curTop[256], curFiller[256];
    for (int b = 0; b < 256; ++b) {
        curTop[b] = mc_bpf_topBlock(b);
        curFiller[b] = mc_bpf_fillerBlock(b);
    }

    JavaRandom rand;
    jrand_set(&rand, (i64)chunkX * 341873128712LL + (i64)chunkZ * 132897987541LL);

    sc->arena.off = 0;   /* reset bump arena at top-level tree */
    int *fullBiome = gl_getInts(nodes, &sc->arena, voronoi, chunkX * 16, chunkZ * 16, 16, 16);

    cp_perlin_getRegion(surfaceNoise, sc->depthBuffer, (double)(chunkX * 16), (double)(chunkZ * 16),
                        16, 16, 0.0625, 0.0625, 1.0, 0.5);
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            int biome = fullBiome[j + i * 16];
            sbr_genTerrainBlocks(biome, &rand, primer, chunkX * 16 + i, chunkZ * 16 + j,
                                 sc->depthBuffer[j + i * 16], curTop, curFiller, grassNoise);
        }
    }

    RavineCtx rctx;
    rctx.primer = primer;
    rctx.st = st;
    rctx.fullBiome = fullBiome;
    rctx.curTop = curTop;
    rctx.curFiller = curFiller;
    rctx.worldSeed = seed;
    cp_rav_generate(&rctx, chunkX, chunkZ);
}

#endif /* MC_RAVINES_REAL_H */
