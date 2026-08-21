/* WorldServer.createSpawnPosition / BiomeProvider.findBiomePosition. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#endif
#define GL_ARENA_INTS 262144
#include "genlayer_biomes.h"
#include "mc_rng.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#if GL_ARENA_INTS < 200000
#error "findBiomePosition(0,0,256) needs GL_ARENA_INTS >= 200000 (measured 180582)"
#endif

#include "game/world_spawn.h"

#include <stdlib.h>

static int spawn_biome_ok(int id) {
    return id == B_FOREST || id == B_PLAINS || id == B_TAIGA ||
           id == B_TAIGA_HILLS || id == B_FOREST_HILLS || id == B_JUNGLE ||
           id == B_JUNGLE_HILLS;
}

int gm_create_spawn_position(long long seed,
                             int (*can_spawn)(void *ctx, int x, int z),
                             void *ctx,
                             int *block_x, int *block_y, int *block_z)
{
    GLNode nodes[GL_MAX_NODES];
    int voronoi;
    JavaRandom r;
    GlArena *arena;
    int rivermix, i, j, k, l, i1, j1, found, k1, bx, bz, l1;
    int *aint;

    if (!block_x || !block_y || !block_z) return 0;
    gl_build(nodes, (i64)seed, &voronoi);
    rivermix = nodes[voronoi].parent;
    jrand_set(&r, (i64)seed);

    /* findBiomePosition(0, 0, 256, biomesToSpawnIn, random) */
    i = (0 - 256) >> 2;
    j = (0 - 256) >> 2;
    k = (0 + 256) >> 2;
    l = (0 + 256) >> 2;
    i1 = k - i + 1;
    j1 = l - j + 1;
    arena = (GlArena *)malloc(sizeof(GlArena));
    if (!arena) return 0;
    arena->off = 0;
    aint = gl_getInts(nodes, arena, rivermix, i, j, i1, j1);
    found = 0;
    k1 = 0;
    bx = 8;
    bz = 8;
    for (l1 = 0; l1 < i1 * j1; ++l1) {
        int i2 = (i + l1 % i1) << 2;
        int j2 = (j + l1 / i1) << 2;
        if (spawn_biome_ok(aint[l1]) && (!found || jrand_int_bound(&r, k1 + 1) == 0)) {
            bx = i2;
            bz = j2;
            found = 1;
            ++k1;
        }
    }
    free(arena);

    if (can_spawn) {
        int walk = 0;
        while (!can_spawn(ctx, bx, bz)) {
            bx += jrand_int_bound(&r, 64) - jrand_int_bound(&r, 64);
            bz += jrand_int_bound(&r, 64) - jrand_int_bound(&r, 64);
            ++walk;
            if (walk == 1000) break;
        }
    }

    *block_x = bx;
    *block_y = 64;
    *block_z = bz;
    return 1;
}
