/* BlockFire: the flammable-block tables, the placement tests its callbacks
 * read (blockcb.c) and BlockFire.updateTick, the spread the server tick
 * reaches as soon as a lava tick writes fire (WorldServer.func_147456_g's lava
 * branch for the static form, the pending set for the dynamic one).
 *
 * Everything here is vanilla's order and vanilla's draws: the metadata bump
 * (rand.nextInt(3) / 2), the re-schedule (30 + rand.nextInt(10)), the six
 * func_149841_a spread attempts, then the (x, z, y) cube walk with its
 * rand.nextInt(100 * height penalty) gate and the var16 meta roll. World.rand
 * is the stream the caller published (randomtick_tick_rand).
 *
 * Two pieces of world state the tick reads are not world fields: World.isRaining
 * is rainingStrength > 0.2 and difficultySetting drives the spread's
 * denominator, so the server tick hands both over once per tick (fire_set_weather).
 *
 * The spread that burns a TNT block primes it through fire_tnt_prime.
 * Not ported: BlockPortal.func_150000_e, the portal frame attempt
 * BlockFire.onBlockAdded runs before it schedules.
 */
#include "fire.h"
#include "env.h"

#include <string.h>

#include "biomes.h"
#include "blocks.h"
#include "features_lakes.h" /* biome_at */
#include "ticks.h"
#include "world.h"

enum {
    ID_AIR = 0,
    ID_GRASS = 2,
    ID_DIRT = 3,
    ID_PLANKS = 5,
    ID_LOG = 17,
    ID_LEAVES = 18,
    ID_TALLGRASS = 31,
    ID_WOOL = 35,
    ID_YELLOW_FLOWER = 37,
    ID_RED_FLOWER = 38,
    ID_TNT = 46,
    ID_BOOKSHELF = 47,
    ID_OAK_STAIRS = 53,
    ID_FENCE = 85,
    ID_VINE = 106,
    ID_DOUBLE_WOODEN_SLAB = 125,
    ID_WOODEN_SLAB = 126,
    ID_SPRUCE_STAIRS = 134,
    ID_BIRCH_STAIRS = 135,
    ID_JUNGLE_STAIRS = 136,
    ID_LEAVES2 = 161,
    ID_LOG2 = 162,
    ID_HAY_BLOCK = 170,
    ID_CARPET = 171,
    ID_COAL_BLOCK = 173,
    ID_DOUBLE_PLANT = 175,
};

/* The two tick scalars the spread reads; 0 outside a server tick. */
#define fire_raining (nw_env->fire.raining)
#define fire_difficulty (nw_env->fire.difficulty)

void fire_set_weather(int raining, int difficulty)
{
    fire_raining = raining;
    fire_difficulty = difficulty;
}

/* BlockFire.func_149843_e's field_149849_a: how easily a block catches fire. */
static int fire_burn(int id)
{
    switch (id & 4095)
    {
    case ID_PLANKS:
    case ID_DOUBLE_WOODEN_SLAB:
    case ID_WOODEN_SLAB:
    case ID_FENCE:
    case ID_OAK_STAIRS:
    case ID_BIRCH_STAIRS:
    case ID_SPRUCE_STAIRS:
    case ID_JUNGLE_STAIRS:
    case ID_LOG:
    case ID_LOG2:
    case ID_COAL_BLOCK:
        return 5;

    case ID_LEAVES:
    case ID_LEAVES2:
    case ID_BOOKSHELF:
    case ID_WOOL:
        return 30;

    case ID_TNT:
    case ID_VINE:
        return 15;

    case ID_TALLGRASS:
    case ID_DOUBLE_PLANT:
    case ID_YELLOW_FLOWER:
    case ID_RED_FLOWER:
    case ID_HAY_BLOCK:
    case ID_CARPET:
        return 60;

    default:
        return 0;
    }
}

/* field_149848_b: the odds a func_149841_a attempt burns the neighbour. Logs
 * and the coal block are 5 (their func_149842_a pair is 5, 5), the rest of the
 * 5-burn blocks 20. */
static int fire_odds(int id)
{
    switch (id & 4095)
    {
    case ID_LOG:
    case ID_LOG2:
    case ID_COAL_BLOCK:
        return 5;

    case ID_PLANKS:
    case ID_DOUBLE_WOODEN_SLAB:
    case ID_WOODEN_SLAB:
    case ID_FENCE:
    case ID_OAK_STAIRS:
    case ID_BIRCH_STAIRS:
    case ID_SPRUCE_STAIRS:
    case ID_JUNGLE_STAIRS:
    case ID_BOOKSHELF:
    case ID_HAY_BLOCK:
    case ID_CARPET:
        return 20;

    case ID_LEAVES:
    case ID_LEAVES2:
    case ID_WOOL:
        return 60;

    case ID_TNT:
    case ID_TALLGRASS:
    case ID_DOUBLE_PLANT:
    case ID_YELLOW_FLOWER:
    case ID_RED_FLOWER:
    case ID_VINE:
        return 100;

    default:
        return 0;
    }
}

