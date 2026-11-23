/* The small decoration features of 1.7.10 world population, ported from
 * oracle/src/world/gen/feature/ and checked against the oracle's feature probe
 * (oracle/harness/netherite/oracle/FeatureProbePlants.java is the Java half of this
 * table).
 *
 * One function per Java WorldGenerator, one table row per Config row in the
 * probe's per-feature table, in the same order and with the same block ids and
 * size parameters, so a config index means the same thing on both sides. The
 * row's `count` is the feature's second parameter: the disk radius for
 * feature_sand, the block count for feature_clay, and the metadata for the
 * features that carry one (flowers, tallgrass, double_plant).
 *
 * Java evaluates operands left to right and C does not, so every expression
 * that draws twice from one Random is split into statements.
 *
 * The features write with flag 2 through world_set_block. Only BlockPumpkin
 * overrides onBlockAdded among the blocks these place, and its body returns
 * immediately while config.yaml built_golems is off, so no callback is ported
 * here (see the report). */
#include "features.h"

#include <string.h>

#include "blocks.h"

/* Block ids, Block.getIdFromBlock on the blocks these features place and the
 * substrates they read. */
enum {
    PL_CLAY = 82, PL_YELLOW_FLOWER = 37, PL_RED_FLOWER = 38,
    PL_BROWN_MUSHROOM = 39, PL_RED_MUSHROOM = 40, PL_TALLGRASS = 31, PL_DEADBUSH = 32,
    PL_REEDS = 83, PL_CACTUS = 81, PL_PUMPKIN = 86, PL_WATERLILY = 111,
    PL_DOUBLE_PLANT = 175, PL_MELON = 103, PL_VINE = 106,
    PL_MYCELIUM = 110, PL_FARMLAND = 60,
};

/* Material identity by the generated name, so a new row in blocks.h cannot
 * shift an index out from under these. */

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

static int mat_name_is(int id, int mati)
{
    return BLOCKS[id & 4095].material == mati;
}

static int is_air_block(struct world *w, int x, int y, int z)
{
    return mat_name_is(world_get_block(w, x, y, z), MATI_AIR);
}

static int is_water(int id)
{
    return mat_name_is(id, MATI_WATER);
}

static int material_replaceable(int id)
{
    return MATERIALS[BLOCKS[id & 4095].material].replaceable;
}

static int material_solid(int id)
{
    return MATERIALS[BLOCKS[id & 4095].material].is_solid;
}

/* Chunk.getBlockLightValue with skylightSubtracted 0, i.e. World's
 * getFullBlockLightValue. */
static int full_block_light(struct world *w, int x, int y, int z)
{
    int sky = world_get_light(w, LIGHT_SKY, x, y, z);
    int block = world_get_light(w, LIGHT_BLOCK, x, y, z);
    return sky > block ? sky : block;
}

/* World.doesBlockHaveSolidTopSurface. The special block classes never appear
 * in raw terrain, but they are ported so the check is exact wherever it runs. */
static int solid_top_surface(struct world *w, int id, int x, int y, int z)
{
    int mat = BLOCKS[id & 4095].material;

    if (MATERIALS[mat].is_opaque && BLOCKS[id & 4095].normal_block) return 1;

    const char *cls = BLOCKS[id & 4095].class_name;

    if (strcmp(cls, "BlockStairs") == 0) return (world_get_meta(w, x, y, z) & 4) == 4;
    if (strcmp(cls, "BlockSlab") == 0) return (world_get_meta(w, x, y, z) & 8) == 8;
    if (strcmp(cls, "BlockHopper") == 0) return 1;
    if (strcmp(cls, "BlockSnow") == 0) return (world_get_meta(w, x, y, z) & 7) == 7;
    return 0;
}

/* Block.onBlockAdded is empty for every block these features place except
 * BlockPumpkin, whose golem build is switched off (config.yaml built_golems). */

/* BlockBush.func_149854_a: grass, dirt or farmland below. */
static int bush_soil(int below)
{
    return below == BLK_GRASS || below == BLK_DIRT || below == PL_FARMLAND;
}

