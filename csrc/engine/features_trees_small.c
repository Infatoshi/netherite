/* The small tree generators of population (lane/treesa), one entry per row
 * appended to oracle/harness/netherite/oracle/FeatureProbe.java's FEATURES table:
 *
 *   trees    WorldGenTrees(false), the oak BiomeGenBase.worldGeneratorTrees
 *   jungle   WorldGenTrees(false, minTreeHeight, 3, 3, true), the tree
 *            BiomeGenJungle.func_150567_a builds, with vines and cocoa
 *   forest   WorldGenForest(false, false) birch (BiomeGenForest.field_150630_aD)
 *            and WorldGenForest(false, true) tall birch (field_150629_aC)
 *   swamp    WorldGenSwamp(), the swamp oak BiomeGenSwamp always returns
 *   shrub    WorldGenShrub(3, 0), the jungle bush BiomeGenJungle builds
 *
 * BiomeDecorator's tree loop places every one of them the same way: x and z on
 * the chunk's grid (chunk_X + nextInt(16) + 8) and y = World.getHeightValue(x,
 * z), so the probe's rows take their y from the surface, not from a band; each
 * case carries the y it used and this table ignores y0/y1.
 *
 * block and count are the row's own parameters, the way the probe's Config
 * rows carry them: for trees and jungle, count is minTreeHeight (the jungle
 * tree's height is 4 + nextInt(7) in the biome, drawn before the tree runs, so
 * every value the biome can produce has its own row); for forest, count 0 is
 * the birch and 1 the tall birch; for shrub, block is metaWood and count
 * metaLeaves; swamp takes neither.
 *
 * The leaves and logs these features write have a breakBlock callback that
 * marks the leaves around them persistent; csrc/engine/world.c ports it and the
 * metadata write it makes. */
#include "features.h"

#include <stdlib.h>
#include <string.h>

#include "blocks.h"

/* Block ids this file places or reads, Block.getIdFromBlock. */
enum {
    BLK_SAPLING = 6, BLK_FLOWING_WATER = 8, BLK_LOG = 17, BLK_LEAVES = 18,
    BLK_FARMLAND = 60, BLK_VINE = 106, BLK_COCOA = 127,
};

/* Direction.offsetX / offsetZ indexed through Direction.rotateOpposite: where
 * a cocoa pod sits relative to the trunk, for WorldGenTrees' var21. */
static const int COCOA_DX[4] = {0, 1, 0, -1};
static const int COCOA_DZ[4] = {-1, 0, 1, 0};

/* WorldGenerator.func_150516_a with doBlockNotify false: World.setBlock(...,
 * flag 2). The flags argument keeps the call sites shaped like the Java
 * source; the value is 2 here and 3 when a growing sapling drives the
 * generator (features_tree_write consults the notify flag). */
static void put(struct world *w, int x, int y, int z, int id, int meta, int flags)
{
    (void)flags;
    features_tree_write(w, x, y, z, id, meta);
}

/* Block.getMaterial(id) == Material.<name>. Vanilla compares material objects;
 * the material name is what blocks.h carries. */

/* the materials tested here, by index: looked up by name before main (a
 * regenerated blocks.h cannot shift them), the same for every environment */
static int MATI_AIR = -1, MATI_LEAVES = -1, MATI_WATER = -1;

__attribute__((constructor)) static void mati_init(void)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
    {
        if (MATERIALS[i].name == NULL) continue;
        if (strcmp(MATERIALS[i].name, "air") == 0) MATI_AIR = (int)i;
        if (strcmp(MATERIALS[i].name, "leaves") == 0) MATI_LEAVES = (int)i;
        if (strcmp(MATERIALS[i].name, "water") == 0) MATI_WATER = (int)i;
    }
}

static int mat_is(int id, int mati)
{
    return BLOCKS[id & 4095].material == mati;
}

static int is_air(struct world *w, int x, int y, int z)
{
    return mat_is(world_get_block(w, x, y, z), MATI_AIR);
}

/* Block.func_149730_j: isOpaqueCube, which the leaves answer true for
 * (BlockLeaves.isOpaqueCube is !field_150121_P and its constructor passes
 * false). */
static int is_opaque_cube(struct world *w, int x, int y, int z)
{
    return BLOCKS[world_get_block(w, x, y, z) & 4095].opaque_cube;
}

