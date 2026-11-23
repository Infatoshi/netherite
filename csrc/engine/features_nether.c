/* The Nether and End lane's features: WorldGenHellLava (both the open and the
 * hidden spring, whose updateTick runs at once under
 * World.scheduledUpdatesAreImmediate), WorldGenFire, the two glowstone
 * stalactites, the Nether mushrooms (WorldGenFlowers with BlockMushroom's
 * canBlockStay), the quartz vein (WorldGenMinable over netherrack) and the
 * End's WorldGenSpikes, whose ender crystal is reported through
 * spike_crystal_last instead of spawned.
 *
 * The liquids reuse the springs lane's liquid_update_tick; the crystal is
 * where the entity work would be. WorldGenGlowStone1 and WorldGenGlowStone2
 * are the same decompiled body and share one function here. */
#include "features_nether.h"
#include "env.h"

#include <string.h>

#include "blocks.h"
#include "features_springs.h"
#include "ticks.h"

/* Block ids, Blocks.registerBlock. */
enum {
    NT_AIR = 0, NT_DIRT = 3, NT_BEDROCK = 7, NT_FLOWING_LAVA = 10, NT_OBSIDIAN = 49,
    NT_FIRE = 51, NT_NETHERRACK = 87, NT_GLOWSTONE = 89, NT_PORTAL = 90,
    NT_BROWN_MUSHROOM = 39, NT_RED_MUSHROOM = 40, NT_MYCELIUM = 110,
    NT_END_STONE = 121, NT_QUARTZ_ORE = 153,
};

/* The material indices the feature reads, by name so a regenerated blocks.h
 * cannot change what they mean. */
static int MAT_AIR = -1;

/* before main: the same for every environment */
__attribute__((constructor)) static void mat_init(void)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
        if (MATERIALS[i].name != NULL && strcmp(MATERIALS[i].name, "air") == 0) MAT_AIR = (int)i;
}

static int material_of(int id)
{
    return BLOCKS[id & 4095].material;
}

static int block_at(struct world *w, int x, int y, int z)
{
    return world_get_block(w, x, y, z) & 4095;
}

static int meta_at(struct world *w, int x, int y, int z)
{
    return world_get_meta(w, x, y, z);
}

static int air_at(struct world *w, int x, int y, int z)
{
    return material_of(block_at(w, x, y, z)) == MAT_AIR;
}

/* BlockMushroom.canBlockStay, the stay rule WorldGenFlowers asks through
 * canBlockStay: mycelium, or podzol, or an opaque block under a position whose
 * full block light stays under 13. */
static int mushroom_can_stay(struct world *w, int x, int y, int z)
{
    if (y < 0 || y >= 256) return 0;

    int below = block_at(w, x, y - 1, z);

    if (below == NT_MYCELIUM) return 1;
    if (below == NT_DIRT && meta_at(w, x, y - 1, z) == 2) return 1;

    int light = world_full_block_light_value(w, x, y, z);

    return light < 13 && BLOCKS[below & 4095].opaque_cube;
}

/* WorldGenHellLava.generate. `block` is field_150553_a (Blocks.flowing_lava)
 * and `count` is field_94524_b: 0 the open spring, 1 the hidden one, which
 * only settles when all five neighbours are netherrack. The immediate
 * updateTick is the springs lane's flow, driven by the case's Random. */
static int feature_helllava(struct world *w, jrand *r, int x, int y, int z, int block, int hidden)
{
    if (block_at(w, x, y + 1, z) != NT_NETHERRACK) return 0;

    int here = block_at(w, x, y, z);

    if (material_of(here) != MAT_AIR && here != NT_NETHERRACK) return 0;

    int netherrack_sides = 0, air_sides = 0;

    if (block_at(w, x - 1, y, z) == NT_NETHERRACK) ++netherrack_sides;
    if (block_at(w, x + 1, y, z) == NT_NETHERRACK) ++netherrack_sides;
    if (block_at(w, x, y, z - 1) == NT_NETHERRACK) ++netherrack_sides;
    if (block_at(w, x, y, z + 1) == NT_NETHERRACK) ++netherrack_sides;
    if (block_at(w, x, y - 1, z) == NT_NETHERRACK) ++netherrack_sides;

    if (air_at(w, x - 1, y, z)) ++air_sides;
    if (air_at(w, x + 1, y, z)) ++air_sides;
    if (air_at(w, x, y, z - 1)) ++air_sides;
    if (air_at(w, x, y, z + 1)) ++air_sides;
    if (air_at(w, x, y - 1, z)) ++air_sides;

    /* !hidden && var6 == 4 && var7 == 1, or var6 == 5, as Java's precedence
     * reads it */
    if ((!hidden && netherrack_sides == 4 && air_sides == 1) || netherrack_sides == 5)
    {
        world_set_block(w, x, y, z, block, 0, 2);
        ticks_set_immediate(1);
        liquid_update_tick(w, x, y, z, block, r);
        ticks_set_immediate(0);
    }

    return 1;
}