/* BlockBush.canPlaceBlockAt. */
static int bush_can_place(struct world *w, int x, int y, int z)
{
    return material_replaceable(world_get_block(w, x, y, z)) && bush_soil(world_get_block(w, x, y - 1, z));
}

/* BlockFlower and BlockMushroom canBlockStay, on the block below id. */
static int flower_can_stay(struct world *w, int block, int x, int y, int z)
{
    int below = world_get_block(w, x, y - 1, z);

    if (block == PL_BROWN_MUSHROOM || block == PL_RED_MUSHROOM)
    {
        if (y < 0 || y >= 256) return 0;
        if (below == PL_MYCELIUM) return 1;
        if (below == BLK_DIRT && world_get_meta(w, x, y - 1, z) == 2) return 1;

        return full_block_light(w, x, y, z) < 13 && BLOCKS[below & 4095].opaque_cube;
    }

    return bush_soil(below);
}

/* WorldGenSand.generate: a disk of sand or gravel through the water at
 * (x, y, z), replacing dirt and grass within radius and y +/- 2. */
int feature_sand(struct world *w, jrand *r, int x, int y, int z, int block, int radius)
{
    if (!is_water(world_get_block(w, x, y, z))) return 0;

    int var6 = jr_int_n(r, radius - 2) + 2;
    int var7 = 2;

    for (int var8 = x - var6; var8 <= x + var6; ++var8)
    {
        for (int var9 = z - var6; var9 <= z + var6; ++var9)
        {
            int var10 = var8 - x;
            int var11 = var9 - z;

            if (var10 * var10 + var11 * var11 <= var6 * var6)
            {
                for (int var12 = y - var7; var12 <= y + var7; ++var12)
                {
                    int var13 = world_get_block(w, var8, var12, var9);

                    if (var13 == BLK_DIRT || var13 == BLK_GRASS)
                        world_set_block(w, var8, var12, var9, block, 0, 2);
                }
            }
        }
    }

    return 1;
}

/* WorldGenClay.generate: the same disk, one block thick, replacing dirt and
 * clay. */
int feature_clay(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    if (!is_water(world_get_block(w, x, y, z))) return 0;

    int var6 = jr_int_n(r, count - 2) + 2;
    int var7 = 1;

    for (int var8 = x - var6; var8 <= x + var6; ++var8)
    {
        for (int var9 = z - var6; var9 <= z + var6; ++var9)
        {
            int var10 = var8 - x;
            int var11 = var9 - z;

            if (var10 * var10 + var11 * var11 <= var6 * var6)
            {
                for (int var12 = y - var7; var12 <= y + var7; ++var12)
                {
                    int var13 = world_get_block(w, var8, var12, var9);

                    if (var13 == BLK_DIRT)
                        world_set_block(w, var8, var12, var9, block, 0, 2);
                }
            }
        }
    }

    return 1;
}

/* WorldGenFlowers.generate: 64 tries in a 15x7x15 box around (x, y, z). The
 * overworld has a sky, so vanilla's (!hasNoSky || y < 255) is always true. */
int feature_flowers(struct world *w, jrand *r, int x, int y, int z, int block, int meta)
{
    for (int var6 = 0; var6 < 64; ++var6)
    {
        int a = jr_int_n(r, 8), b = jr_int_n(r, 8);
        int var7 = x + a - b;
        int c = jr_int_n(r, 4), d = jr_int_n(r, 4);
        int var8 = y + c - d;
        int e = jr_int_n(r, 8), f = jr_int_n(r, 8);
        int var9 = z + e - f;

        if (is_air_block(w, var7, var8, var9) && flower_can_stay(w, block, var7, var8, var9))
            world_set_block(w, var7, var8, var9, block, meta, 2);
    }

    return 1;
}

/* WorldGenTallGrass.generate: first walk down to the first block that is not
 * air or leaves, then 128 tries around the spot. */