/* WorldGenAbstractTree.func_150523_a: the blocks a tree may grow through. */
static int tree_replaceable(int id)
{
    return mat_is(id, MATI_AIR) || mat_is(id, MATI_LEAVES) || id == BLK_GRASS || id == BLK_DIRT ||
           id == BLK_LOG || id == 162 /* Blocks.log2 */ || id == BLK_SAPLING || id == BLK_VINE;
}

/* WorldGenTrees.growVines: one vine, then four more down the air below it. */
static void grow_vines(struct world *w, int x, int y, int z, int meta)
{
    put(w, x, y, z, BLK_VINE, meta, 2);

    int var6 = 4;

    for (;;)
    {
        --y;

        if (!is_air(w, x, y, z) || var6 <= 0) return;

        put(w, x, y, z, BLK_VINE, meta, 2);
        --var6;
    }
}

/* WorldGenTrees.generate, with the four fields its constructor sets. Every
 * write goes through func_150516_a, which is setBlock(..., 2) for the tree the
 * decorator builds (WorldGenerator's doBlockNotify is false), and the two
 * integer divisions keep Java's truncation toward zero. */
static int gen_trees(struct world *w, jrand *r, int x, int y, int z, int min_height, int meta_wood,
                     int meta_leaves, int vines)
{
    int var6 = jr_int_n(r, 3) + min_height;
    int var7 = 1;

    if (y >= 1 && y + var6 + 1 <= 256)
    {
        for (int var8 = y; var8 <= y + 1 + var6; ++var8)
        {
            int var9 = 1;

            if (var8 == y) var9 = 0;
            if (var8 >= y + 1 + var6 - 2) var9 = 2;

            for (int var10 = x - var9; var10 <= x + var9 && var7; ++var10)
            {
                for (int var11 = z - var9; var11 <= z + var9 && var7; ++var11)
                {
                    if (var8 >= 0 && var8 < 256)
                    {
                        if (!tree_replaceable(world_get_block(w, var10, var8, var11))) var7 = 0;
                    }
                    else
                    {
                        var7 = 0;
                    }
                }
            }
        }

        if (!var7) return 0;

        int var19 = world_get_block(w, x, y - 1, z);

        if (!((var19 == BLK_GRASS || var19 == BLK_DIRT || var19 == BLK_FARMLAND) && y < 256 - var6 - 1)) return 0;

        put(w, x, y - 1, z, BLK_DIRT, 0, 2); /* func_150515_a */

        /* the canopy: three layers down from the trunk's top */
        for (int var11 = y - 3 + var6; var11 <= y + var6; ++var11)
        {
            int var21 = var11 - (y + var6);
            int var13 = 1 - var21 / 2;

            for (int var14 = x - var13; var14 <= x + var13; ++var14)
            {
                int var15 = var14 - x;

                for (int var16 = z - var13; var16 <= z + var13; ++var16)
                {
                    int var17 = var16 - z;

                    if (abs(var15) != var13 || abs(var17) != var13 || (jr_int_n(r, 2) != 0 && var21 != 0))
                    {
                        int var18 = world_get_block(w, var14, var11, var16);

                        if (mat_is(var18, MATI_AIR) || mat_is(var18, MATI_LEAVES))
                            put(w, var14, var11, var16, BLK_LEAVES, meta_leaves, 2);
                    }
                }
            }
        }

        /* the trunk, and the vines hanging off it */
        for (int var11 = 0; var11 < var6; ++var11)
        {
            int var12 = world_get_block(w, x, y + var11, z);

            if (mat_is(var12, MATI_AIR) || mat_is(var12, MATI_LEAVES))
            {
                put(w, x, y + var11, z, BLK_LOG, meta_wood, 2);

                if (vines && var11 > 0)
                {
                    if (jr_int_n(r, 3) > 0 && is_air(w, x - 1, y + var11, z))
                        put(w, x - 1, y + var11, z, BLK_VINE, 8, 2);

                    if (jr_int_n(r, 3) > 0 && is_air(w, x + 1, y + var11, z))
                        put(w, x + 1, y + var11, z, BLK_VINE, 2, 2);

                    if (jr_int_n(r, 3) > 0 && is_air(w, x, y + var11, z - 1))
                        put(w, x, y + var11, z - 1, BLK_VINE, 1, 2);

                    if (jr_int_n(r, 3) > 0 && is_air(w, x, y + var11, z + 1))
                        put(w, x, y + var11, z + 1, BLK_VINE, 4, 2);
                }
            }
        }

        if (vines)
        {
            /* vines off the canopy's leaves */
            for (int var11 = y - 3 + var6; var11 <= y + var6; ++var11)
            {
                int var21 = var11 - (y + var6);
                int var13 = 2 - var21 / 2;

                for (int var14 = x - var13; var14 <= x + var13; ++var14)
                {
                    for (int var15 = z - var13; var15 <= z + var13; ++var15)
                    {
                        if (mat_is(world_get_block(w, var14, var11, var15), MATI_LEAVES))
                        {
                            if (jr_int_n(r, 4) == 0 && is_air(w, var14 - 1, var11, var15))
                                grow_vines(w, var14 - 1, var11, var15, 8);

                            if (jr_int_n(r, 4) == 0 && is_air(w, var14 + 1, var11, var15))
                                grow_vines(w, var14 + 1, var11, var15, 2);

                            if (jr_int_n(r, 4) == 0 && is_air(w, var14, var11, var15 - 1))
                                grow_vines(w, var14, var11, var15 - 1, 1);

                            if (jr_int_n(r, 4) == 0 && is_air(w, var14, var11, var15 + 1))
                                grow_vines(w, var14, var11, var15 + 1, 4);
                        }
                    }
                }
            }

            /* cocoa pods, on a jungle tree taller than 5 */
            if (jr_int_n(r, 5) == 0 && var6 > 5)
            {
                for (int var11 = 0; var11 < 2; ++var11)
                {
                    for (int var21 = 0; var21 < 4; ++var21)
                    {
                        if (jr_int_n(r, 4 - var11) == 0)
                        {
                            int var13 = jr_int_n(r, 3);

                            put(w, x + COCOA_DX[var21], y + var6 - 5 + var11, z + COCOA_DZ[var21],
                                            BLK_COCOA, var13 << 2 | var21, 2);
                        }
                    }
                }
            }
        }

        return 1;
    }

    return 0;
}