static int fire_air = -1;

/* before main: the same for every environment */
__attribute__((constructor)) static void fire_air_init(void)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
        if (MATERIALS[i].name != NULL && strcmp(MATERIALS[i].name, "air") == 0) fire_air = (int)i;
}

static int material_is_air(int id)
{
    int air = fire_air;

    return BLOCKS[id & 4095].material == (unsigned)air;
}

static int class_is(int id, const char *name)
{
    const char *c = BLOCKS[id & 4095].class_name;

    return c != NULL && strcmp(c, name) == 0;
}

/* World.doesBlockHaveSolidTopSurface. */
static int solid_top_surface(struct world *w, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z) & 4095;
    int meta = world_get_meta(w, x, y, z);

    if (MATERIALS[BLOCKS[id].material].is_opaque && BLOCKS[id].normal_block) return 1;
    if (class_is(id, "BlockStairs")) return (meta & 4) == 4;
    if (class_is(id, "BlockSlab")) return (meta & 8) == 8;
    if (class_is(id, "BlockHopper")) return 1;
    if (class_is(id, "BlockSnow")) return (meta & 7) == 7;

    return 0;
}

int fire_solid_top_surface(struct world *w, int x, int y, int z)
{
    return solid_top_surface(w, x, y, z);
}

/* BlockFire.func_149844_e: the block at the position can burn. */
static int flammable_at(struct world *w, int x, int y, int z)
{
    return fire_burn(world_get_block(w, x, y, z)) > 0;
}

/* BlockFire.func_149847_e: any of the six neighbours can burn, in its order
 * (x, then y, then z): each read may load a chunk, so the first that burns
 * stops the loads (a fire under a log at a chunk's edge loaded the next
 * chunk natively, reseeding World.rand ahead of its draws; seed-1 S18 row
 * 2824) */
static int neighbor_flammable(struct world *w, int x, int y, int z)
{
    return flammable_at(w, x + 1, y, z) || flammable_at(w, x - 1, y, z) || flammable_at(w, x, y - 1, z) ||
           flammable_at(w, x, y + 1, z) || flammable_at(w, x, y, z - 1) || flammable_at(w, x, y, z + 1);
}

/* BlockFire.canPlaceBlockAt. */
int fire_can_place(struct world *w, int x, int y, int z)
{
    return solid_top_surface(w, x, y - 1, z) || neighbor_flammable(w, x, y, z);
}

/* World.canLightningStrikeAt: rain first, then the sky, the precipitation
 * height, the biome's snow flag, the snow check and canSpawnLightningBolt. */
int fire_can_lightning_strike_at(struct world *w, int x, int y, int z)
{
    if (!fire_raining) return 0;
    if (!world_can_block_see_the_sky(w, x, y, z)) return 0;
    if (world_get_precipitation_height(w, x, z) > y) return 0;

    int biome = biome_at(w, x, z);

    if (BIOMES[biome].snow) return 0;
    if (world_can_snow_at(w, x, y, z, 0)) return 0;

    return BIOMES[biome].lightning;
}

/* BlockFire.func_149846_a: the higher of the block's flammability and the
 * running maximum. */
static int burn_max(struct world *w, int x, int y, int z, int cur)
{
    int v = fire_burn(world_get_block(w, x, y, z));

    return v > cur ? v : cur;
}

/* BlockFire.func_149845_m: an air cell's burn value, the maximum flammability
 * over its six neighbours. */
static int burn_value(struct world *w, int x, int y, int z)
{
    if (!material_is_air(world_get_block(w, x, y, z))) return 0;

    int v = 0;

    v = burn_max(w, x + 1, y, z, v);
    v = burn_max(w, x - 1, y, z, v);
    v = burn_max(w, x, y, z - 1, v);
    v = burn_max(w, x, y, z + 1, v);
    v = burn_max(w, x, y - 1, z, v);
    v = burn_max(w, x, y + 1, z, v);
    return v;
}

/* The fire tick's World.setBlockMetadataWithNotify for the age bump. */
static void fire_set_age(struct world *w, int x, int y, int z, int meta)
{
    world_set_meta(w, x, y, z, meta, 4);
}

