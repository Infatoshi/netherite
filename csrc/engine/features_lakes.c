/* The lakes lane's population features, mirroring the rows this lane appended
 * to oracle/harness/netherite/oracle/FeatureProbe.java's FEATURES table: one
 * function per Java feature, one table row per config, same block ids and same
 * parameters, so the config indices agree with the probe.
 *
 * The features are WorldGenLakes (water and lava), WorldGenBigMushroom (both
 * colours and the colour-drawing object BiomeDecorator builds),
 * WorldGenDesertWells, WorldGenIceSpike and WorldGenIcePath, all of 1.7.10.
 * They read the world's light (the lake's grass restore, and the freeze test)
 * and its biome (the grass restore's mycelium branch and the freeze test's
 * temperature), and lava placed next to water runs BlockLiquid.onBlockAdded
 * inside world_set_block, exactly where Chunk.func_150807_a calls it.
 *
 * Java evaluates operands left to right and C does not: every expression that
 * draws from the feature's Random more than once is a separate statement here,
 * and every float expression keeps Java's float width under the
 * -ffp-contract=off build. Short-circuit reads are kept literal, because a
 * draw inside a && or || happens only when Java would make it. */
#include "features_lakes.h"

#include <string.h>

#include "biomes.h"
#include "blocks.h"
#include "jmath.h"

/* Block ids, Blocks.registerBlock. Blocks.water is the static one (id 9) and
 * Blocks.flowing_water the flowing one (8); Blocks.lava is 11 and
 * Blocks.flowing_lava 10. */
enum {
    LK_AIR = 0, LK_STONE = 1, LK_GRASS = 2, LK_DIRT = 3,
    LK_FLOWING_WATER = 8, LK_WATER = 9, LK_LAVA = 11,
    LK_SAND = 12, LK_SANDSTONE = 24, LK_STONE_SLAB = 44, LK_MYCELIUM = 110,
    LK_ICE = 79, LK_SNOW = 80, LK_PACKED_ICE = 174,
    LK_BROWN_MUSHROOM_BLOCK = 99, LK_RED_MUSHROOM_BLOCK = 100,
};

/* Material.getMaterial(name) over blocks.h's generated MATERIALS table: the
 * feature checks name a material, and looking the row up by name keeps a
 * regenerated table from silently moving it. */
static int mat_index(const char *name)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
        if (strcmp(MATERIALS[i].name, name) == 0) return (int)i;

    return -1;
}

/* The material rows the features compare against, resolved once from the
 * generated table's names. */
static int MAT_AIR = -1, MAT_LEAVES = -1, MAT_LAVA = -1, MAT_WATER = -1;

/* before main: the same for every environment */
__attribute__((constructor)) static void mat_init(void)
{
    MAT_AIR = mat_index("air");
    MAT_LEAVES = mat_index("leaves");
    MAT_LAVA = mat_index("lava");
    MAT_WATER = mat_index("water");
}

/* Block.getMaterial(): the material row of a block id. */
static int mat_of(int id)
{
    return BLOCKS[id & 4095].material;
}

/* Material.isSolid() / isLiquid(). */
static int mat_solid(int id)
{
    return MATERIALS[mat_of(id)].is_solid;
}

static int mat_liquid(int id)
{
    return MATERIALS[mat_of(id)].is_liquid;
}

/* World.isAirBlock: Material.air. */
static int air_at(struct world *w, int x, int y, int z)
{
    return mat_of(world_get_block(w, x, y, z)) == MAT_AIR;
}

/* MathHelper.ceiling_float_int. */
static int ceiling_float_int(float f)
{
    int i = j_f2i(f);
    return f > (float)i ? i + 1 : i;
}

/* MathHelper.abs_int. */
static int abs_int(int v)
{
    return v >= 0 ? v : -v;
}

/* World.getBiomeGenForCoords for a position whose chunk is loaded: the biome
 * the chunk was provided with (Chunk.getBiomeGenForWorldCoords reads
 * blockBiomeArray, which ChunkProviderGenerate.provideChunk fills from
 * WorldChunkManager.loadBlockGeneratorData, the voronoi layer). */
int biome_at(struct world *w, int x, int z)
{
    return world_get_biome(w, x, z);
}