/* WorldGenForest.generate. count is field_150531_a: the tall birch draws a
 * second height term. */
int feature_forest(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    (void)block;

    int var6 = jr_int_n(r, 3) + 5;

    if (count) var6 += jr_int_n(r, 7);

    int var7 = 1;

    if (y >= 1 && y + var6 + 1 <= 256)
    {
        for (int var8 = y; var8 <= y + 1 + var6; ++var8)
        {
            int var9 = 1;

            if (var8 == y) var9 = 0;
            if (var8 >= y + 1 + var6 - 2) var9 = 2;

            for (int var10 = x - var9; var10 <= x + var9 && var7; ++var10)
            {
                for (int var11 = z - var9; var11 <= z + var9 && var7; ++var11)
                {
                    if (var8 >= 0 && var8 < 256)
                    {
                        if (!tree_replaceable(world_get_block(w, var10, var8, var11))) var7 = 0;
                    }
                    else
                    {
                        var7 = 0;
                    }
                }
            }
        }

        if (!var7) return 0;

        int var17 = world_get_block(w, x, y - 1, z);

        if (!((var17 == BLK_GRASS || var17 == BLK_DIRT || var17 == BLK_FARMLAND) && y < 256 - var6 - 1)) return 0;

        put(w, x, y - 1, z, BLK_DIRT, 0, 2);

        for (int var18 = y - 3 + var6; var18 <= y + var6; ++var18)
        {
            int var10 = var18 - (y + var6);
            int var11 = 1 - var10 / 2;

            for (int var20 = x - var11; var20 <= x + var11; ++var20)
            {
                int var13 = var20 - x;

                for (int var14 = z - var11; var14 <= z + var11; ++var14)
                {
                    int var15 = var14 - z;

                    if (abs(var13) != var11 || abs(var15) != var11 || (jr_int_n(r, 2) != 0 && var10 != 0))
                    {
                        int var16 = world_get_block(w, var20, var18, var14);

                        if (mat_is(var16, MATI_AIR) || mat_is(var16, MATI_LEAVES))
                            put(w, var20, var18, var14, BLK_LEAVES, 2, 2);
                    }
                }
            }
        }

        for (int var18 = 0; var18 < var6; ++var18)
        {
            int var19 = world_get_block(w, x, y + var18, z);

            if (mat_is(var19, MATI_AIR) || mat_is(var19, MATI_LEAVES))
                put(w, x, y + var18, z, BLK_LOG, 2, 2);
        }

        return 1;
    }

    return 0;
}