/* BlockFire.func_149841_a: one spread attempt on a neighbour. */
static void spread_attempt(struct world *w, jrand *wr, int x, int y, int z, int chance, int meta)
{
    int odds = fire_odds(world_get_block(w, x, y, z));

    if (jr_int_n(wr, chance) >= odds) return;

    int var9 = (world_get_block(w, x, y, z) & 4095) == ID_TNT;

    if (jr_int_n(wr, meta + 10) < 5 && !fire_can_lightning_strike_at(w, x, y, z))
    {
        int v = meta + jr_int_n(wr, 5) / 4;

        if (v > 15) v = 15;

        world_set_block(w, x, y, z, FIRE_BLOCK, v, 3);
    }
    else
    {
        world_set_block(w, x, y, z, ID_AIR, 0, 3);
    }

    if (var9 && fire_tnt_prime != NULL) fire_tnt_prime(fire_tnt_prime_ctx, x, y, z);
}

/* BlockFire.updateTick, with wr as World.rand. */
void fire_update_tick(struct world *w, jrand *wr, int x, int y, int z)
{
    /* World.getGameRules doFireTick (true in every recording) */
    int below = world_get_block(w, x, y - 1, z) & 4095;
    /* netherrack below, or in the End (WorldProviderEnd) bedrock: the
     * ender crystals' fires burn forever */
    int netherrack_below = below == 87 || (w->dim == 1 && below == 7);

    if (!fire_can_place(w, x, y, z)) world_set_block(w, x, y, z, ID_AIR, 0, 3);

    if (!netherrack_below && fire_raining &&
        (fire_can_lightning_strike_at(w, x, y, z) || fire_can_lightning_strike_at(w, x - 1, y, z) ||
         fire_can_lightning_strike_at(w, x + 1, y, z) || fire_can_lightning_strike_at(w, x, y, z - 1) ||
         fire_can_lightning_strike_at(w, x, y, z + 1)))
    {
        world_set_block(w, x, y, z, ID_AIR, 0, 3);
        return;
    }

    int meta = world_get_meta(w, x, y, z);

    if (meta < 15) fire_set_age(w, x, y, z, meta + jr_int_n(wr, 3) / 2);

    ticks_schedule_block_update(w, x, y, z, FIRE_BLOCK, 30 + jr_int_n(wr, 10));

    if (!netherrack_below && !neighbor_flammable(w, x, y, z))
    {
        if (!solid_top_surface(w, x, y - 1, z) || meta > 3) world_set_block(w, x, y, z, ID_AIR, 0, 3);

        return;
    }

    if (!netherrack_below && !flammable_at(w, x, y - 1, z) && meta == 15 && jr_int_n(wr, 4) == 0)
    {
        world_set_block(w, x, y, z, ID_AIR, 0, 3);
        return;
    }

    int humid = BIOMES[biome_at(w, x, z)].rainfall > 0.85F;
    int penalty = humid ? -50 : 0;

    spread_attempt(w, wr, x + 1, y, z, 300 + penalty, meta);
    spread_attempt(w, wr, x - 1, y, z, 300 + penalty, meta);
    spread_attempt(w, wr, x, y - 1, z, 250 + penalty, meta);
    spread_attempt(w, wr, x, y + 1, z, 250 + penalty, meta);
    spread_attempt(w, wr, x, y, z - 1, 300 + penalty, meta);
    spread_attempt(w, wr, x, y, z + 1, 300 + penalty, meta);

    /* the (x, z, y) cube: the spread into cells that are not adjacent, gated by
     * their burn value and the height penalty */
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 4; ++dy)
            {
                if (dx == 0 && dy == 0 && dz == 0) continue;

                int cx = x + dx, cy = y + dy, cz = z + dz;
                int chance = 100;

                if (dy > 1) chance += (dy - 1) * 100;

                int value = burn_value(w, cx, cy, cz);

                if (value <= 0) continue;

                int limit = (value + 40 + fire_difficulty * 7) / (meta + 30);

                if (humid) limit /= 2;

                /* the four side lightning checks read the fire's own z, exactly
                 * as the decompiled code does */
                if (limit > 0 && jr_int_n(wr, chance) <= limit && (!fire_raining || !fire_can_lightning_strike_at(w, cx, cy, cz)) &&
                    !fire_can_lightning_strike_at(w, cx - 1, cy, z) && !fire_can_lightning_strike_at(w, cx + 1, cy, cz) &&
                    !fire_can_lightning_strike_at(w, cx, cy, cz - 1) && !fire_can_lightning_strike_at(w, cx, cy, cz + 1))
                {
                    int v = meta + jr_int_n(wr, 5) / 4;

                    if (v > 15) v = 15;

                    world_set_block(w, cx, cy, cz, FIRE_BLOCK, v, 3);
                }
            }
}