/* BiomeGenBase.getFloatTemperature, which only reads the biome and the fixed
 * perlin noise field_150605_ac (new NoiseGeneratorPerlin(new Random(1234L), 1))
 * above y 64. */
static struct perlin TEMP_NOISE;

/* before main: the same for every environment */
__attribute__((constructor)) static void temp_noise_init(void)
{
    jrand r;

    jr_seed(&r, 1234L);
    perlin_init(&TEMP_NOISE, &r, 1);
}

float biome_temperature(int biome, int x, int y, int z)
{
    float t = BIOMES[biome].temperature;

    if (y > 64)
    {
        float n = (float)perlin_point(&TEMP_NOISE, (double)x * 1.0 / 8.0, (double)z * 1.0 / 8.0) * 4.0F;

        return t - (n + (float)y - 64.0F) * 0.05F / 30.0F;
    }

    return t;
}

/* World.isBlockFreezable -> canBlockFreeze(x, y, z, false). */
static int block_freezable(struct world *w, int x, int y, int z)
{
    int biome = biome_at(w, x, z);
    float t = biome_temperature(biome, x, y, z);

    if (t > 0.15F) return 0;

    if (y >= 0 && y < 256 && world_get_light(w, LIGHT_BLOCK, x, y, z) < 10)
    {
        int id = world_get_block(w, x, y, z);

        if ((id == LK_WATER || id == LK_FLOWING_WATER) && world_get_meta(w, x, y, z) == 0) return 1;
    }

    return 0;
}

/* WorldGenLakes.generate. `block` is field_150556_a: Blocks.water (9) or
 * Blocks.lava (11). */