/* WorldGenSwamp.generate: it walks down while the block below it is water,
 * then builds a wide leafy crown over a trunk that may stand in the water,
 * and hangs vines off the crown. Its leaves go through func_150515_a, meta 0,
 * and are placed only where the block is not a full cube. */
int feature_swamp(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    (void)block;
    (void)count;

    int var6 = jr_int_n(r, 4) + 5;

    while (mat_is(world_get_block(w, x, y - 1, z), MATI_WATER)) --y;

    int var7 = 1;

    if (y >= 1 && y + var6 + 1 <= 256)
    {
        for (int var8 = y; var8 <= y + 1 + var6; ++var8)
        {
            int var9 = 1;

            if (var8 == y) var9 = 0;
            if (var8 >= y + 1 + var6 - 2) var9 = 3;

            for (int var10 = x - var9; var10 <= x + var9 && var7; ++var10)
            {
                for (int var11 = z - var9; var11 <= z + var9 && var7; ++var11)
                {
                    if (var8 >= 0 && var8 < 256)
                    {
                        int var12 = world_get_block(w, var10, var8, var11);

                        if (!mat_is(var12, MATI_AIR) && !mat_is(var12, MATI_LEAVES))
                        {
                            if (var12 != BLK_WATER && var12 != BLK_FLOWING_WATER) var7 = 0;
                            else if (var8 > y) var7 = 0;
                        }
                    }
                    else
                    {
                        var7 = 0;
                    }
                }
            }
        }

        if (!var7) return 0;

        int var16 = world_get_block(w, x, y - 1, z);

        if (!((var16 == BLK_GRASS || var16 == BLK_DIRT) && y < 256 - var6 - 1)) return 0;

        put(w, x, y - 1, z, BLK_DIRT, 0, 2);

        for (int var17 = y - 3 + var6; var17 <= y + var6; ++var17)
        {
            int var10 = var17 - (y + var6);
            int var11 = 2 - var10 / 2;

            for (int var19 = x - var11; var19 <= x + var11; ++var19)
            {
                int var13 = var19 - x;

                for (int var14 = z - var11; var14 <= z + var11; ++var14)
                {
                    int var15 = var14 - z;

                    if ((abs(var13) != var11 || abs(var15) != var11 || (jr_int_n(r, 2) != 0 && var10 != 0)) &&
                        !is_opaque_cube(w, var19, var17, var14))
                        put(w, var19, var17, var14, BLK_LEAVES, 0, 2);
                }
            }
        }

        for (int var17 = 0; var17 < var6; ++var17)
        {
            int var18 = world_get_block(w, x, y + var17, z);

            if (mat_is(var18, MATI_AIR) || mat_is(var18, MATI_LEAVES) || var18 == BLK_FLOWING_WATER || var18 == BLK_WATER)
                put(w, x, y + var17, z, BLK_LOG, 0, 2);
        }

        for (int var17 = y - 3 + var6; var17 <= y + var6; ++var17)
        {
            int var10 = var17 - (y + var6);
            int var11 = 2 - var10 / 2;

            for (int var19 = x - var11; var19 <= x + var11; ++var19)
            {
                for (int var13 = z - var11; var13 <= z + var11; ++var13)
                {
                    if (mat_is(world_get_block(w, var19, var17, var13), MATI_LEAVES))
                    {
                        if (jr_int_n(r, 4) == 0 && is_air(w, var19 - 1, var17, var13))
                            grow_vines(w, var19 - 1, var17, var13, 8);

                        if (jr_int_n(r, 4) == 0 && is_air(w, var19 + 1, var17, var13))
                            grow_vines(w, var19 + 1, var17, var13, 2);

                        if (jr_int_n(r, 4) == 0 && is_air(w, var19, var17, var13 - 1))
                            grow_vines(w, var19, var17, var13 - 1, 1);

                        if (jr_int_n(r, 4) == 0 && is_air(w, var19, var17, var13 + 1))
                            grow_vines(w, var19, var17, var13 + 1, 4);
                    }
                }
            }
        }

        return 1;
    }

    return 0;
}