int feature_tallgrass(struct world *w, jrand *r, int x, int y, int z, int block, int meta)
{
    for (;;)
    {
        int id = world_get_block(w, x, y, z);

        if (!(mat_name_is(id, MATI_AIR) || mat_name_is(id, MATI_LEAVES)) || y <= 0) break;

        --y;
    }

    for (int var7 = 0; var7 < 128; ++var7)
    {
        int a = jr_int_n(r, 8), b = jr_int_n(r, 8);
        int var8 = x + a - b;
        int c = jr_int_n(r, 4), d = jr_int_n(r, 4);
        int var9 = y + c - d;
        int e = jr_int_n(r, 8), f = jr_int_n(r, 8);
        int var10 = z + e - f;

        if (is_air_block(w, var8, var9, var10) && bush_soil(world_get_block(w, var8, var9 - 1, var10)))
            world_set_block(w, var8, var9, var10, block, meta, 2);
    }

    return 1;
}

/* BlockDeadBush.func_149854_a. */
static int deadbush_soil(int below)
{
    return below == BLK_SAND || below == BLK_HARDENED_CLAY || below == BLK_STAINED_CLAY || below == BLK_DIRT;
}

/* WorldGenDeadBush.generate: the same walk down, then 4 tries. */
int feature_deadbush(struct world *w, jrand *r, int x, int y, int z, int block, int meta)
{
    for (;;)
    {
        int id = world_get_block(w, x, y, z);

        if (!(mat_name_is(id, MATI_AIR) || mat_name_is(id, MATI_LEAVES)) || y <= 0) break;

        --y;
    }

    for (int var7 = 0; var7 < 4; ++var7)
    {
        int a = jr_int_n(r, 8), b = jr_int_n(r, 8);
        int var8 = x + a - b;
        int c = jr_int_n(r, 4), d = jr_int_n(r, 4);
        int var9 = y + c - d;
        int e = jr_int_n(r, 8), f = jr_int_n(r, 8);
        int var10 = z + e - f;

        if (is_air_block(w, var8, var9, var10) && deadbush_soil(world_get_block(w, var8, var9 - 1, var10)))
            world_set_block(w, var8, var9, var10, block, meta, 2);
    }

    return 1;
}

/* BlockReed.canBlockStay (BlockReed.canPlaceBlockAt). */
static int reed_can_stay(struct world *w, int x, int y, int z)
{
    int below = world_get_block(w, x, y - 1, z);

    if (below == PL_REEDS) return 1;
    if (below != BLK_GRASS && below != BLK_DIRT && below != BLK_SAND) return 0;

    return is_water(world_get_block(w, x - 1, y - 1, z)) || is_water(world_get_block(w, x + 1, y - 1, z)) ||
           is_water(world_get_block(w, x, y - 1, z - 1)) || is_water(world_get_block(w, x, y - 1, z + 1));
}

/* WorldGenReed.generate: 20 tries, a stalk of 2 to 4 reeds at the given y. */
int feature_reed(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    for (int var6 = 0; var6 < 20; ++var6)
    {
        int a = jr_int_n(r, 4), b = jr_int_n(r, 4);
        int var7 = x + a - b;
        int var8 = y;
        int c = jr_int_n(r, 4), d = jr_int_n(r, 4);
        int var9 = z + c - d;

        if (is_air_block(w, var7, y, var9) &&
            (is_water(world_get_block(w, var7 - 1, y - 1, var9)) || is_water(world_get_block(w, var7 + 1, y - 1, var9)) ||
             is_water(world_get_block(w, var7, y - 1, var9 - 1)) || is_water(world_get_block(w, var7, y - 1, var9 + 1))))
        {
            int e = jr_int_n(r, 3);
            int var10 = 2 + jr_int_n(r, e + 1);

            for (int var11 = 0; var11 < var10; ++var11)
            {
                if (reed_can_stay(w, var7, var8 + var11, var9))
                    world_set_block(w, var7, var8 + var11, var9, block, 0, 2);
            }
        }
    }

    return 1;
}

/* BlockCactus.canBlockStay. */
static int cactus_can_stay(struct world *w, int x, int y, int z)
{
    if (material_solid(world_get_block(w, x - 1, y, z))) return 0;
    if (material_solid(world_get_block(w, x + 1, y, z))) return 0;
    if (material_solid(world_get_block(w, x, y, z - 1))) return 0;
    if (material_solid(world_get_block(w, x, y, z + 1))) return 0;

    int below = world_get_block(w, x, y - 1, z);

    return below == PL_CACTUS || below == BLK_SAND;
}