static int feature_lake(struct world *w, jrand *r, int x, int y, int z, int block, int unused)
{
    (void)unused;
    x -= 8;
    z -= 8;

    while (y > 5 && air_at(w, x, y, z)) --y;

    if (y <= 4) return 0;

    y -= 4;

    unsigned char mask[2048];
    memset(mask, 0, sizeof mask);

    int shapes = jr_int_n(r, 4) + 4;

    /* the lake's blobs: three radii and three centres, six draws each */
    for (int i = 0; i < shapes; ++i)
    {
        double var9 = jr_double(r) * 6.0 + 3.0;
        double var11 = jr_double(r) * 4.0 + 2.0;
        double var13 = jr_double(r) * 6.0 + 3.0;
        double var15 = jr_double(r) * (16.0 - var9 - 2.0) + 1.0 + var9 / 2.0;
        double var17 = jr_double(r) * (8.0 - var11 - 4.0) + 2.0 + var11 / 2.0;
        double var19 = jr_double(r) * (16.0 - var13 - 2.0) + 1.0 + var13 / 2.0;

        for (int var21 = 1; var21 < 15; ++var21)
        {
            for (int var22 = 1; var22 < 15; ++var22)
            {
                for (int var23 = 1; var23 < 7; ++var23)
                {
                    double var24 = ((double)var21 - var15) / (var9 / 2.0);
                    double var26 = ((double)var23 - var17) / (var11 / 2.0);
                    double var28 = ((double)var22 - var19) / (var13 / 2.0);

                    if (var24 * var24 + var26 * var26 + var28 * var28 < 1.0)
                        mask[(var21 * 16 + var22) * 8 + var23] = 1;
                }
            }
        }
    }

    /* the rim: a cell next to a lake cell, in any of the six directions */
#define LK_RIM(i, j, k) (!mask[((i) * 16 + (j)) * 8 + (k)] &&                                         \
                         (((i) < 15 && mask[(((i) + 1) * 16 + (j)) * 8 + (k)]) ||                     \
                          ((i) > 0 && mask[(((i) - 1) * 16 + (j)) * 8 + (k)]) ||                      \
                          ((j) < 15 && mask[((i) * 16 + (j) + 1) * 8 + (k)]) ||                       \
                          ((j) > 0 && mask[((i) * 16 + ((j) - 1)) * 8 + (k)]) ||                      \
                          ((k) < 7 && mask[((i) * 16 + (j)) * 8 + (k) + 1]) ||                        \
                          ((k) > 0 && mask[((i) * 16 + (j)) * 8 + ((k) - 1)])))

    /* the rim must be solid below the water line and not liquid above it */
    for (int var8 = 0; var8 < 16; ++var8)
    {
        for (int var32 = 0; var32 < 16; ++var32)
        {
            for (int var10 = 0; var10 < 8; ++var10)
            {
                if (LK_RIM(var8, var32, var10))
                {
                    int var12 = world_get_block(w, x + var8, y + var10, z + var32);

                    if (var10 >= 4 && mat_liquid(var12)) return 0;

                    if (var10 < 4 && !mat_solid(var12) && var12 != block) return 0;
                }
            }
        }
    }

    /* fill: the lake block below the water line, air above it */
    for (int var8 = 0; var8 < 16; ++var8)
    {
        for (int var32 = 0; var32 < 16; ++var32)
        {
            for (int var10 = 0; var10 < 8; ++var10)
            {
                if (mask[(var8 * 16 + var32) * 8 + var10])
                    world_set_block(w, x + var8, y + var10, z + var32, var10 >= 4 ? LK_AIR : block, 0, 2);
            }
        }
    }

    /* the floor the water exposed: dirt under a lake cell that still sees sky
     * becomes grass, or mycelium under a mushroom island */
    for (int var8 = 0; var8 < 16; ++var8)
    {
        for (int var32 = 0; var32 < 16; ++var32)
        {
            for (int var10 = 4; var10 < 8; ++var10)
            {
                if (mask[(var8 * 16 + var32) * 8 + var10] &&
                    world_get_block(w, x + var8, y + var10 - 1, z + var32) == LK_DIRT &&
                    world_get_light(w, LIGHT_SKY, x + var8, y + var10, z + var32) > 0)
                {
                    int biome = biome_at(w, x + var8, z + var32);
                    int top = BIOMES[biome].top;

                    world_set_block(w, x + var8, y + var10 - 1, z + var32, top == LK_MYCELIUM ? LK_MYCELIUM : LK_GRASS, 0, 2);
                }
            }
        }
    }

    /* a lava lake lines its rim with stone, drawing once per rim cell at or
     * above the water line (Java's var10 < 4 short-circuits the draw) */
    if (mat_of(block) == MAT_LAVA)
    {
        for (int var8 = 0; var8 < 16; ++var8)
        {
            for (int var32 = 0; var32 < 16; ++var32)
            {
                for (int var10 = 0; var10 < 8; ++var10)
                {
                    if (LK_RIM(var8, var32, var10) && (var10 < 4 || jr_int_n(r, 2) != 0) &&
                        mat_solid(world_get_block(w, x + var8, y + var10, z + var32)))
                        world_set_block(w, x + var8, y + var10, z + var32, LK_STONE, 0, 2);
                }
            }
        }
    }

    /* a water lake freezes its surface where the biome is cold enough */
    if (mat_of(block) == MAT_WATER)
    {
        for (int var8 = 0; var8 < 16; ++var8)
        {
            for (int var32 = 0; var32 < 16; ++var32)
            {
                if (block_freezable(w, x + var8, y + 4, z + var32)) world_set_block(w, x + var8, y + 4, z + var32, LK_ICE, 0, 2);
            }
        }
    }

#undef LK_RIM

    return 1;
}

/* WorldGenBigMushroom.generate. `type` is the mushroomType field: 0 brown, 1
 * red, -1 draw the colour. The Java constructor also decides the flag its cap
 * writes carry (super(true) for the typed one, super(false) for the
 * no-argument one BiomeDecorator builds), but the flag only reaches
 * World.setBlock's neighbour notification, which nothing here observes. */