/* WorldGenFire.generate: 64 attempts, fire on netherrack. The onBlockAdded of
 * every placement (the portal attempt, the stay check and the parked tick) is
 * the flag-2 write's own work in blockcb.c. */
static int feature_fire(struct world *w, jrand *r, int x, int y, int z, int block, int unused)
{
    (void)block;
    (void)unused;

    for (int i = 0; i < 64; ++i)
    {
        int dx = jr_int_n(r, 8), dw = jr_int_n(r, 8);
        int dy = jr_int_n(r, 4), dh = jr_int_n(r, 4);
        int dz = jr_int_n(r, 8), dt = jr_int_n(r, 8);
        int vx = x + dx - dw, vy = y + dy - dh, vz = z + dz - dt;

        if (air_at(w, vx, vy, vz) && block_at(w, vx, vy - 1, vz) == NT_NETHERRACK)
            world_set_block(w, vx, vy, vz, NT_FIRE, 0, 2);
    }

    return 1;
}

/* WorldGenGlowStone1.generate and WorldGenGlowStone2.generate, the same
 * decompiled body: a stalactite grown downward from netherrack, one glowstone
 * at a time where the block has exactly one glowstone neighbour. */
static int feature_glowstone(struct world *w, jrand *r, int x, int y, int z, int block, int unused)
{
    (void)block;
    (void)unused;

    if (!air_at(w, x, y, z)) return 0;
    if (block_at(w, x, y + 1, z) != NT_NETHERRACK) return 0;

    world_set_block(w, x, y, z, NT_GLOWSTONE, 0, 2);

    for (int i = 0; i < 1500; ++i)
    {
        int dx = jr_int_n(r, 8), dw = jr_int_n(r, 8);
        int dy = jr_int_n(r, 12);
        int dz = jr_int_n(r, 8), dt = jr_int_n(r, 8);
        int vx = x + dx - dw, vy = y - dy, vz = z + dz - dt;

        if (material_of(block_at(w, vx, vy, vz)) != MAT_AIR) continue;

        int count = 0;

        if (block_at(w, vx - 1, vy, vz) == NT_GLOWSTONE) ++count;
        if (block_at(w, vx + 1, vy, vz) == NT_GLOWSTONE) ++count;
        if (block_at(w, vx, vy - 1, vz) == NT_GLOWSTONE) ++count;
        if (block_at(w, vx, vy + 1, vz) == NT_GLOWSTONE) ++count;
        if (block_at(w, vx, vy, vz - 1) == NT_GLOWSTONE) ++count;
        if (block_at(w, vx, vy, vz + 1) == NT_GLOWSTONE) ++count;

        if (count == 1) world_set_block(w, vx, vy, vz, NT_GLOWSTONE, 0, 2);
    }

    return 1;
}

/* WorldGenFlowers.generate with a BlockMushroom. The hasNoSky arm of the
 * condition is the Nether's: there y < 255 is asked, here always true for a
 * 128-high world. */
static int feature_nethermushroom(struct world *w, jrand *r, int x, int y, int z, int block, int unused)
{
    (void)unused;

    for (int i = 0; i < 64; ++i)
    {
        int dx = jr_int_n(r, 8), dw = jr_int_n(r, 8);
        int dy = jr_int_n(r, 4), dh = jr_int_n(r, 4);
        int dz = jr_int_n(r, 8), dt = jr_int_n(r, 8);
        int vx = x + dx - dw, vy = y + dy - dh, vz = z + dz - dt;

        if (air_at(w, vx, vy, vz) && (w->dim >= 0 || vy < 255) &&
            mushroom_can_stay(w, vx, vy, vz))
            world_set_block(w, vx, vy, vz, block, 0, 2);
    }

    return 1;
}

/* WorldGenMinable over Blocks.netherrack: the quartz row of populate, the
 * shared minable body with the replaced block as a parameter. */
