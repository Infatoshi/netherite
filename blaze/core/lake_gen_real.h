/* lake_gen_real: WorldGenLakes on REAL terrain primer from sbr_run (surface_blocks_real.h) with
 * per-column genlayer biomes driving dirt->topBlock (mycelium path) and canBlockFreezeWater (ice).
 *
 * Pipeline for chunk (chunkX,chunkZ):
 *   1. cpbw_run + replaceBiomeBlocks (capture fullBiome/curTop[])
 *   2. ChunkProviderOverworld.populate decoration RNG -> water/lava WorldGenLakes attempts
 *
 * READ-ONLY compose: surface_blocks_real.h (sbr_genTerrainBlocks), lake_gen.h (algorithm reference),
 * biome_props_full.h (mc_bpf_temperature). Block ids are CB_* (chunk_provider.h).
 * Output: ChunkPrimer char[65536] in index order, %04x one per line. */
#ifndef MC_LAKE_GEN_REAL_H
#define MC_LAKE_GEN_REAL_H

#include "surface_blocks_real.h"
#include "lake_gen.h"

typedef struct {
    CpScratch sc;
    int fullBiome[256];
    int curTop[256];
    int curFiller[256];
} LgrCtx;

MC_HD MC_NOINLINE static int lgr_height(const ChunkPrimer *p, int x, int z) {
    for (int y = 255; y >= 0; --y)
        if (cb_get(p, x, y, z) != CB_AIR) return y;
    return 0;
}

MC_HD MC_NOINLINE static int lgr_sky_light(const ChunkPrimer *p, int x, int y, int z) {
    return y >= lgr_height(p, x, z) ? 15 : 0;
}

MC_HD MC_NOINLINE static int lgr_block_light(const ChunkPrimer *p, int x, int y, int z) {
    return y > lgr_height(p, x, z) ? 15 : 0;
}

MC_HD MC_NOINLINE static int lgr_is_liquid(int s) {
    return s == CB_WATER || s == CB_FLOWING_WATER || s == CB_LAVA || s == CB_FLOWING_LAVA;
}

MC_HD MC_NOINLINE static int lgr_is_solid(int s) {
    if (s == CB_AIR) return 0;
    if (lgr_is_liquid(s)) return 0;
    return 1;
}

MC_HD MC_NOINLINE static int lgr_biome_at(const LgrCtx *ctx, int x, int z) {
    if (x < 0 || x >= 16 || z < 0 || z >= 16) return 1;
    return ctx->fullBiome[x + z * 16];
}

MC_HD MC_NOINLINE static int lgr_biome_top_block(const LgrCtx *ctx, int x, int z) {
    return ctx->curTop[lgr_biome_at(ctx, x, z)];
}

/* World.canBlockFreezeBody(pos, false) without adjacency: cold biome + water + block light < 10. */
MC_HD MC_NOINLINE static int lgr_can_block_freeze_water(const ChunkPrimer *p, const LgrCtx *ctx,
        int x, int y, int z) {
    if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= 256) return 0;
    float temp = mc_bpf_temperature(lgr_biome_at(ctx, x, z));
    if (cb_getFloatTemperature(temp, y) >= 0.15f) return 0;
    if (lgr_block_light(p, x, y, z) >= 10) return 0;
    int b = cb_get(p, x, y, z);
    return b == CB_WATER || b == CB_FLOWING_WATER;
}

