#ifndef MAGMA_WORLD_SPAWN_H
#define MAGMA_WORLD_SPAWN_H

/* WorldServer.createSpawnPosition for WorldType.DEFAULT.
 * findBiomePosition(0,0,256) on genBiomes (1/4-res), then the grass walk.
 * Spawn Y is provider averageGroundLevel (64). Player feet Y is a separate
 * getTopSolidOrLiquidBlock at this xz. Superflat is not this path. */

int gm_create_spawn_position(long long seed,
                             int (*can_spawn)(void *ctx, int x, int z),
                             void *ctx,
                             int *block_x, int *block_y, int *block_z);

#endif