static int feature_bigmushroom(struct world *w, jrand *r, int x, int y, int z, int type, int unused)
{
    (void)unused;

    int var6 = jr_int_n(r, 2);

    if (type >= 0) var6 = type;

    int var7 = jr_int_n(r, 3) + 4;
    int var8 = 1;

    if (!(y >= 1 && y + var7 + 1 < 256)) return 0;

    /* the room check: air or leaves over the footprint */
    for (int var9 = y; var9 <= y + 1 + var7; ++var9)
    {
        int var10 = 3;

        if (var9 <= y + 3) var10 = 0;

        for (int var11 = x - var10; var11 <= x + var10 && var8; ++var11)
        {
            for (int var12 = z - var10; var12 <= z + var10 && var8; ++var12)
            {
                if (var9 >= 0 && var9 < 256)
                {
                    int var13 = world_get_block(w, var11, var9, var12);
                    int m = mat_of(var13);

                    if (m != MAT_AIR && m != MAT_LEAVES) var8 = 0;
                }
                else
                {
                    var8 = 0;
                }
            }
        }
    }

    if (!var8) return 0;

    int var16 = world_get_block(w, x, y - 1, z);

    if (var16 != LK_DIRT && var16 != LK_GRASS && var16 != LK_MYCELIUM) return 0;

    int var17 = y + var7;

    if (var6 == 1) var17 = y + var7 - 3;

    int cap = var6 == 0 ? LK_BROWN_MUSHROOM_BLOCK : LK_RED_MUSHROOM_BLOCK;

    for (int var11 = var17; var11 <= y + var7; ++var11)
    {
        int var12 = 1;

        if (var11 < y + var7) ++var12;
        if (var6 == 0) var12 = 3;

        for (int var19 = x - var12; var19 <= x + var12; ++var19)
        {
            for (int var14 = z - var12; var14 <= z + var12; ++var14)
            {
                int var15 = 5;

                if (var19 == x - var12) --var15;
                if (var19 == x + var12) ++var15;
                if (var14 == z - var12) var15 -= 3;
                if (var14 == z + var12) var15 += 3;

                if (var6 == 0 || var11 < y + var7)
                {
                    if ((var19 == x - var12 || var19 == x + var12) && (var14 == z - var12 || var14 == z + var12)) continue;

                    if (var19 == x - (var12 - 1) && var14 == z - var12) var15 = 1;
                    if (var19 == x - var12 && var14 == z - (var12 - 1)) var15 = 1;
                    if (var19 == x + (var12 - 1) && var14 == z - var12) var15 = 3;
                    if (var19 == x + var12 && var14 == z - (var12 - 1)) var15 = 3;
                    if (var19 == x - (var12 - 1) && var14 == z + var12) var15 = 7;
                    if (var19 == x - var12 && var14 == z + (var12 - 1)) var15 = 7;
                    if (var19 == x + (var12 - 1) && var14 == z + var12) var15 = 9;
                    if (var19 == x + var12 && var14 == z + (var12 - 1)) var15 = 9;
                }

                if (var15 == 5 && var11 < y + var7) var15 = 0;

                if ((var15 != 0 || y >= y + var7 - 1) && !BLOCKS[world_get_block(w, var19, var11, var14) & 4095].opaque_cube)
                    world_set_block(w, var19, var11, var14, cap, var15, 2);
            }
        }
    }

    /* the stem */
    for (int var11 = 0; var11 < var7; ++var11)
    {
        if (!BLOCKS[world_get_block(w, x, y + var11, z) & 4095].opaque_cube)
            world_set_block(w, x, y + var11, z, cap, 10, 2);
    }

    return 1;
}

/* WorldGenDesertWells.generate. BiomeGenDesert.decorate hands it
 * getHeightValue(x, z) + 1. */