/* WorldGenShrub.generate, WorldGenShrub(3, 0): a one-block trunk with a two
 * block crown on top of it. It walks down to the ground first, and it answers
 * true whether or not it found soil. block is metaWood, count metaLeaves. */
int feature_shrub(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    int var6;

    while ((var6 = world_get_block(w, x, y, z), mat_is(var6, MATI_AIR) || mat_is(var6, MATI_LEAVES)) && y > 0) --y;

    int var7 = world_get_block(w, x, y, z);

    if (var7 == BLK_DIRT || var7 == BLK_GRASS)
    {
        ++y;
        put(w, x, y, z, BLK_LOG, block, 2);

        for (int var8 = y; var8 <= y + 2; ++var8)
        {
            int var9 = var8 - y;
            int var10 = 2 - var9;

            for (int var11 = x - var10; var11 <= x + var10; ++var11)
            {
                int var12 = var11 - x;

                for (int var13 = z - var10; var13 <= z + var10; ++var13)
                {
                    int var14 = var13 - z;

                    if ((abs(var12) != var10 || abs(var14) != var10 || jr_int_n(r, 2) != 0) &&
                        !is_opaque_cube(w, var11, var8, var13))
                        put(w, var11, var8, var13, BLK_LEAVES, count, 2);
                }
            }
        }
    }

    return 1;
}

int feature_trees(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    (void)block;
    return gen_trees(w, r, x, y, z, count, 0, 0, 0);
}

int features_sapling_jungle(struct world *w, jrand *r, int x, int y, int z, int min_height)
{
    return gen_trees(w, r, x, y, z, min_height, 3, 3, 0);
}

int feature_jungle_tree(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    (void)block;
    return gen_trees(w, r, x, y, z, count, 3, 3, 1);
}

/* The probe's rows for this group, in config order: appending a row here and
 * one to the Java table, at the same index, is all a new variant needs. */
static const struct feature TREES[] = {
    {"trees", 0, 0, 0, 8, 0, 4, feature_trees},
};

static const struct feature JUNGLE[] = {
    {"jungle", 0, 0, 0, 8, 0, 4, feature_jungle_tree},
    {"jungle", 1, 0, 0, 8, 0, 5, feature_jungle_tree},
    {"jungle", 2, 0, 0, 8, 0, 6, feature_jungle_tree},
    {"jungle", 3, 0, 0, 8, 0, 7, feature_jungle_tree},
    {"jungle", 4, 0, 0, 8, 0, 8, feature_jungle_tree},
    {"jungle", 5, 0, 0, 8, 0, 9, feature_jungle_tree},
    {"jungle", 6, 0, 0, 8, 0, 10, feature_jungle_tree},
};

/* block is unused; count 0 is the birch, 1 the tall birch. */
static const struct feature FOREST[] = {
    {"forest", 0, 0, 0, 8, 0, 0, feature_forest},
    {"forest", 1, 0, 0, 8, 0, 1, feature_forest},
};

static const struct feature SWAMP[] = {
    {"swamp", 0, 0, 0, 8, 0, 0, feature_swamp},
};

/* block is metaWood, count metaLeaves. */
static const struct feature SHRUB[] = {
    {"shrub", 0, 0, 0, 8, 3, 0, feature_shrub},
};

const struct feature *features_trees_small_for(const char *name, int *n)
{
    if (name == NULL) return NULL;

    if (strcmp(name, "trees") == 0)
    {
        *n = (int)(sizeof TREES / sizeof TREES[0]);
        return TREES;
    }

    if (strcmp(name, "jungle") == 0)
    {
        *n = (int)(sizeof JUNGLE / sizeof JUNGLE[0]);
        return JUNGLE;
    }

    if (strcmp(name, "forest") == 0)
    {
        *n = (int)(sizeof FOREST / sizeof FOREST[0]);
        return FOREST;
    }

    if (strcmp(name, "swamp") == 0)
    {
        *n = (int)(sizeof SWAMP / sizeof SWAMP[0]);
        return SWAMP;
    }

    if (strcmp(name, "shrub") == 0)
    {
        *n = (int)(sizeof SHRUB / sizeof SHRUB[0]);
        return SHRUB;
    }

    return NULL;
}