static int feature_quartz(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    return feature_minable_target(w, r, x, y, z, block, count, NT_NETHERRACK);
}

/* WorldGenSpikes.generate: an obsidian column on end stone, the bedrock cap,
 * and the ender crystal it would spawn, reported through spike_crystal_last
 * (the crystal's position and the nextFloat() * 360 yaw are the generator's
 * own draws, the same ones the oracle's entity record holds). */
static int feature_spikes(struct world *w, jrand *r, int x, int y, int z, int block, int unused)
{
    (void)block;
    (void)unused;

    spike_crystal_last.spawned = 0;

    if (!air_at(w, x, y, z) || block_at(w, x, y - 1, z) != NT_END_STONE) return 0;

    int height = jr_int_n(r, 32) + 6;
    int radius = jr_int_n(r, 4) + 1;

    for (int vx = x - radius; vx <= x + radius; ++vx)
        for (int vz = z - radius; vz <= z + radius; ++vz)
        {
            int dx = vx - x, dz = vz - z;

            if (dx * dx + dz * dz <= radius * radius + 1 &&
                block_at(w, vx, y - 1, vz) != NT_END_STONE)
                return 0;
        }

    for (int vy = y; vy < y + height && vy < 256; ++vy)
        for (int vx = x - radius; vx <= x + radius; ++vx)
            for (int vz = z - radius; vz <= z + radius; ++vz)
            {
                int dx = vx - x, dz = vz - z;

                if (dx * dx + dz * dz <= radius * radius + 1)
                    world_set_block(w, vx, vy, vz, NT_OBSIDIAN, 0, 2);
            }

    float yaw = jr_float(r) * 360.0F;

    /* setLocationAndAngles: posY gets the entity's yOffset, the crystal's
     * height / 2 (setSize(2, 2) in the constructor) */
    spike_crystal_last.spawned = 1;
    spike_crystal_last.x = (double)((float)x + 0.5F);
    spike_crystal_last.y = (double)(y + height) + 1.0;
    spike_crystal_last.z = (double)((float)z + 0.5F);
    spike_crystal_last.yaw = yaw;

    world_set_block(w, x, y + height, z, NT_BEDROCK, 0, 2);
    return 1;
}

/* The probe's per-feature table for this lane: one row per config, in the
 * same order as the rows FeatureProbeNether appends, so a config index means
 * the same thing on both sides. The bands are the draws
 * ChunkProviderHell.populate makes (the probe draws them from its own case
 * Random), the margins are each generator's reach. */
static const struct feature NETHER_ROWS[] = {
    {"helllava", 0, 4, 124, 9, NT_FLOWING_LAVA, 0, feature_helllava},
    {"fire", 0, 4, 124, 8, NT_FIRE, 0, feature_fire},
    {"glowstone1", 0, 4, 124, 7, NT_GLOWSTONE, 0, feature_glowstone},
    {"glowstone2", 0, 0, 128, 7, NT_GLOWSTONE, 0, feature_glowstone},
    {"nethermushrooms", 0, 0, 128, 7, NT_BROWN_MUSHROOM, 0, feature_nethermushroom},
    {"nethermushrooms", 1, 0, 128, 7, NT_RED_MUSHROOM, 0, feature_nethermushroom},
    {"quartz", 0, 10, 118, 11, NT_QUARTZ_ORE, 13, feature_quartz},
    {"helllavahidden", 0, 10, 118, 9, NT_FLOWING_LAVA, 1, feature_helllava},
    {"spikes", 0, 0, 0, 4, NT_AIR, 0, feature_spikes},
};

const struct feature *features_nether_for(const char *name, int *n)
{
    static const struct { const char *name; const struct feature *rows; int n; } table[] = {
        {"helllava", &NETHER_ROWS[0], 1},
        {"fire", &NETHER_ROWS[1], 1},
        {"glowstone1", &NETHER_ROWS[2], 1},
        {"glowstone2", &NETHER_ROWS[3], 1},
        {"nethermushrooms", &NETHER_ROWS[4], 2},
        {"quartz", &NETHER_ROWS[6], 1},
        {"helllavahidden", &NETHER_ROWS[7], 1},
        {"spikes", &NETHER_ROWS[8], 1},
    };

    *n = 0;

    if (name == NULL) return NULL;

    for (size_t i = 0; i < sizeof table / sizeof table[0]; ++i)
        if (strcmp(name, table[i].name) == 0)
        {
            *n = table[i].n;
            return table[i].rows;
        }

    return NULL;
}