static int feature_desertwell(struct world *w, jrand *r, int x, int y, int z, int unused, int unused2)
{
    (void)r;
    (void)unused;
    (void)unused2;

    while (air_at(w, x, y, z) && y > 2) --y;

    if (world_get_block(w, x, y, z) != LK_SAND) return 0;

    for (int var6 = -2; var6 <= 2; ++var6)
        for (int var7 = -2; var7 <= 2; ++var7)
            if (air_at(w, x + var6, y - 1, z + var7) && air_at(w, x + var6, y - 2, z + var7)) return 0;

    for (int var6 = -1; var6 <= 0; ++var6)
        for (int var7 = -2; var7 <= 2; ++var7)
            for (int var8 = -2; var8 <= 2; ++var8)
                world_set_block(w, x + var7, y + var6, z + var8, LK_SANDSTONE, 0, 2);

    world_set_block(w, x, y, z, LK_FLOWING_WATER, 0, 2);
    world_set_block(w, x - 1, y, z, LK_FLOWING_WATER, 0, 2);
    world_set_block(w, x + 1, y, z, LK_FLOWING_WATER, 0, 2);
    world_set_block(w, x, y, z - 1, LK_FLOWING_WATER, 0, 2);
    world_set_block(w, x, y, z + 1, LK_FLOWING_WATER, 0, 2);

    for (int var6 = -2; var6 <= 2; ++var6)
        for (int var7 = -2; var7 <= 2; ++var7)
            if (var6 == -2 || var6 == 2 || var7 == -2 || var7 == 2)
                world_set_block(w, x + var6, y + 1, z + var7, LK_SANDSTONE, 0, 2);

    world_set_block(w, x + 2, y + 1, z, LK_STONE_SLAB, 1, 2);
    world_set_block(w, x - 2, y + 1, z, LK_STONE_SLAB, 1, 2);
    world_set_block(w, x, y + 1, z + 2, LK_STONE_SLAB, 1, 2);
    world_set_block(w, x, y + 1, z - 2, LK_STONE_SLAB, 1, 2);

    for (int var6 = -1; var6 <= 1; ++var6)
    {
        for (int var7 = -1; var7 <= 1; ++var7)
        {
            if (var6 == 0 && var7 == 0) world_set_block(w, x + var6, y + 4, z + var7, LK_SANDSTONE, 0, 2);
            else world_set_block(w, x + var6, y + 4, z + var7, LK_STONE_SLAB, 1, 2);
        }
    }

    for (int var6 = 1; var6 <= 3; ++var6)
    {
        world_set_block(w, x - 1, y + var6, z - 1, LK_SANDSTONE, 0, 2);
        world_set_block(w, x - 1, y + var6, z + 1, LK_SANDSTONE, 0, 2);
        world_set_block(w, x + 1, y + var6, z - 1, LK_SANDSTONE, 0, 2);
        world_set_block(w, x + 1, y + var6, z + 1, LK_SANDSTONE, 0, 2);
    }

    return 1;
}

/* WorldGenIceSpike.generate. BiomeGenSnow.decorate hands it
 * getHeightValue(x, z), and it only builds where that block is snow (biomes
 * with topBlock snow, i.e. Ice Plains Spikes). */
static int feature_icespike(struct world *w, jrand *r, int x, int y, int z, int unused, int unused2)
{
    (void)unused;
    (void)unused2;

    while (air_at(w, x, y, z) && y > 2) --y;

    if (world_get_block(w, x, y, z) != LK_SNOW) return 0;

    y += jr_int_n(r, 4);

    int var6 = jr_int_n(r, 4) + 7;
    int var7 = var6 / 4 + jr_int_n(r, 2);

    if (var7 > 1 && jr_int_n(r, 60) == 0) y += 10 + jr_int_n(r, 30);

    for (int var8 = 0; var8 < var6; ++var8)
    {
        float var9 = (1.0F - (float)var8 / (float)var6) * (float)var7;
        int var10 = ceiling_float_int(var9);

        for (int var11 = -var10; var11 <= var10; ++var11)
        {
            float var12 = (float)abs_int(var11) - 0.25F;

            for (int var13 = -var10; var13 <= var10; ++var13)
            {
                float var14 = (float)abs_int(var13) - 0.25F;

                /* Java's || and && short-circuit, so the nextFloat draw happens
                 * only when the cell is inside the circle and sits on the rim */
                if (((var11 == 0 && var13 == 0) || var12 * var12 + var14 * var14 <= var9 * var9) &&
                    ((var11 != -var10 && var11 != var10 && var13 != -var10 && var13 != var10) || jr_float(r) <= 0.75F))
                {
                    int var15 = world_get_block(w, x + var11, y + var8, z + var13);
                    int m = mat_of(var15);

                    if (m == MAT_AIR || var15 == LK_DIRT || var15 == LK_SNOW || var15 == LK_ICE)
                        world_set_block(w, x + var11, y + var8, z + var13, LK_PACKED_ICE, 0, 2);

                    if (var8 != 0 && var10 > 1)
                    {
                        int var18 = world_get_block(w, x + var11, y - var8, z + var13);
                        int m2 = mat_of(var18);

                        if (m2 == MAT_AIR || var18 == LK_DIRT || var18 == LK_SNOW || var18 == LK_ICE)
                            world_set_block(w, x + var11, y - var8, z + var13, LK_PACKED_ICE, 0, 2);
                    }
                }
            }
        }
    }

    int var8 = var7 - 1;

    if (var8 < 0) var8 = 0;
    else if (var8 > 1) var8 = 1;

    for (int var16 = -var8; var16 <= var8; ++var16)
    {
        int var10 = -var8;

        while (var10 <= var8)
        {
            int var11 = y - 1;
            int var17 = 50;

            if (abs_int(var16) == 1 && abs_int(var10) == 1) var17 = jr_int_n(r, 5);

            for (;;)
            {
                if (var11 > 50)
                {
                    int var18 = world_get_block(w, x + var16, var11, z + var10);
                    int m = mat_of(var18);

                    if (m == MAT_AIR || var18 == LK_DIRT || var18 == LK_SNOW || var18 == LK_ICE || var18 == LK_PACKED_ICE)
                    {
                        world_set_block(w, x + var16, var11, z + var10, LK_PACKED_ICE, 0, 2);
                        --var11;
                        --var17;

                        if (var17 <= 0)
                        {
                            var11 -= jr_int_n(r, 5) + 1;
                            var17 = jr_int_n(r, 5);
                        }

                        continue;
                    }
                }

                ++var10;
                break;
            }
        }
    }

    return 1;
}