/* WorldGenCactus.generate: 10 tries, a column of 1 to 3 cacti. */
int feature_cactus(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    for (int var6 = 0; var6 < 10; ++var6)
    {
        int a = jr_int_n(r, 8), b = jr_int_n(r, 8);
        int var7 = x + a - b;
        int c = jr_int_n(r, 4), d = jr_int_n(r, 4);
        int var8 = y + c - d;
        int e = jr_int_n(r, 8), f = jr_int_n(r, 8);
        int var9 = z + e - f;

        if (is_air_block(w, var7, var8, var9))
        {
            int g = jr_int_n(r, 3);
            int var10 = 1 + jr_int_n(r, g + 1);

            for (int var11 = 0; var11 < var10; ++var11)
            {
                if (cactus_can_stay(w, var7, var8 + var11, var9))
                    world_set_block(w, var7, var8 + var11, var9, block, 0, 2);
            }
        }
    }

    return 1;
}

/* BlockPumpkin.canPlaceBlockAt. */
static int pumpkin_can_place(struct world *w, int x, int y, int z)
{
    return material_replaceable(world_get_block(w, x, y, z)) &&
           solid_top_surface(w, world_get_block(w, x, y - 1, z), x, y - 1, z);
}

/* WorldGenPumpkin.generate: 64 tries, on grass, with a random facing. */
int feature_pumpkin(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    for (int var6 = 0; var6 < 64; ++var6)
    {
        int a = jr_int_n(r, 8), b = jr_int_n(r, 8);
        int var7 = x + a - b;
        int c = jr_int_n(r, 4), d = jr_int_n(r, 4);
        int var8 = y + c - d;
        int e = jr_int_n(r, 8), f = jr_int_n(r, 8);
        int var9 = z + e - f;

        if (is_air_block(w, var7, var8, var9) && world_get_block(w, var7, var8 - 1, var9) == BLK_GRASS &&
            pumpkin_can_place(w, var7, var8, var9))
        {
            int meta = jr_int_n(r, 4);
            world_set_block(w, var7, var8, var9, block, meta, 2);
        }
    }

    return 1;
}

/* BlockLilyPad.canPlaceBlockAt: a replaceable cell over water. */
static int lily_can_place(struct world *w, int x, int y, int z)
{
    return material_replaceable(world_get_block(w, x, y, z)) && world_get_block(w, x, y - 1, z) == BLK_WATER;
}

/* WorldGenWaterlily.generate: 10 tries. */
int feature_waterlily(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    for (int var6 = 0; var6 < 10; ++var6)
    {
        int a = jr_int_n(r, 8), b = jr_int_n(r, 8);
        int var7 = x + a - b;
        int c = jr_int_n(r, 4), d = jr_int_n(r, 4);
        int var8 = y + c - d;
        int e = jr_int_n(r, 8), f = jr_int_n(r, 8);
        int var9 = z + e - f;

        if (is_air_block(w, var7, var8, var9) && lily_can_place(w, var7, var8, var9))
            world_set_block(w, var7, var8, var9, block, 0, 2);
    }

    return 1;
}

/* BlockDoublePlant.canPlaceBlockAt: the bush rule plus air above. The
 * overworld has a sky, so vanilla's (!hasNoSky || y < 254) is always true. */
static int double_plant_can_place(struct world *w, int x, int y, int z)
{
    return bush_can_place(w, x, y, z) && is_air_block(w, x, y + 1, z);
}

/* WorldGenDoublePlant.generate: 64 tries; func_149889_c writes the lower half
 * with the variant metadata and the upper half with bit 8 set. */
