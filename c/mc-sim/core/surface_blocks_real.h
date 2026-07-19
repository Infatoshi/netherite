/* surface_blocks_real: ChunkProviderOverworld.replaceBiomeBlocks on REAL terrain primer from
 * cpbw_run (chunk_provider_biome_wired.h setBlocksInChunk), with full-res genlayer biomes driving
 * per-column topBlock/fillerBlock/temperature via biome_props_full.h mc_bpf_* (not Plains shortcut).
 *
 * Pipeline for chunk (chunkX,chunkZ):
 *   1. cpbw_run -> stone/water ChunkPrimer from genlayer low-res + biome_props heightmap
 *   2. gl_build + gl_getInts(voronoi, chunkX*16, chunkZ*16, 16, 16) -> fullBiome[256]
 *   3. surfaceNoise depthBuffer + genTerrainBlocks per column (Hills/Taiga/Swamp dispatch)
 *
 * READ-ONLY compose: chunk_provider_biome_wired.h, surface_blocks.h (logic via cp_* noise in
 * chunk_provider.h), biome_props_full.h, genlayer_biomes.h.
 * Output: full ChunkPrimer char[65536] in CB_* ids, index order, %04x one per line. */
#ifndef MC_SURFACE_BLOCKS_REAL_H
#define MC_SURFACE_BLOCKS_REAL_H

#include "chunk_provider_biome_wired.h"
#include "biome_props_full.h"

/* genTerrainBlocks with mc_bpf_* (mirrors cp_genTerrainBlocks + surface_blocks generateBiomeTerrain). */
MC_HD MC_NOINLINE static void sbr_genTerrainBlocks(int biome, JavaRandom *rand, ChunkPrimer *primer,
        int x, int z, double noiseVal, int *curTop, int *curFiller, const CpPerlin *grassNoise) {
    int type = mc_bpf_genTerrainType(biome);
    if (type == BPF_GT_HILLS) {
        curTop[biome] = CB_GRASS;
        curFiller[biome] = CB_DIRT;
        int ht = mc_bpf_hillsType(biome);
        if ((noiseVal < -1.0 || noiseVal > 2.0) && ht == BPF_HILLS_MUTATED) {
            curTop[biome] = CB_GRAVEL;
            curFiller[biome] = CB_GRAVEL;
        } else if (noiseVal > 1.0 && ht != BPF_HILLS_EXTRA_TREES) {
            curTop[biome] = CB_STONE;
            curFiller[biome] = CB_STONE;
        }
    } else if (type == BPF_GT_TAIGA) {
        int tt = mc_bpf_taigaType(biome);
        if (tt == BPF_TAIGA_MEGA || tt == BPF_TAIGA_MEGA_SPRUCE) {
            curTop[biome] = CB_GRASS;
            curFiller[biome] = CB_DIRT;
            if (noiseVal > 1.75) curTop[biome] = CB_COARSE_DIRT;
            else if (noiseVal > -0.95) curTop[biome] = CB_PODZOL;
        }
    } else if (type == BPF_GT_SWAMP) {
        double d0 = cp_perlin_getValue(grassNoise, (double)x * 0.25, (double)z * 0.25);
        if (d0 > 0.0) {
            int ii = x & 15;
            int jj = z & 15;
            for (int kk = 255; kk >= 0; --kk) {
                if (cb_get(primer, jj, kk, ii) != CB_AIR) {
                    if (kk == 62 && cb_get(primer, jj, kk, ii) != CB_WATER) {
                        cb_set(primer, jj, kk, ii, CB_WATER);
                        if (d0 < 0.12) cb_set(primer, jj, kk + 1, ii, CB_WATER_LILY);
                    }
                    break;
                }
            }
        }
    }
    cp_generateBiomeTerrain(rand, primer, x, z, noiseVal, curTop[biome], curFiller[biome],
                            mc_bpf_temperature(biome));
}

MC_HD MC_NOINLINE static void sbr_replaceBiomeBlocks(ChunkPrimer *primer, CpScratch *sc, i64 seed,
        int chunkX, int chunkZ) {
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
}

/* cpbw terrain primer + replaceBiomeBlocks for chunk (chunkX,chunkZ). */
MC_HD MC_NOINLINE static void sbr_run(ChunkPrimer *primer, CpScratch *sc, i64 seed,
                                int chunkX, int chunkZ) {
    cpbw_run(primer, sc, seed, chunkX, chunkZ);
    sbr_replaceBiomeBlocks(primer, sc, seed, chunkX, chunkZ);
}

#endif /* MC_SURFACE_BLOCKS_REAL_H */