MC_HD MC_NOINLINE static void lgr_replaceBiomeBlocks(ChunkPrimer *primer, LgrCtx *ctx, i64 seed,
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

/* WorldGenLakes.generate on chunk-local ChunkPrimer (decoration coords 8-23 in x/z). */
MC_HD MC_NOINLINE static int lgr_lake_gen(ChunkPrimer *primer, LgrCtx *ctx, JavaRandom *rand,
        int posX, int posY, int posZ, int liquidBlockId) {
    int px = posX - 8, py = posY, pz = posZ - 8;

    for (; py > 5 && cb_get(primer, px, py, pz) == CB_AIR; --py) {
        ;
    }

    if (py <= 4) {
        return 0;
    } else {
        py -= 4;
        char aboolean[2048];
        for (int t = 0; t < 2048; ++t) aboolean[t] = 0;
        int i = jrand_int_bound(rand, 4) + 4;

        for (int j = 0; j < i; ++j) {
            double d0 = jrand_double(rand) * 6.0 + 3.0;
            double d1 = jrand_double(rand) * 4.0 + 2.0;
            double d2 = jrand_double(rand) * 6.0 + 3.0;
            double d3 = jrand_double(rand) * (16.0 - d0 - 2.0) + 1.0 + d0 / 2.0;
            double d4 = jrand_double(rand) * (8.0 - d1 - 4.0) + 2.0 + d1 / 2.0;
            double d5 = jrand_double(rand) * (16.0 - d2 - 2.0) + 1.0 + d2 / 2.0;

            for (int l = 1; l < 15; ++l) {
                for (int i1 = 1; i1 < 15; ++i1) {
                    for (int j1 = 1; j1 < 7; ++j1) {
                        double d6 = ((double)l - d3) / (d0 / 2.0);
                        double d7 = ((double)j1 - d4) / (d1 / 2.0);
                        double d8 = ((double)i1 - d5) / (d2 / 2.0);
                        double d9 = d6 * d6 + d7 * d7 + d8 * d8;

                        if (d9 < 1.0) {
                            aboolean[(l * 16 + i1) * 8 + j1] = 1;
                        }
                    }
                }
            }
        }

        for (int k1 = 0; k1 < 16; ++k1) {
            for (int l2 = 0; l2 < 16; ++l2) {
                for (int k = 0; k < 8; ++k) {
                    int bx = px + k1, by = py + k, bz = pz + l2;
                    if (bx < 0 || bx >= 16 || bz < 0 || bz >= 16 || by < 0 || by >= 256)
                        continue;
                    int flag = !aboolean[(k1 * 16 + l2) * 8 + k] &&
                               (k1 < 15 && aboolean[((k1 + 1) * 16 + l2) * 8 + k] ||
                                k1 > 0 && aboolean[((k1 - 1) * 16 + l2) * 8 + k] ||
                                l2 < 15 && aboolean[(k1 * 16 + l2 + 1) * 8 + k] ||
                                l2 > 0 && aboolean[(k1 * 16 + (l2 - 1)) * 8 + k] ||
                                k < 7 && aboolean[(k1 * 16 + l2) * 8 + k + 1] ||
                                k > 0 && aboolean[(k1 * 16 + l2) * 8 + (k - 1)]);

                    if (flag) {
                        int state = cb_get(primer, bx, by, bz);

                        if (k >= 4 && lgr_is_liquid(state)) {
                            return 0;
                        }

                        if (k < 4 && !lgr_is_solid(state) && state != liquidBlockId) {
                            return 0;
                        }
                    }
                }
            }
        }

        for (int l1 = 0; l1 < 16; ++l1) {
            for (int i3 = 0; i3 < 16; ++i3) {
                for (int i4 = 0; i4 < 8; ++i4) {
                    if (aboolean[(l1 * 16 + i3) * 8 + i4]) {
                        int bx = px + l1, by = py + i4, bz = pz + i3;
                        if (bx >= 0 && bx < 16 && bz >= 0 && bz < 16 && by >= 0 && by < 256)
                            cb_set(primer, bx, by, bz, i4 >= 4 ? CB_AIR : liquidBlockId);
                    }
                }
            }
        }

        for (int i2 = 0; i2 < 16; ++i2) {
            for (int j3 = 0; j3 < 16; ++j3) {
                for (int j4 = 4; j4 < 8; ++j4) {
                    if (aboolean[(i2 * 16 + j3) * 8 + j4]) {
                        int bx = px + i2, by = py + (j4 - 1), bz = pz + j3;
                        if (bx < 0 || bx >= 16 || bz < 0 || bz >= 16 || by < 0 || by >= 256)
                            continue;
                        if (cb_get(primer, bx, by, bz) == CB_DIRT &&
                            lgr_sky_light(primer, px + i2, py + j4, pz + j3) > 0) {
                            int top = lgr_biome_top_block(ctx, bx, bz);
                            cb_set(primer, bx, by, bz, top);
                        }
                    }
                }
            }
        }

        if (liquidBlockId == CB_LAVA || liquidBlockId == CB_FLOWING_LAVA) {
            for (int j2 = 0; j2 < 16; ++j2) {
                for (int k3 = 0; k3 < 16; ++k3) {
                    for (int k4 = 0; k4 < 8; ++k4) {
                        int bx = px + j2, by = py + k4, bz = pz + k3;
                        if (bx < 0 || bx >= 16 || bz < 0 || bz >= 16 || by < 0 || by >= 256)
                            continue;
                        int flag1 = !aboolean[(j2 * 16 + k3) * 8 + k4] &&
                                    (j2 < 15 && aboolean[((j2 + 1) * 16 + k3) * 8 + k4] ||
                                     j2 > 0 && aboolean[((j2 - 1) * 16 + k3) * 8 + k4] ||
                                     k3 < 15 && aboolean[(j2 * 16 + k3 + 1) * 8 + k4] ||
                                     k3 > 0 && aboolean[(j2 * 16 + (k3 - 1)) * 8 + k4] ||
                                     k4 < 7 && aboolean[(j2 * 16 + k3) * 8 + k4 + 1] ||
                                     k4 > 0 && aboolean[(j2 * 16 + k3) * 8 + (k4 - 1)]);

                        if (flag1 && (k4 < 4 || jrand_int_bound(rand, 2) != 0) &&
                            lgr_is_solid(cb_get(primer, bx, by, bz))) {
                            cb_set(primer, bx, by, bz, CB_STONE);
                        }
                    }
                }
            }
        }

        if (liquidBlockId == CB_WATER || liquidBlockId == CB_FLOWING_WATER) {
            for (int k2 = 0; k2 < 16; ++k2) {
                for (int l3 = 0; l3 < 16; ++l3) {
                    int bx = px + k2, by = py + 4, bz = pz + l3;
                    if (bx >= 0 && bx < 16 && bz >= 0 && bz < 16 && by >= 0 && by < 256 &&
                        lgr_can_block_freeze_water(primer, ctx, bx, by, bz)) {
                        cb_set(primer, bx, by, bz, CB_ICE);
                    }
                }
            }
        }

        return 1;
    }
}

/* sbr terrain + populate decoration lake attempts for chunk (chunkX,chunkZ). */
MC_HD MC_NOINLINE static void lgr_run(ChunkPrimer *primer, LgrCtx *ctx, i64 seed,
                                int chunkX, int chunkZ) {
    cpbw_run(primer, &ctx->sc, seed, chunkX, chunkZ);
    lgr_replaceBiomeBlocks(primer, ctx, seed, chunkX, chunkZ);

    JavaRandom r;
    jrand_set(&r, seed);
    i64 k = jrand_long(&r) / 2 * 2 + 1;
    i64 l = jrand_long(&r) / 2 * 2 + 1;
    jrand_set(&r, (i64)chunkX * k + (i64)chunkZ * l ^ seed);

    int biome = ctx->fullBiome[8 + 8 * 16];
    if (biome != 2 && biome != 17 && jrand_int_bound(&r, 4) == 0) {
        int i1 = jrand_int_bound(&r, 16) + 8;
        int j1 = jrand_int_bound(&r, 256);
        int k1 = jrand_int_bound(&r, 16) + 8;
        lgr_lake_gen(primer, ctx, &r, i1, j1, k1, CB_WATER);
    }
    if (jrand_int_bound(&r, 8) == 0) {
        int i2 = jrand_int_bound(&r, 16) + 8;
        int inner = jrand_int_bound(&r, 248) + 8;
        int l2 = jrand_int_bound(&r, inner);
        int k3 = jrand_int_bound(&r, 16) + 8;
        if (l2 < CB_SEA_LEVEL || jrand_int_bound(&r, 10) == 0)
            lgr_lake_gen(primer, ctx, &r, i2, l2, k3, CB_LAVA);
    }
}

#endif /* MC_LAKE_GEN_REAL_H */