int feature_double_plant(struct world *w, jrand *r, int x, int y, int z, int block, int meta)
{
    int var6 = 0;

    for (int var7 = 0; var7 < 64; ++var7)
    {
        int a = jr_int_n(r, 8), b = jr_int_n(r, 8);
        int var8 = x + a - b;
        int c = jr_int_n(r, 4), d = jr_int_n(r, 4);
        int var9 = y + c - d;
        int e = jr_int_n(r, 8), f = jr_int_n(r, 8);
        int var10 = z + e - f;

        if (is_air_block(w, var8, var9, var10) && double_plant_can_place(w, var8, var9, var10))
        {
            world_set_block(w, var8, var9, var10, block, meta, 2);
            world_set_block(w, var8, var9 + 1, var10, block, 8, 2);
            var6 = 1;
        }
    }

    return var6;
}

/* WorldGenMelon.generate: 64 tries on grass. BlockMelon has no
 * canPlaceBlockAt, so the default (a replaceable cell) applies. */
int feature_melon(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    for (int var6 = 0; var6 < 64; ++var6)
    {
        int a = jr_int_n(r, 8), b = jr_int_n(r, 8);
        int var7 = x + a - b;
        int c = jr_int_n(r, 4), d = jr_int_n(r, 4);
        int var8 = y + c - d;
        int e = jr_int_n(r, 8), f = jr_int_n(r, 8);
        int var9 = z + e - f;

        if (material_replaceable(world_get_block(w, var7, var8, var9)) && world_get_block(w, var7, var8 - 1, var9) == BLK_GRASS)
            world_set_block(w, var7, var8, var9, block, 0, 2);
    }

    return 1;
}

/* BlockVine.func_150093_a. */
static int vine_neighbor(int id)
{
    return BLOCKS[id & 4095].normal_block && MATERIALS[BLOCKS[id & 4095].material].blocks_movement;
}

/* BlockVine.canPlaceBlockOnSide, sides 2..5 only (the lengths WorldGenVines
 * asks about). The side offsets are the switch's, not Facing's tables. */
static int vine_can_place_on_side(struct world *w, int x, int y, int z, int side)
{
    switch (side)
    {
        case 2: return vine_neighbor(world_get_block(w, x, y, z + 1));
        case 3: return vine_neighbor(world_get_block(w, x, y, z - 1));
        case 4: return vine_neighbor(world_get_block(w, x + 1, y, z));
        case 5: return vine_neighbor(world_get_block(w, x - 1, y, z));
        default: return 0;
    }
}

/* 1 << Direction.facingToDirection[Facing.oppositeSide[side]], sides 2..5. */
static const int VINE_META[6] = {-1, -1, 1, 4, 8, 2};

/* WorldGenVines.generate: climb one column from y to 127, placing a vine on
 * whichever side has a normal block; on a non-air cell, wander by up to 3 in x
 * and z from the original column. */
int feature_vines(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    int var6 = x;
    int var7 = z;

    for (; y < 128; ++y)
    {
        if (is_air_block(w, x, y, z))
        {
            for (int var8 = 2; var8 <= 5; ++var8)
            {
                if (vine_can_place_on_side(w, x, y, z, var8))
                {
                    world_set_block(w, x, y, z, block, VINE_META[var8], 2);
                    break;
                }
            }
        }
        else
        {
            int a = jr_int_n(r, 4), b = jr_int_n(r, 4);
            x = var6 + a - b;
            int c = jr_int_n(r, 4), d = jr_int_n(r, 4);
            z = var7 + c - d;
        }
    }

    return 1;
}

/* The probe's per-feature table: one row per config, in config order, so a
 * later lane appends its own rows after these and existing probes keep their
 * indices. y0 and y1 are the band a band-placed row draws from; the surface
 * rows leave them 0. */
static const struct feature SAND[] = {
    {"sand", 0, 0, 0, 7, BLK_SAND, 7, feature_sand},
    {"sand", 1, 0, 0, 7, BLK_GRAVEL, 6, feature_sand},
};

static const struct feature CLAY[] = {
    {"clay", 0, 0, 0, 4, PL_CLAY, 4, feature_clay},
};

static const struct feature FLOWERS[] = {
    {"flowers", 0, 0, 0, 8, PL_YELLOW_FLOWER, 0, feature_flowers},
    {"flowers", 1, 0, 0, 8, PL_RED_FLOWER, 0, feature_flowers},
    {"flowers", 2, 0, 0, 8, PL_RED_FLOWER, 1, feature_flowers},
    {"flowers", 3, 0, 0, 8, PL_BROWN_MUSHROOM, 0, feature_flowers},
    {"flowers", 4, 0, 0, 8, PL_RED_MUSHROOM, 0, feature_flowers},
};