/* WorldGenIcePath.generate. The block is fixed (packed ice) and `radius` is
 * field_150554_b, 4 in BiomeGenSnow. */
static int feature_icepath(struct world *w, jrand *r, int x, int y, int z, int unused, int radius)
{
    (void)unused;

    while (air_at(w, x, y, z) && y > 2) --y;

    if (world_get_block(w, x, y, z) != LK_SNOW) return 0;

    int var6 = jr_int_n(r, radius - 2) + 2;
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

                    if (var13 == LK_DIRT || var13 == LK_SNOW || var13 == LK_ICE)
                        world_set_block(w, var8, var12, var9, LK_PACKED_ICE, 0, 2);
                }
            }
        }
    }

    return 1;
}

/* The probe's per-feature table for this lane: one row per config, in the same
 * order as the rows appended to FeatureProbe.FEATURES, so a config index means
 * the same thing on both sides. y0/y1 carry the spawn band for a band row and
 * the margin carries the feature's reach; a surface row keeps a zero band
 * (the probe takes its y from the height map instead). */
static const struct feature LAKES[] = {
    {"waterlake", 0, 0, 256, 9, LK_WATER, 0, feature_lake},
    {"lavalake", 0, 0, 256, 9, LK_LAVA, 0, feature_lake},
    {"bigmushroom", 0, 0, 0, 4, 0, 0, feature_bigmushroom},
    {"bigmushroom", 1, 0, 0, 4, 1, 0, feature_bigmushroom},
    {"bigmushroom", 2, 0, 0, 4, -1, 0, feature_bigmushroom},
    {"desertwell", 0, 0, 0, 3, 0, 0, feature_desertwell},
    {"icespike", 0, 0, 0, 5, 0, 0, feature_icespike},
    {"icepath", 0, 0, 0, 5, 0, 4, feature_icepath},
};

const struct feature *features_lakes_for(const char *name, int *n)
{
    *n = 0;

    if (name == NULL) return NULL;

    if (strcmp(name, "waterlake") == 0)
    {
        *n = 1;
        return &LAKES[0];
    }

    if (strcmp(name, "lavalake") == 0)
    {
        *n = 1;
        return &LAKES[1];
    }

    if (strcmp(name, "bigmushroom") == 0)
    {
        *n = 3;
        return &LAKES[2];
    }

    if (strcmp(name, "desertwell") == 0)
    {
        *n = 1;
        return &LAKES[5];
    }

    if (strcmp(name, "icespike") == 0)
    {
        *n = 1;
        return &LAKES[6];
    }

    if (strcmp(name, "icepath") == 0)
    {
        *n = 1;
        return &LAKES[7];
    }

    /* the two lake rows as one table, in the order the probe's rows are in */
    if (strcmp(name, "lakes") == 0)
    {
        *n = 2;
        return &LAKES[0];
    }

    return NULL;
}