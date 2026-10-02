/* The population features, one entry per Java feature in the oracle's
 * FeatureProbe table. A feature is a pure function of the world, its Random
 * and its position: it writes through world_set_block (so every write goes
 * through the same path the oracle's setBlock does, including the write
 * listener) and returns 1, like WorldGenerator.generate.
 *
 * Later lanes add one row per feature here and one entry in features_for, and
 * match the same probe: oracle/harness/netherite/oracle/FeatureProbe.java's FEATURES
 * table is the other half of the pair, and the config indices must agree. */
#ifndef NETHERITE_FEATURES_H
#define NETHERITE_FEATURES_H

#include "jrand.h"
#include "world.h"

/* One feature table row: what to build, where a case may place it, and the
 * function that runs it. This is the native half of the oracle's FEATURES
 * table; feature_minable is the WorldGenerator each row's Java entry wraps. */
struct feature
{
    const char *name;
    int config;      /* index into the probe's per-feature table */
    int y0, y1;      /* the spawn band a case draws y from */
    int margin;      /* how far the feature can reach from its position */
    int block;       /* the block a case places (feature-specific) */
    int count;       /* the feature's size parameter (feature-specific) */
    int (*generate)(struct world *w, jrand *r, int x, int y, int z, int block, int count);
};

/* The rows for one feature name, or NULL; *n gets their number. */
const struct feature *features_for(const char *name, int *n);

/* WorldGenMinable.generate: an ellipsoid vein of |count| blocks through
 * (x, y, z), replacing blocks whose id is stone. Returns 1. */
int feature_minable(struct world *w, jrand *r, int x, int y, int z, int block, int count);

/* The tree generators write through WorldGenerator.func_150516_a, which is
 * setBlock flag 2 for population and flag 3 when a growing sapling built the
 * generator (WorldGenerator's doBlockNotify). The flag is switched around one
 * growth call; every other feature keeps 2. */
void features_tree_notify(int on);
int features_tree_write(struct world *w, int x, int y, int z, int id, int meta);

/* WorldGenBigTree draws its heightLimit once per generator object and keeps it
 * while the object lives; the population probe holds one generator for the
 * whole run. A growing sapling builds a fresh one, so the draw starts over. */
void features_bigtree_reset(void);

/* WorldGenBigTree's leafDistanceLimit: 5 with the setScale(1, 1, 1) the
 * population probe applies, 4 for the fresh generator a sapling builds. */
void features_bigtree_scaled(int scaled);

/* BlockSapling.func_149878_d's fallback for a lone jungle sapling:
 * WorldGenTrees(true, 4 + nextInt(7), 3, 3, no vines). The probe's jungle rows
 * carry vines, so the sapling path needs its own entry. */
int features_sapling_jungle(struct world *w, jrand *r, int x, int y, int z, int min_height);

/* WorldGenForest.generate (count is the tall-birch flag). */
int feature_forest(struct world *w, jrand *r, int x, int y, int z, int block, int count);

/* lane/treesb: the large and conifer trees. The rows live in
 * features_trees.c and features_for asks for them here, so a new group is one
 * more call rather than an edit to the tables above. */
const struct feature *features_trees_for(const char *name, int *n);

/* WorldGenMinable with the replaced block as a parameter (the nether lane's
 * quartz vein over Blocks.netherrack); feature_minable is this over
 * Blocks.stone. */
int feature_minable_target(struct world *w, jrand *r, int x, int y, int z, int block, int count,
                           int target);
/* WorldGenBigTree's heightLimit, carried between calls: one per biome object
 * on the populate path, one global for the feature probe. Save before a
 * biome's tree loop, restore after, with feature_bigtree_state_set. */
void feature_bigtree_state_set(int v);
int feature_bigtree_state_get(void);

int feature_bigtree(struct world *w, jrand *r, int x, int y, int z, int block, int count);
int feature_taiga1(struct world *w, jrand *r, int x, int y, int z, int block, int count);
int feature_taiga2(struct world *w, jrand *r, int x, int y, int z, int block, int count);
int feature_megapine(struct world *w, jrand *r, int x, int y, int z, int block, int count);
int feature_megapine_grow(struct world *w, jrand *r, int x, int y, int z, int tall);
int feature_megajungle(struct world *w, jrand *r, int x, int y, int z, int block, int count);
int feature_savanna(struct world *w, jrand *r, int x, int y, int z, int block, int count);
int feature_canopy(struct world *w, jrand *r, int x, int y, int z, int block, int count);

/* The rows of one tree feature (lane/treesa: trees, jungle, forest, swamp,
 * shrub), or NULL; *n gets their number. Mirrors the tree rows of the oracle's
 * FEATURES table; the generators behind them are in features_trees_small.c. */
const struct feature *features_trees_small_for(const char *name, int *n);

int feature_trees(struct world *w, jrand *r, int x, int y, int z, int block, int count);
int feature_jungle_tree(struct world *w, jrand *r, int x, int y, int z, int block, int count);
int feature_forest(struct world *w, jrand *r, int x, int y, int z, int block, int count);
int feature_swamp(struct world *w, jrand *r, int x, int y, int z, int block, int count);
int feature_shrub(struct world *w, jrand *r, int x, int y, int z, int block, int count);
/* The rows for one feature name from csrc/engine/features_plants.c, or NULL;
 * *n gets their number. */
const struct feature *features_plants_for(const char *name, int *n);
/* WorldGenDungeons.generate: the cobblestone room with its chests and mob
 * spawner, in csrc/engine/features_dungeons.c. Returns 1 when it built a room,
 * 0 when the terrain did not fit it, like the Java boolean. */
int feature_dungeons(struct world *w, jrand *r, int x, int y, int z, int block, int count);

#endif