static const struct feature TALLGRASS[] = {
    {"tallgrass", 0, 0, 0, 8, PL_TALLGRASS, 1, feature_tallgrass},
    {"tallgrass", 1, 0, 0, 8, PL_TALLGRASS, 2, feature_tallgrass},
};

static const struct feature DEADBUSH[] = {
    {"deadbush", 0, 0, 0, 8, PL_DEADBUSH, 0, feature_deadbush},
};

static const struct feature REED[] = {
    {"reed", 0, 0, 0, 4, PL_REEDS, 0, feature_reed},
};

static const struct feature CACTUS[] = {
    {"cactus", 0, 0, 0, 8, PL_CACTUS, 0, feature_cactus},
};

static const struct feature PUMPKIN[] = {
    {"pumpkin", 0, 0, 0, 8, PL_PUMPKIN, 0, feature_pumpkin},
};

static const struct feature WATERLILY[] = {
    {"waterlily", 0, 0, 0, 8, PL_WATERLILY, 0, feature_waterlily},
};

static const struct feature DOUBLE_PLANT[] = {
    {"doubleplant", 0, 0, 0, 8, PL_DOUBLE_PLANT, 0, feature_double_plant},
    {"doubleplant", 1, 0, 0, 8, PL_DOUBLE_PLANT, 1, feature_double_plant},
    {"doubleplant", 2, 0, 0, 8, PL_DOUBLE_PLANT, 2, feature_double_plant},
    {"doubleplant", 3, 0, 0, 8, PL_DOUBLE_PLANT, 3, feature_double_plant},
    {"doubleplant", 4, 0, 0, 8, PL_DOUBLE_PLANT, 4, feature_double_plant},
    {"doubleplant", 5, 0, 0, 8, PL_DOUBLE_PLANT, 5, feature_double_plant},
};

static const struct feature MELON[] = {
    {"melon", 0, 0, 0, 8, PL_MELON, 0, feature_melon},
};

static const struct feature VINES[] = {
    {"vines", 0, 4, 124, 4, PL_VINE, 0, feature_vines},
};

/* The rows for one of this lane's feature names, or NULL. */
const struct feature *features_plants_for(const char *name, int *n)
{
    struct pair { const char *name; const struct feature *rows; int n; };
    static const struct pair TABLES[] = {
        {"sand", SAND, (int)(sizeof SAND / sizeof SAND[0])},
        {"clay", CLAY, (int)(sizeof CLAY / sizeof CLAY[0])},
        {"flowers", FLOWERS, (int)(sizeof FLOWERS / sizeof FLOWERS[0])},
        {"tallgrass", TALLGRASS, (int)(sizeof TALLGRASS / sizeof TALLGRASS[0])},
        {"deadbush", DEADBUSH, (int)(sizeof DEADBUSH / sizeof DEADBUSH[0])},
        {"reed", REED, (int)(sizeof REED / sizeof REED[0])},
        {"cactus", CACTUS, (int)(sizeof CACTUS / sizeof CACTUS[0])},
        {"pumpkin", PUMPKIN, (int)(sizeof PUMPKIN / sizeof PUMPKIN[0])},
        {"waterlily", WATERLILY, (int)(sizeof WATERLILY / sizeof WATERLILY[0])},
        {"doubleplant", DOUBLE_PLANT, (int)(sizeof DOUBLE_PLANT / sizeof DOUBLE_PLANT[0])},
        {"melon", MELON, (int)(sizeof MELON / sizeof MELON[0])},
        {"vines", VINES, (int)(sizeof VINES / sizeof VINES[0])},
    };

    if (name == NULL) return NULL;

    for (size_t i = 0; i < sizeof TABLES / sizeof TABLES[0]; ++i)
    {
        if (strcmp(name, TABLES[i].name) == 0)
        {
            *n = TABLES[i].n;
            return TABLES[i].rows;
        }
    }

    return NULL;
}