/* caves_real: MapGenCaves on REAL terrain primer from sbr_run (surface_blocks_real.h) with
 * per-column genlayer biomes driving isTopBlock/digBlock via curTop[]/curFiller[] (not Plains).
 *
 * Pipeline for chunk (chunkX,chunkZ):
 *   1. cpbw_run + replaceBiomeBlocks (same as sbr_run, capturing fullBiome/curTop/curFiller)
 *   2. MapGenBase.generate -> MapGenCaves.recursiveGenerate (core/chunk_provider.h cp_cave_*)
 *
 * READ-ONLY compose: surface_blocks_real.h, caves.h (algorithm reference), genlayer_biomes.h,
 * biome_props_full.h (via surface_blocks_real).
 * Output: ChunkPrimer char[65536] in CB_* ids, index order, %04x one per line. */
#ifndef MC_CAVES_REAL_H
#define MC_CAVES_REAL_H

#include "surface_blocks_real.h"
#include "mc_math.h"

typedef struct {
    CpScratch sc;
    int fullBiome[256];
    int curTop[256];
    int curFiller[256];
} CvrScratch;

/* replaceBiomeBlocks with captured biome arrays (mirrors sbr_replaceBiomeBlocks). */
MC_HD MC_NOINLINE static void cvr_replaceBiomeBlocks(ChunkPrimer *primer, CvrScratch *ctx, i64 seed,
        int chunkX, int chunkZ) {
    GLNode nodes[GL_MAX_NODES];
    int voronoi;
    gl_build(nodes, seed, &voronoi);

    CpPerlin *surfaceNoise = &ctx->sc.surfaceNoise;   /* preallocated (no in-kernel malloc) */
    CpPerlin *grassNoise = &ctx->sc.grassNoise;
    cp_surface_noise_init(surfaceNoise, seed);
    cp_grass_noise_init(grassNoise);

    for (int b = 0; b < 256; ++b) {
        ctx->curTop[b] = mc_bpf_topBlock(b);
        ctx->curFiller[b] = mc_bpf_fillerBlock(b);
    }

    JavaRandom rand;
    jrand_set(&rand, (i64)chunkX * 341873128712LL + (i64)chunkZ * 132897987541LL);

    ctx->sc.arena.off = 0;   /* reset bump arena at top-level tree */
    int *fullBiome = gl_getInts(nodes, &ctx->sc.arena, voronoi, chunkX * 16, chunkZ * 16, 16, 16);
    for (int i = 0; i < 256; ++i)
        ctx->fullBiome[i] = fullBiome[i];

    cp_perlin_getRegion(surfaceNoise, ctx->sc.depthBuffer, (double)(chunkX * 16), (double)(chunkZ * 16),
                        16, 16, 0.0625, 0.0625, 1.0, 0.5);
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            int biome = ctx->fullBiome[j + i * 16];
            sbr_genTerrainBlocks(biome, &rand, primer, chunkX * 16 + i, chunkZ * 16 + j,
                                 ctx->sc.depthBuffer[j + i * 16], ctx->curTop, ctx->curFiller, grassNoise);
        }
    }
}

MC_HD MC_NOINLINE static void cvr_run(ChunkPrimer *primer, CvrScratch *ctx, i64 seed,
                                int chunkX, int chunkZ, const McSinTable *st) {
    cpbw_run(primer, &ctx->sc, seed, chunkX, chunkZ);
    cvr_replaceBiomeBlocks(primer, ctx, seed, chunkX, chunkZ);

    CaveCtx cc;
    cc.primer = primer;
    cc.st = st;
    cc.fullBiome = ctx->fullBiome;
    cc.curTop = ctx->curTop;
    cc.curFiller = ctx->curFiller;
    cp_cave_generate(&cc, seed, chunkX, chunkZ);
}

#endif /* MC_CAVES_REAL_H */
