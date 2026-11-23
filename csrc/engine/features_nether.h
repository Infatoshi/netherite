/* The Nether and End lane's features (csrc/engine/features_nether.c): the
 * generators ChunkProviderHell.populate and BiomeEndDecorator.run, one table
 * row per row this lane appended to the oracle's FeatureProbe tables, with
 * the same config indices. */
#ifndef NETHERITE_FEATURES_NETHER_H
#define NETHERITE_FEATURES_NETHER_H

#include "features.h"

/* The rows for one of this lane's feature names, or NULL; *n gets their
 * number. */
const struct feature *features_nether_for(const char *name, int *n);

/* WorldGenMinable with the replaced block a parameter (the quartz row's
 * Blocks.netherrack); feature_minable is this with Blocks.stone. */
int feature_minable_target(struct world *w, jrand *r, int x, int y, int z, int block, int count,
                           int target);

/* The ender crystal WorldGenSpikes would have spawned at its generate that
 * last returned 1, cleared at every call: 0 when it spawned none, otherwise
 * the position and the yaw it drew. Entities are not ported; the feature test
 * compares this report against the oracle's record. */
struct spike_crystal
{
    int spawned;
    double x, y, z;
    float yaw;
};

#endif
