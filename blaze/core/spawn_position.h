/* Minecraft 1.11.2 WorldServer.createSpawnPosition biome search.
 *
 * BiomeProvider.findBiomePosition evaluates the genBiomes layer on a
 * quarter-coordinate 129x129 square and reservoir-samples the allowed spawn
 * biomes with the world's java.util.Random(seed).  Evaluating one cell at a
 * time is equivalent (GenLayer output cells are coordinate-pure) and keeps
 * the existing fixed IntCache arena comfortably bounded.
 */
#ifndef MC_SPAWN_POSITION_H
#define MC_SPAWN_POSITION_H

#include "genlayer_biomes.h"
#include "mc_rng.h"

typedef struct {
    int found;
    int x;
    int z;
    int matches;
    JavaRandom random; /* cursor immediately after findBiomePosition */
} McSpawnBiomePosition;

MC_HD static inline int mc_spawn_biome_allowed(int biome) {
    return biome == B_FOREST || biome == B_PLAINS || biome == B_TAIGA
        || biome == B_TAIGA_HILLS || biome == B_FOREST_HILLS
        || biome == B_JUNGLE || biome == B_JUNGLE_HILLS;
}

MC_HD MC_NOINLINE static McSpawnBiomePosition mc_spawn_biome_position(i64 seed) {
    GLNode nodes[GL_MAX_NODES];
    GlArena arena;
    McSpawnBiomePosition out;
    int voronoi;
    (void)gl_build(nodes, seed, &voronoi);
    out.found = 0;
    out.x = 8;
    out.z = 8;
    out.matches = 0;
    jrand_set(&out.random, seed);

    /* Java scans the row-major int[] returned for (-64,-64,129,129). */
    for (int qz = -64; qz <= 64; ++qz) {
        for (int qx = -64; qx <= 64; ++qx) {
            arena.off = 0;
            int biome = gl_getInts(
                nodes, &arena, nodes[voronoi].parent, qx, qz, 1, 1)[0];
            if (mc_spawn_biome_allowed(biome)
                    && (!out.found
                        || jrand_int_bound(&out.random, out.matches + 1) == 0)) {
                out.x = qx << 2;
                out.z = qz << 2;
                ++out.matches;
                out.found = 1;
            }
        }
    }
    return out;
}

#endif
