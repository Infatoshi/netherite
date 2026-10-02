/* Block.onBlockAdded and Block.onNeighborBlockChange, the two callbacks
 * World.setBlock runs over a stored block (see blockcb.h).
 *
 * Which classes get a body here, and why:
 *
 *   BlockLiquid          func_149805_n: lava with water beside or above it
 *                        becomes obsidian (meta 0) or cobblestone (meta 1..4).
 *                        The fizz sound and the smoke particles it also runs
 *                        draw from World.rand and write no world state
 *                        (randomtick_callback_fizz).
 *   BlockStaticLiquid    super, then setNotStationary: a still water or lava
 *                        that survived becomes its dynamic form (id - 1) with
 *                        flags 2 (no notification), plus a scheduled tick the
 *                        probe world never runs.
 *   BlockDynamicLiquid   super, plus a scheduled tick.
 *   BlockTrapDoor        removes itself when the block behind its facing is
 *                        gone, and toggles its open bit on a redstone change.
 *   BlockSnow            canPlaceBlockAt: removes itself without a floor.
 *   BlockCactus          canBlockStay.
 *   BlockLadder          removes itself when its wall side is no longer a
 *                        normal cube.
 *   BlockVine            func_150094_e: clears the face bits that lost their
 *                        wall and removes itself when none is left.
 *   BlockCarpet          canBlockStay: needs a floor.
 *   BlockBed             removes a half whose partner is gone.
 *   BlockCake            canBlockStay.
 *   BlockFarmland        turns to dirt when the block above turns solid.
 *   BlockFenceGate       toggles its open bit on a redstone change.
 *
 * Every other block the shape table places (slabs, stairs, fences, panes,
 * walls, soul sand, web, glass, stone, sponge, lily pad, air, cake's second
 * row) inherits Block's empty bodies, and so does the terrain, apart from the
 * liquids.
 *
 * World.setBlock passes notifyBlockChange the block that was replaced, so
 * canProvidePower on that argument drives the trapdoor and fence gate. The
 * probe worlds hold no block that provides power, which leaves those branches
 * dead here; see the report for what a real redstone port would need. */
#include "blockcb.h"
#include "comparator.h"
#include "blockwl.h"
#include "env.h"
#include "spill.h"
#include "blocks.h"
#include "place.h"
#include "fire.h"
#include "jrand.h"
#include "drops.h"
#include "randomtick.h"
#include "ticks.h"
#include "tileentity.h"
#include "tileticks.h"
#include "world.h"

#include <stdio.h>
#include <string.h>

/* The nested calls a callback makes are emitted to the worklist (blockwl.h):
 * a write, a flag-1 metadata write and a notification run, with everything
 * they trigger, after the emitting step and before whatever that step emits
 * next. The quiet metadata writes (flags 2 and 4) run in place. */
#define SET_BLOCK(w, x, y, z, id, meta, flags) BWL_EMIT(BWL_SET_BLOCK, w, x, y, z, id, meta, flags)
#define SET_META_NOTIFY(w, x, y, z, meta) BWL_EMIT(BWL_SET_META, w, x, y, z, meta, 3)
#define NOTIFY(w, x, y, z, source) BWL_EMIT(BWL_NOTIFY, w, x, y, z, source, -1)

/* The breaking callbacks below run Block.dropBlockAsItem, whose World.rand
 * draws are world state even though the entities are not: a tick in progress
 * publishes its World.rand stream (randomtick.h) and the drop draws are spent
 * through it, so the tick's recorded World.rand state stays exact. Outside a
 * tick the hook answers at once. */

static void breaking_drop(struct world *w, int x, int y, int z, int id, int meta)
{
    bwl_quiet();

    if (nw_env->blockcb.env.world_rand == NULL && randomtick_tick_rand_stream() == NULL)
    {
        /* neither a tick environment nor a published tick stream (the
         * placement probe): the drop draws the case's world Random and
         * Math.random through the placement port's drop */
        place_block_drop(w, x, y, z, id, meta);
        return;
    }

    struct randomtick_env rt;
    randomtick_tick_save(&rt);
    if (nw_env->blockcb.env.world_rand != NULL && nw_env->blockcb.env.det != NULL && nw_env->blockcb.env.item_drop != NULL &&
        (nw_env->blockcb.env.world_rand != randomtick_tick_rand_stream() || rt.sink == NULL))
    {
        /* a write inside an entity's update (an explosion's setBlock pops the
         * plant above): World.rand is the environment's, handed over for the
         * entity pass, not the tick's published stream, and the item goes to
         * the environment's sink; so does a write outside the world tick (the
         * network tick's dig under a flower, a Dev fill), where the tick's
         * stream has no sink of its own */
        randomtick_drop_into(nw_env->blockcb.env.world_rand, blockcb_env_drop_sink, NULL, x, y, z, id, meta);
        return;
    }

    randomtick_drop(w, x, y, z, id, meta);
}

/* Block.dropBlockAsItem for a caller outside the block callbacks (the
 * bucket's func_147480_a break): the same routing as a callback's pop. */
void blockcb_break_drop(struct world *w, int x, int y, int z, int id, int meta)
{
    breaking_drop(w, x, y, z, id, meta);
}

/* ------------------------------------------------------------ registry reads */
static int block_at(struct world *w, int x, int y, int z)
{
    bwl_quiet();
    return world_get_block(w, x, y, z) & 4095;
}

static int meta_at(struct world *w, int x, int y, int z)
{
    bwl_quiet();
    return world_get_meta(w, x, y, z);
}

/* Material indices, looked up by name so a regenerated blocks.h cannot change
 * what these mean. */
static int material_named(const char *name)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
        if (MATERIALS[i].name != NULL && strcmp(MATERIALS[i].name, name) == 0) return (int)i;

    return -1;
}

static int material_of(int id)
{
    return BLOCKS[id & 4095].material;
}

static int class_named(int id, const char *name)
{
    const char *n = BLOCKS[id & 4095].class_name;
    return n != NULL && strcmp(n, name) == 0;
}

/* The block classes the callbacks test, a bit each in bc_cls by block id
 * (is_class: the table's bit, not a strcmp a call) */
enum {
    BC_CACTUS,
    BC_CARROT,
    BC_CROPS,
    BC_DEADBUSH,
    BC_DOUBLEPLANT,
    BC_FLOWER,
    BC_HOPPER,
    BC_LILYPAD,
    BC_MUSHROOM,
    BC_NETHERWART,
    BC_POTATO,
    BC_RAIL,
    BC_RAILDETECTOR,
    BC_RAILPOWERED,
    BC_REED,
    BC_SAPLING,
    BC_SNOW,
    BC_STAIRS,
    BC_STEM,
    BC_STONESLAB,
    BC_TALLGRASS,
    BC_WOODSLAB,
    BC_COUNT
};
static const char *const bc_class_names[BC_COUNT] = {
    [BC_CACTUS] = "BlockCactus",
    [BC_CARROT] = "BlockCarrot",
    [BC_CROPS] = "BlockCrops",
    [BC_DEADBUSH] = "BlockDeadBush",
    [BC_DOUBLEPLANT] = "BlockDoublePlant",
    [BC_FLOWER] = "BlockFlower",
    [BC_HOPPER] = "BlockHopper",
    [BC_LILYPAD] = "BlockLilyPad",
    [BC_MUSHROOM] = "BlockMushroom",
    [BC_NETHERWART] = "BlockNetherWart",
    [BC_POTATO] = "BlockPotato",
    [BC_RAIL] = "BlockRail",
    [BC_RAILDETECTOR] = "BlockRailDetector",
    [BC_RAILPOWERED] = "BlockRailPowered",
    [BC_REED] = "BlockReed",
    [BC_SAPLING] = "BlockSapling",
    [BC_SNOW] = "BlockSnow",
    [BC_STAIRS] = "BlockStairs",
    [BC_STEM] = "BlockStem",
    [BC_STONESLAB] = "BlockStoneSlab",
    [BC_TALLGRASS] = "BlockTallGrass",
    [BC_WOODSLAB] = "BlockWoodSlab",
};

/* the materials the callbacks test, the rail classes (is_rail_block) and
 * the classes by id: looked up before main, the same for every environment */
static int bc_mat_air, bc_mat_water, bc_mat_lava, bc_mat_leaves;
static unsigned char bc_rail[4096];
static uint32_t bc_cls[4096];

static inline int is_class(int id, int bc)
{
    return (int)(bc_cls[id & 4095] >> bc) & 1;
}

__attribute__((constructor)) static void bc_tables_init(void)
{
    bc_mat_air = material_named("air");
    bc_mat_water = material_named("water");
    bc_mat_lava = material_named("lava");
    bc_mat_leaves = material_named("leaves");
    for (int id = 0; id < 4096; ++id)
    {
        for (int k = 0; k < BC_COUNT; ++k)
            if (class_named(id, bc_class_names[k])) bc_cls[id] |= 1u << k;
        bc_rail[id] = is_class(id, BC_RAIL) || is_class(id, BC_RAILPOWERED) || is_class(id, BC_RAILDETECTOR);
    }
}

/* BlockStairs.func_150119_a tests instanceof BlockSlab / BlockStairs, which is
 * the class name for every slab and stair id. */
static int is_slab(int id)
{
    return is_class(id, BC_STONESLAB) || is_class(id, BC_WOODSLAB);
}

/* ------------------------------------------------------------- items and ids
 * The block ids the callbacks name, from Blocks. */
enum {
    ID_DIRT = 3,
    ID_COBBLESTONE = 4,
    ID_SAND = 12,
    ID_BED = 26,
    ID_OBSIDIAN = 49,

    ID_FIRE = 51,
    ID_FARMLAND = 60,
    ID_LADDER = 65,
    ID_SNOW_LAYER = 78,
    ID_ICE = 79,
    ID_CACTUS = 81,
    ID_REED = 83,
    ID_GLOWSTONE = 89,
    ID_PORTAL = 90,
    ID_CAKE = 92,
    ID_DOOR = 64,
    ID_TRAPDOOR = 96,
    ID_VINE = 106,
    ID_FENCE_GATE = 107,
    ID_MYCELIUM = 110,
    ID_STAINED_CLAY = 159,
    ID_CARPET = 171,
    ID_HARDENED_CLAY = 172,
    ID_PACKED_ICE = 174,
    ID_GLASS = 20,
    ID_TORCH = 50,
    ID_FURNACE = 61,
    ID_LIT_FURNACE = 62,
    ID_GOLDEN_RAIL = 27,
    ID_DETECTOR_RAIL = 28,
    ID_RAIL = 66,
    ID_ACTIVATOR_RAIL = 157,
    ID_FENCE = 85,
    ID_NETHER_BRICK_FENCE = 113,
    ID_COBBLE_WALL = 139,
    /* the temple pieces' redstone blocks */
    ID_DISPENSER = 23,
    ID_REDSTONE_WIRE = 55,
    ID_LEVER = 69,
    ID_BTN_STONE = 77,
    ID_BTN_WOOD = 143,
    ID_UNPOWERED_REPEATER = 93,
    ID_POWERED_REPEATER = 94,
    ID_UNPOWERED_COMPARATOR = 149,
    ID_POWERED_COMPARATOR = 150,
    ID_TRIPWIRE_HOOK = 131,
    ID_TRIPWIRE = 132,
    ID_JUKEBOX = 84,
    ID_FLOWER_POT = 140,
    /* the items those drop, from Items */
    ITEM_TRIPWIRE_HOOK = 131,
    ITEM_STRING = 287,
    ITEM_BED = 355,
    ITEM_REPEATER = 356,
    ITEM_COMPARATOR = 404,
    ITEM_REDSTONE = 331,
    /* the falling blocks this file schedules */
    ID_ANVIL = 145,
    ID_DRAGON_EGG = 122
};

/* ------------------------------------------------------------- the liquids */
/* BlockLiquid.func_149805_n. */
static void lava_water_check(struct world *w, int id, int x, int y, int z)
{
    if (block_at(w, x, y, z) != id) return; /* getBlock(...) == this */
    if (material_of(id) != bc_mat_lava) return;

    int water = material_of(block_at(w, x, y, z - 1)) == bc_mat_water ||
                material_of(block_at(w, x, y, z + 1)) == bc_mat_water ||
                material_of(block_at(w, x - 1, y, z)) == bc_mat_water ||
                material_of(block_at(w, x + 1, y, z)) == bc_mat_water ||
                material_of(block_at(w, x, y + 1, z)) == bc_mat_water;

    if (!water) return;

    int m = meta_at(w, x, y, z);

    if (m == 0) SET_BLOCK(w, x, y, z, ID_OBSIDIAN, 0, 3);
    else if (m <= 4) SET_BLOCK(w, x, y, z, ID_COBBLESTONE, 0, 3);

    BWL_EMIT(BWL_LAVA_FIZZ, w, x, y, z, 0);
}

/* BlockLiquid.func_149799_m after lava_water_check's write: the fizz sound's
 * two World.rand nextFloat and the smoke particles' sixteen Math.random. The
 * World.rand draws go to an installed environment's stream (the explosion's,
 * blockcb_env) first, else the server tick's; the Math.random draws follow the
 * tick's Det streams when one is published. */
static void lava_fizz(void)
{
    if (nw_env->blockcb.env.world_rand != NULL)
    {
        (void)jr_float(nw_env->blockcb.env.world_rand);
        (void)jr_float(nw_env->blockcb.env.world_rand);

        for (int i = 0; nw_env->blockcb.env.det != NULL && i < 8; ++i)
        {
            (void)jr_double(&nw_env->blockcb.env.det->math[nw_env->blockcb.env.role].r);
            (void)jr_double(&nw_env->blockcb.env.det->math[nw_env->blockcb.env.role].r);
        }
    }
    else
    {
        /* the placement probe's case streams when no tick environment is
         * published (randomtick's callback is absent there): the fizz draws
         * the case's world Random and Math.random */
        place_lava_fizz();

        randomtick_callback_fizz();
    }
}

/* BlockStaticLiquid.onNeighborBlockChange: the lava check, then the still to
 * dynamic switch for a block that is still there, plus the scheduled tick
 * setNotStationary parks (under the immediate flag ticks.c runs it at once). */
static void static_liquid_tail(struct world *w, int id, int x, int y, int z)
{
    if (block_at(w, x, y, z) != id) return;

    SET_BLOCK(w, x, y, z, id - 1, meta_at(w, x, y, z), 2);
    ticks_bwl_sched(w, x, y, z, id - 1, ticks_liquid_tick_rate_world(w, id - 1));
}

static void static_liquid_neighbor(struct world *w, int id, int x, int y, int z)
{
    lava_water_check(w, id, x, y, z);

    if (bwl_pending()) BWL_EMIT(BWL_STATIC_NEIGHBOR, w, x, y, z, id);
    else static_liquid_tail(w, id, x, y, z);
}

/* BlockDynamicLiquid.onBlockAdded after the lava check: the scheduled tick
 * for a block that is still there */
static void liquid_added_tail(struct world *w, int id, int x, int y, int z)
{
    if (block_at(w, x, y, z) == id) ticks_bwl_sched(w, x, y, z, id, ticks_liquid_tick_rate_world(w, id));
}

/* ------------------------------------------------------------- the plants */
/* BlockTrapDoor.func_150119_a: an opaque full cube, glowstone, a slab or a
 * stair can hold a trapdoor. */
static int trapdoor_support(int b)
{
    if (MATERIALS[material_of(b)].is_opaque && BLOCKS[b & 4095].normal_block) return 1;
    if (b == ID_GLOWSTONE) return 1;

    return is_slab(b) || is_class(b, BC_STAIRS);
}

/* BlockTrapDoor.func_150120_a. */
static void trapdoor_set_open(struct world *w, int x, int y, int z, int open)
{
    int m = meta_at(w, x, y, z);

    if (((m & 4) > 0) != open)
    {
        world_set_meta_quiet(w, x, y, z, m ^ 4);
        env_aux_sfx(w, 1003, x, y, z, 0);   /* playAuxSFXAtEntity(null, 1003) */
    }
}

/* BlockTrapDoor.onNeighborBlockChange after its support test: the popped
 * trapdoor's drop (the metadata the cell holds now), then the power test. */
static int is_indirectly_powered(struct world *w, int x, int y, int z);
static int source_provides_power(int source);

static void trapdoor_tail(struct world *w, int x, int y, int z, int source, int popped)
{
    if (popped) place_block_drop(w, x, y, z, ID_TRAPDOOR, meta_at(w, x, y, z));

    int powered = is_indirectly_powered(w, x, y, z);

    if (powered || source_provides_power(source)) trapdoor_set_open(w, x, y, z, powered);
}

/* BlockSnow.canPlaceBlockAt. */
static int snow_can_place(struct world *w, int x, int y, int z)
{
    int b = block_at(w, x, y - 1, z);

    if (b == ID_ICE || b == ID_PACKED_ICE) return 0;
    if (material_of(b) == bc_mat_leaves) return 1;
    if (b == ID_SNOW_LAYER && (meta_at(w, x, y - 1, z) & 7) == 7) return 1;

    return BLOCKS[b].opaque_cube && MATERIALS[material_of(b)].blocks_movement;
}

/* BlockCactus.canBlockStay. */
static int cactus_can_stay(struct world *w, int x, int y, int z)
{
    if (MATERIALS[material_of(block_at(w, x - 1, y, z))].is_solid) return 0;
    if (MATERIALS[material_of(block_at(w, x + 1, y, z))].is_solid) return 0;
    if (MATERIALS[material_of(block_at(w, x, y, z - 1))].is_solid) return 0;
    if (MATERIALS[material_of(block_at(w, x, y, z + 1))].is_solid) return 0;

    int below = block_at(w, x, y - 1, z);

    return below == ID_CACTUS || below == ID_SAND;
}

/* BlockCarpet.canBlockStay: any block below, not air. */
static int carpet_can_stay(struct world *w, int x, int y, int z)
{
    return material_of(block_at(w, x, y - 1, z)) != bc_mat_air;
}

/* BlockVine.func_150093_a: a wall that can hold a vine. */
static int vine_attach_ok(int b)
{
    return BLOCKS[b & 4095].normal_block && MATERIALS[material_of(b)].blocks_movement;
}

/* BlockVine.func_150094_e. Direction.offsetX/offsetZ for bits 0..3, and
 * func_150118_d's own two tables are the same ones the collision boxes use. */
static int vine_update(struct world *w, int x, int y, int z)
{
    static const int off_x[4] = {0, -1, 0, 1};
    static const int off_z[4] = {1, 0, -1, 0};
    int m = meta_at(w, x, y, z);
    int nm = m;

    if (m > 0)
    {
        for (int i = 0; i <= 3; ++i)
        {
            int bit = 1 << i;

            if ((m & bit) != 0 && !vine_attach_ok(block_at(w, x + off_x[i], y, z + off_z[i])) &&
                (block_at(w, x, y + 1, z) != ID_VINE || (meta_at(w, x, y + 1, z) & bit) == 0))
                nm &= ~bit;
        }
    }

    if (nm == 0 && !vine_attach_ok(block_at(w, x, y + 1, z))) return 0;

    if (nm != m) world_set_meta_quiet(w, x, y, z, nm);

    return 1;
}

/* BlockBed.func_149981_a: the offset from a bed's foot to its head, by the
 * two facing bits. */
static void bed_head_offset(int dir, int *dx, int *dz)
{
    static const int facing[4][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};

    *dx = facing[dir & 3][0];
    *dz = facing[dir & 3][1];
}

/* ------------------------------------------------------- the bushes (plants)
 * BlockBush and its subclasses. BlockBush.onNeighborBlockChange runs
 * func_149855_e, which drops and removes the plant with a flags-2 write (so
 * nothing is notified). The floor test is func_149854_a, overridden per
 * subclass; Block.opaque, which BlockMushroom asks for through
 * func_149730_j, is set from isOpaqueCube in the constructor and never
 * changes, so the registry's opaque_cube is the same value. */

/* instanceof BlockCrops: BlockCarrot and BlockPotato extend it */
static int is_crops(int id)
{
    return is_class(id, BC_CROPS) || is_class(id, BC_CARROT) || is_class(id, BC_POTATO);
}

static int is_bush_family(int id)
{
    return is_class(id, BC_FLOWER) || is_class(id, BC_SAPLING) || is_class(id, BC_TALLGRASS) ||
           is_class(id, BC_DEADBUSH) || is_class(id, BC_MUSHROOM) || is_crops(id) ||
           is_class(id, BC_STEM) || is_class(id, BC_LILYPAD) || is_class(id, BC_DOUBLEPLANT) ||
           is_class(id, BC_NETHERWART);
}

/* BlockBush.func_149854_a (through canBlockStay), per subclass. */
static int bush_floor_ok(int id, struct world *w, int x, int y, int z)
{
    int below = block_at(w, x, y - 1, z);

    if (is_class(id, BC_DEADBUSH))
        return below == ID_SAND || below == ID_HARDENED_CLAY || below == ID_STAINED_CLAY || below == ID_DIRT;

    if (is_class(id, BC_MUSHROOM))
    {
        /* BlockMushroom.canBlockStay: mycelium always, dirt only at metadata 2
         * (podzol), otherwise an opaque floor with the full light at the
         * mushroom below 13 (World.getFullBlockLightValue, no skylight
         * subtraction) */
        if (y < 0 || y >= 256) return 0;
        if (below == ID_MYCELIUM) return 1;
        if (below == ID_DIRT && meta_at(w, x, y - 1, z) == 2) return 1;

        return world_get_full_block_light_value(w, x, y, z, 0) < 13 && BLOCKS[below & 4095].opaque_cube;
    }

    if (is_class(id, BC_NETHERWART)) return below == 88; /* soul sand */

    if (is_crops(id) || is_class(id, BC_STEM)) return below == ID_FARMLAND;

    /* BlockBush's own: grass, dirt or farmland */
    return below == 2 || below == ID_DIRT || below == ID_FARMLAND;
}

/* BlockBush.canBlockStay, with BlockLilyPad's and BlockDoublePlant's own. */
static int bush_can_stay(struct world *w, int id, int x, int y, int z)
{
    if (is_class(id, BC_LILYPAD))
        return material_of(block_at(w, x, y - 1, z)) == bc_mat_water && meta_at(w, x, y - 1, z) == 0;


    if (is_class(id, BC_DOUBLEPLANT))
    {
        /* the upper half sits on its lower half; the lower half needs its upper
         * half and a floor */
        if ((meta_at(w, x, y, z) & 8) != 0) return block_at(w, x, y - 1, z) == id;

        return block_at(w, x, y + 1, z) == id && bush_floor_ok(id, w, x, y, z);
    }

    return bush_floor_ok(id, w, x, y, z);
}

/* The placement's soil test (the placement's ItemBlock.onItemUse reaches it
 * through the Bush's canPlaceBlockAt). */
int blockcb_bush_floor_ok(int id, struct world *w, int x, int y, int z)
{
    return bush_floor_ok(id, w, x, y, z);
}

/* Block.canPlaceBlockAt's stay check for the blocks the activation lane's
 * held pools reach: the bushes and the cactus. */
int blockcb_can_stay(int id, struct world *w, int x, int y, int z)
{
    if (is_bush_family(id)) return bush_floor_ok(id, w, x, y, z);
    if (is_class(id, BC_CACTUS)) return cactus_can_stay(w, x, y, z);
    return 1;
}

/* BlockBush.func_149855_e, and BlockDoublePlant's override that also clears the
 * other half of the plant. The pop drops the plant first (fortune 0), then
 * clears with a flags-2 write, as the sources do. */
static void bush_neighbor(struct world *w, int id, int x, int y, int z)
{
    if (bush_can_stay(w, id, x, y, z)) return;

    if (is_class(id, BC_DOUBLEPLANT))
    {
        if ((meta_at(w, x, y, z) & 8) == 0 && block_at(w, x, y + 1, z) == id)
            SET_BLOCK(w, x, y + 1, z, 0, 0, 2);

        SET_BLOCK(w, x, y, z, 0, 0, 2);
        return;
    }

    SET_BLOCK(w, x, y, z, 0, 0, 2);
}

/* BlockReed.canPlaceBlockAt, which its canBlockStay simply delegates to. */
static int reed_can_stay(struct world *w, int x, int y, int z)
{
    int below = block_at(w, x, y - 1, z);

    if (below == ID_REED) return 1;
    if (below != 2 && below != ID_DIRT && below != ID_SAND) return 0;

    return material_of(block_at(w, x - 1, y - 1, z)) == bc_mat_water ||
           material_of(block_at(w, x + 1, y - 1, z)) == bc_mat_water ||
           material_of(block_at(w, x, y - 1, z - 1)) == bc_mat_water ||
           material_of(block_at(w, x, y - 1, z + 1)) == bc_mat_water;
}

/* --------------------------------------------- the placement callbacks */
/* The bodies the blocks a placement stores run, beside the liquids' and the
 * plants' bodies above. */

/* World.doesBlockHaveSolidTopSurface. */
static int solid_top_at(struct world *w, int x, int y, int z)
{
    int b = block_at(w, x, y, z);

    if (MATERIALS[material_of(b)].is_opaque && BLOCKS[b].normal_block) return 1;
    if (is_class(b, BC_STAIRS)) return (meta_at(w, x, y, z) & 4) == 4;
    if (is_slab(b)) return (meta_at(w, x, y, z) & 8) == 8;
    if (is_class(b, BC_HOPPER)) return 1;
    if (is_class(b, BC_SNOW)) return (meta_at(w, x, y, z) & 7) == 7;
    return 0;
}

/* BlockTorch.func_150107_m, which place.c consults through this export. */
int place_torch_can_support(struct world *w, int x, int y, int z)
{
    if (solid_top_at(w, x, y, z)) return 1;

    int b = block_at(w, x, y, z);

    return b == 30 || b == 139 || b == 20;   /* fence, cobblestone wall, glass */
}

/* ------------------------------------------------------- fire and its portal
 * BlockFire.onBlockAdded and the portal frame search it runs in a world whose
 * provider.dimensionId <= 0 (the Nether and the overworld; the End skips it).
 * A raw region holds no obsidian, so the search never builds a frame there,
 * but it is the callback vanilla runs on every fire the probe places. */

/* Direction.offsetX / offsetZ. */
static const int DIR_X[4] = {0, -1, 0, 1};
static const int DIR_Z[4] = {1, 0, -1, 0};

/* BlockPortal.field_150001_a: per axis, the width direction then the along
 * direction. */
static const int PORTAL_DIR[3][2] = {{0, 0}, {3, 1}, {2, 0}};

/* BlockPortal.func_150857_a: what a frame's inside accepts. */
static int portal_frame_ok(int b)
{
    return material_of(b) == bc_mat_air || b == ID_FIRE || b == ID_PORTAL;
}

/* BlockPortal.Size.func_150853_a: how far the open run along `dir` goes, with
 * obsidian under every block, 0 when the run does not end in obsidian. */
static int portal_run_along(struct world *w, int x, int y, int z, int dir)
{
    int dx = DIR_X[dir], dz = DIR_Z[dir];
    int i;

    for (i = 0; i < 22; ++i)
    {
        if (!portal_frame_ok(block_at(w, x + dx * i, y, z + dz * i))) break;
        if (block_at(w, x + dx * i, y - 1, z + dz * i) != ID_OBSIDIAN) break;
    }

    return block_at(w, x + dx * i, y, z + dz * i) == ID_OBSIDIAN ? i : 0;
}

/* BlockPortal.Size: one axis of the frame search at (x, y, z). has_origin is
 * field_150861_f != null; width is field_150868_h, height field_150862_g,
 * existing field_150864_e (the portal blocks already inside). */
struct portal_size
{
    int has_origin, width, height, existing, ox, oy, oz;
};

/* BlockPortal.Size.func_150858_a, the height of the frame above the origin
 * row: upward while every row stays open with obsidian on its two ends, then
 * the top row has to be obsidian all the way across; 0 and no origin when the
 * frame does not close. */
static void portal_frame_height(struct world *w, struct portal_size *s, int axis)
{
    int dx = DIR_X[PORTAL_DIR[axis][1]], dz = DIR_Z[PORTAL_DIR[axis][1]];
    int sx = DIR_X[PORTAL_DIR[axis][0]], sz = DIR_Z[PORTAL_DIR[axis][0]];
    int g;

    /* the label56 break: the height stays where the interior gave up */
    for (g = 0; g < 21; ++g)
    {
        int y = s->oy + g;
        int broken = 0;

        for (int i = 0; i < s->width; ++i)
        {
            int bx = s->ox + dx * i, bz = s->oz + dz * i;
            int b = block_at(w, bx, y, bz);

            if (!portal_frame_ok(b)) { broken = 1; break; }
            if (b == ID_PORTAL) ++s->existing;

            if (i == 0 && block_at(w, bx + sx, y, bz + sz) != ID_OBSIDIAN) { broken = 1; break; }
            if (i == s->width - 1 && block_at(w, bx + dx, y, bz + dz) != ID_OBSIDIAN) { broken = 1; break; }
        }

        if (broken) break;
    }

    for (int i = 0; i < s->width; ++i)
        if (block_at(w, s->ox + dx * i, s->oy + g, s->oz + dz * i) != ID_OBSIDIAN)
        {
            g = 0;
            break;
        }

    if (g <= 21 && g >= 3) s->height = g;
    else
    {
        s->has_origin = 0;
        s->width = 0;
        s->height = 0;
    }
}

static void portal_size_init(struct world *w, struct portal_size *s, int x, int y, int z, int axis)
{
    s->has_origin = 0;
    s->width = 0;
    s->height = 0;
    s->existing = 0;

    /* the walk down to the frame's floor, at most 21 blocks */
    int bottom = y;

    for (; y > bottom - 21 && y > 0 && portal_frame_ok(block_at(w, x, y - 1, z)); --y)
        ;

    int v7 = portal_run_along(w, x, y, z, PORTAL_DIR[axis][0]) - 1;

    if (v7 >= 0)
    {
        s->ox = x + v7 * DIR_X[PORTAL_DIR[axis][0]];
        s->oy = y;
        s->oz = z + v7 * DIR_Z[PORTAL_DIR[axis][0]];
        s->width = portal_run_along(w, s->ox, s->oy, s->oz, PORTAL_DIR[axis][1]);

        if (s->width < 2 || s->width > 21)
        {
            s->has_origin = 0;
            s->width = 0;
        }
        else s->has_origin = 1;
    }

    if (s->has_origin) portal_frame_height(w, s, axis);
}

/* BlockPortal.Size.func_150860_b. */
static int portal_size_ok(const struct portal_size *s)
{
    return s->has_origin && s->width >= 2 && s->width <= 21 && s->height >= 3 && s->height <= 21;
}

/* BlockPortal.Size.func_150859_c: the portal blocks of the frame, meta = the
 * axis, flags 2. */
static void portal_size_place(struct world *w, const struct portal_size *s, int axis)
{
    BWL_EMIT(BWL_PORTAL_PLACE, w, 0, 0, 0, s->ox, s->oy, s->oz, s->width, s->height, axis);
}

/* the placement's writes, one per step, in the loops' order */
static int portal_place_step(struct bwl_frame *f)
{
    int axis = f->op.a[5], height = f->op.a[4];
    int dx = DIR_X[PORTAL_DIR[axis][1]], dz = DIR_Z[PORTAL_DIR[axis][1]];

    if (f->i >= f->op.a[3] * height) return 1;

    int i = f->i / height, j = f->i % height;

    ++f->i;
    SET_BLOCK(f->op.w, f->op.a[0] + dx * i, f->op.a[1] + j, f->op.a[2] + dz * i, ID_PORTAL, axis, 2);
    return f->i >= f->op.a[3] * height;
}

/* BlockPortal.func_150000_e. */
static int portal_try_create(struct world *w, int x, int y, int z)
{
    struct portal_size a, b;

    portal_size_init(w, &a, x, y, z, 1);

    if (portal_size_ok(&a) && a.existing == 0)
    {
        portal_size_place(w, &a, 1);
        return 1;
    }

    portal_size_init(w, &b, x, y, z, 2);

    if (portal_size_ok(&b) && b.existing == 0)
    {
        portal_size_place(w, &b, 2);
        return 1;
    }

    return 0;
}

/* World.doesBlockHaveSolidTopSurface: an opaque block that renders as a
 * normal cube, or one of the four half blocks whose top bit says top. */
static int solid_top_surface(struct world *w, int x, int y, int z)
{
    int b = block_at(w, x, y, z);
    int m = meta_at(w, x, y, z);

    if (MATERIALS[material_of(b)].is_opaque && BLOCKS[b & 4095].normal_block) return 1;

    if (is_class(b, BC_STAIRS)) return (m & 4) == 4;
    if (is_slab(b)) return (m & 8) == 8;
    if (is_class(b, BC_HOPPER)) return 1;
    if (is_class(b, BC_SNOW)) return (m & 7) == 7;

    return 0;
}

/* The ids BlockFire.func_149843_e gives a chance to encourage fire
 * (field_149849_a); func_149844_e is "the id has a chance". */
static int fire_encourages(int id)
{
    switch (id & 4095)
    {
    case 5:   /* planks */
    case 125: /* double_wooden_slab */
    case 126: /* wooden_slab */
    case 85:  /* fence */
    case 53:  /* oak_stairs */
    case 135: /* birch_stairs */
    case 134: /* spruce_stairs */
    case 136: /* jungle_stairs */
    case 17:  /* log */
    case 162: /* log2 */
    case 18:  /* leaves */
    case 161: /* leaves2 */
    case 47:  /* bookshelf */
    case 46:  /* tnt */
    case 31:  /* tallgrass */
    case 175: /* double_plant */
    case 37:  /* yellow_flower */
    case 38:  /* red_flower */
    case 35:  /* wool */
    case 106: /* vine */
    case 173: /* coal_block */
    case 170: /* hay_block */
    case 171: /* carpet */
        return 1;

    default:
        return 0;
    }
}

/* BlockFire.func_149847_e: a neighbour that encourages fire. */
static int fire_can_catch(struct world *w, int x, int y, int z)
{
    static const int off[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};

    for (int i = 0; i < 6; ++i)
        if (fire_encourages(block_at(w, x + off[i][0], y + off[i][1], z + off[i][2]))) return 1;

    return 0;
}

/* BlockFire.onBlockAdded. A fire that stays parks a scheduled tick at
 * 30 + world.rand.nextInt(10); the draw is the world's own Random, which the
 * probe world never reads again, so the delay value is never observed, and
 * the entry joins the pending list the same either way. */
#define fire_rand (nw_env->rng.fire)
#define fire_rand_ready (nw_env->rng.fire_ready)

static void fire_on_block_added(struct world *w, int x, int y, int z)
{
    if (w->dim > 0 || !portal_try_create(w, x, y, z))
    {
        if (!solid_top_surface(w, x, y - 1, z) && !fire_can_catch(w, x, y, z))
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        else
        {
            /* World.rand: an installed environment's stream (the explosion's
             * blockcb_env, a structure block half's), else the server tick's
             * stream while a tick runs (servertick.c publishes it), else the
             * feature probes' stand-in */
            jrand *wr = nw_env->blockcb.env.world_rand != NULL ? nw_env->blockcb.env.world_rand
                                                       : randomtick_tick_rand_stream();

            if (wr == NULL)
            {
                if (!fire_rand_ready)
                {
                    jr_seed(&fire_rand, 1L);
                    fire_rand_ready = 1;
                }

                wr = &fire_rand;
            }

            ticks_bwl_sched(w, x, y, z, ID_FIRE, 30 + jr_int_n(wr, 10));
        }
    }
}

/* ---------------------------------------------- the temple's redstone blocks
 *
 * The jungle pyramid's tripwires, hooks, redstone wire, repeater, levers and
 * dispensers all run their vanilla callbacks during generation even though
 * config.yaml keeps redstone off. No block ever receives power (World's two
 * power entry points answer 0), so the wire and the repeater never rewrite a
 * metadata for strength, but the tripwire and the hook do: their state is about
 * support and connectivity, not power. The dispenser's onBlockAdded rewrites
 * its facing from the neighbours, and every block that can lose its support
 * drops itself, which spawns an item entity (World.rand and Det draws). */

/* The support and registry helpers, defined beside the torch and rail drops
 * below; Direction.rotateOpposite. */
static void drop_item_stack(struct world *w, int x, int y, int z, int item, int damage);
static int normal_cube_default(struct world *w, int x, int y, int z, int def);
static int support_top(struct world *w, int x, int y, int z);

static const int DIR_ROT_OPP[4] = {2, 3, 0, 1};

/* World.func_147460_e: one neighbour's onNeighborBlockChange, the block and
 * metadata read when its turn comes. */
static void notify_one(struct world *w, int x, int y, int z, int source)
{
    BWL_EMIT(BWL_NEIGHBOR, w, x, y, z, source, 0);
}

/* World.func_147441_b: six neighbours, skipping one side (a Facing index;
 * the notification's own order is -x, +x, -y, +y, -z, +z). */
static void notify_all_but(struct world *w, int x, int y, int z, int source, int skip)
{
    static const int index_of_facing[6] = {2, 3, 4, 5, 0, 1};

    BWL_EMIT(BWL_NOTIFY, w, x, y, z, source, index_of_facing[skip]);
}

/* ---- BlockTripWireHook.func_150136_a, the hook's state machine ---- */

/* func_150135_a: the click. Only the last branch draws (the client's pitch);
 * the server's playSoundEffect is empty but the argument is still evaluated. */
static void hook_sound(struct world *w, int x, int y, int z, int connected, int active, int powered, int was_powered)
{
    (void)w;
    (void)x;
    (void)y;
    (void)z;

    /* the first three branches pass constant volumes and pitches; the fourth
     * (a wire that stopped pressing, random.bowhit) evaluates
     * 1.2F / (rand.nextFloat() * 0.2F + 0.9F) before the call */
    if (active && !was_powered) return;
    if (!active && was_powered) return;
    if (connected && !powered) return;
    if (!connected && powered && nw_env->blockcb.env.world_rand != NULL) jr_float(nw_env->blockcb.env.world_rand);
}

/* func_150134_a: the hook's own notification and the one beside its facing. */
static void hook_notify(struct world *w, int x, int y, int z, int dir)
{
    NOTIFY(w, x, y, z, ID_TRIPWIRE_HOOK);

    if (dir == 3) NOTIFY(w, x - 1, y, z, ID_TRIPWIRE_HOOK);
    else if (dir == 1) NOTIFY(w, x + 1, y, z, ID_TRIPWIRE_HOOK);
    else if (dir == 0) NOTIFY(w, x, y, z - 1, ID_TRIPWIRE_HOOK);
    else if (dir == 2) NOTIFY(w, x, y, z + 1, ID_TRIPWIRE_HOOK);
}

/* BlockTripWireHook.canPlaceBlockAt: a normal cube on one horizontal side. */
static int hook_can_place_at(struct world *w, int x, int y, int z)
{
    return normal_cube_default(w, x - 1, y, z, 1) || normal_cube_default(w, x + 1, y, z, 1)
        || normal_cube_default(w, x, y, z - 1, 1) || normal_cube_default(w, x, y, z + 1, 1);
}

/* func_150137_e: the support test that drops the hook. */
static int hook_stay(struct world *w, int x, int y, int z)
{
    if (!hook_can_place_at(w, x, y, z))
    {
        drop_item_stack(w, x, y, z, ITEM_TRIPWIRE_HOOK, 0);
        SET_BLOCK(w, x, y, z, 0, 0, 3);
        return 0;
    }

    return 1;
}

static void hook_update(struct world *w, int x, int y, int z, int removing, int meta, int can_update, int wire_pos,
                        int wire_meta)
{
    int dir = meta & 3;
    int powered = (meta & 4) == 4;
    int was_powered = (meta & 8) == 8;
    int connected = !removing;
    int found_active = 0;
    int suspended = !support_top(w, x, y - 1, z);
    int dx = DIR_X[dir];
    int dz = DIR_Z[dir];
    int wire_count = 0;
    int wires[42];
    int meta_new;

    memset(wires, 0, sizeof wires);

    for (int i = 1; i < 42; ++i)
    {
        int wx = x + dx * i, wz = z + dz * i;
        int id = block_at(w, wx, y, wz);

        if (id == ID_TRIPWIRE_HOOK)
        {
            int m = meta_at(w, wx, y, wz);

            if ((m & 3) == DIR_ROT_OPP[dir]) wire_count = i;

            break;
        }

        if (id != ID_TRIPWIRE && i != wire_pos)
        {
            wires[i] = -1;
            connected = 0;
        }
        else
        {
            int m = i == wire_pos ? wire_meta : meta_at(w, wx, y, wz);
            int unobstructed = (m & 8) != 8;
            int tripped = (m & 1) == 1;
            int wire_suspended = (m & 2) == 2;

            connected = connected && (wire_suspended == suspended);
            found_active = found_active || (unobstructed && tripped);
            wires[i] = m;

            if (i == wire_pos)
            {
                ticks_bwl_sched(w, x, y, z, ID_TRIPWIRE_HOOK, 10);
                connected = connected && unobstructed;
            }
        }
    }

    connected = connected && wire_count > 1;
    found_active = found_active && connected;

    meta_new = dir | (connected ? 4 : 0) | (found_active ? 8 : 0);

    if (wire_count > 0)
    {
        int hx = x + dx * wire_count, hz = z + dz * wire_count;
        int back = DIR_ROT_OPP[dir];

        SET_META_NOTIFY(w, hx, y, hz, back | (connected ? 4 : 0) | (found_active ? 8 : 0));
        hook_notify(w, hx, y, hz, back);
        BWL_EMIT(BWL_HOOK_SOUND, w, hx, y, hz, connected, found_active, powered, was_powered);
    }

    if (bwl_pending()) BWL_EMIT(BWL_HOOK_SOUND, w, x, y, z, connected, found_active, powered, was_powered);
    else hook_sound(w, x, y, z, connected, found_active, powered, was_powered);

    if (!removing)
    {
        SET_META_NOTIFY(w, x, y, z, meta_new);

        if (can_update) hook_notify(w, x, y, z, dir);
    }

    if (powered != connected)
    {
        for (int i = 1; i < wire_count; ++i)
        {
            int wx = x + dx * i, wz = z + dz * i;
            int m = wires[i];

            if (m < 0) continue;

            if (connected) m |= 4;
            else m &= -5;

            SET_META_NOTIFY(w, wx, y, wz, m);
        }
    }
}

/* BlockTripWireHook.onNeighborBlockChange. */
static void hook_neighbor(struct world *w, int x, int y, int z, int source)
{
    if (source == ID_TRIPWIRE_HOOK) return;
    if (!hook_stay(w, x, y, z)) return;

    int meta = meta_at(w, x, y, z);
    int dir = meta & 3;
    int unsupported = 0;

    if (!normal_cube_default(w, x - 1, y, z, 1) && dir == 3) unsupported = 1;
    if (!normal_cube_default(w, x + 1, y, z, 1) && dir == 1) unsupported = 1;
    if (!normal_cube_default(w, x, y, z - 1, 1) && dir == 0) unsupported = 1;
    if (!normal_cube_default(w, x, y, z + 1, 1) && dir == 2) unsupported = 1;

    if (unsupported)
    {
        drop_item_stack(w, x, y, z, ITEM_TRIPWIRE_HOOK, 0);
        SET_BLOCK(w, x, y, z, 0, 0, 3);
    }
}

/* ---- BlockTripWire ---- */

/* BlockTripWire.onNeighborBlockChange. */
static void tripwire_neighbor(struct world *w, int x, int y, int z)
{
    int meta = meta_at(w, x, y, z);
    int suspended = (meta & 2) == 2;
    int wants = !support_top(w, x, y - 1, z);

    if (suspended == wants) return;

    drop_item_stack(w, x, y, z, ITEM_STRING, 0);
    SET_BLOCK(w, x, y, z, 0, 0, 3);
}

/* BlockTripWire.onBlockAdded: the suspended bit from the floor, then the two
 * wire runs' hooks. */
static void tripwire_on_added(struct world *w, int x, int y, int z)
{
    int meta = support_top(w, x, y - 1, z) ? 0 : 2;

    SET_META_NOTIFY(w, x, y, z, meta);
    BWL_EMIT(BWL_TRIPWIRE_HOOKS, w, x, y, z, meta);
}

void blockcb_tripwire_hooks(struct world *w, int x, int y, int z, int meta)
{
    BWL_CALL(BWL_TRIPWIRE_HOOKS, w, x, y, z, meta);
}

void blockcb_tripwire_hook_tick(struct world *w, int x, int y, int z)
{
    if (block_at(w, x, y, z) != ID_TRIPWIRE_HOOK) return;
    BWL_CALL(BWL_HOOK_UPDATE, w, x, y, z, 0, meta_at(w, x, y, z), 1, -1, 0);
}

/* BlockTripWire.func_150138_a: the two directions' runs, up to 41 cells, each
 * facing hook's state machine called with the run length and this wire's
 * metadata. One direction per step. */
static int tripwire_hooks_step(struct bwl_frame *f)
{
    struct world *w = f->op.w;
    int x = f->op.x, y = f->op.y, z = f->op.z, meta = f->op.a[0];

    while (f->i < 2)
    {
        int dir = f->i++;
        int pos = 1;

        for (;;)
        {
            if (pos < 42)
            {
                int wx = x + DIR_X[dir] * pos, wz = z + DIR_Z[dir] * pos;
                int id = block_at(w, wx, y, wz);

                if (id == ID_TRIPWIRE_HOOK)
                {
                    int m = meta_at(w, wx, y, wz);

                    if ((m & 3) == DIR_ROT_OPP[dir])
                    {
                        BWL_EMIT(BWL_HOOK_UPDATE, w, wx, y, wz, 0, m, 1, pos, meta);
                        return f->i >= 2;
                    }
                }
                else if (id == ID_TRIPWIRE)
                {
                    ++pos;
                    continue;
                }
            }

            break;
        }
    }

    return 1;
}

/* ---- BlockRedstoneWire ---- */

/* BlockRedstoneWire's field_150179_b: the positions whose wire metadata moved in
 * the propagation in progress. Java copies the HashSet and clears it before it
 * notifies; with redstone off no wire ever changes its strength, so the set
 * stays empty and only the insertion-order difference a powered world would see
 * is left out. */
#define wire_changed (nw_env->blockcb_wire.wire_changed)
#define wire_nchanged (nw_env->blockcb_wire.wire_nchanged)

/* func_150178_a: the neighbour's strength, if it is a wire. */
static int wire_strength_at(struct world *w, int x, int y, int z, int best)
{
    if (block_at(w, x, y, z) != ID_REDSTONE_WIRE) return best;

    int m = meta_at(w, x, y, z);
    return m > best ? m : best;
}

/* func_150175_a: the strength the wire at (x,y,z) settles at, from the source
 * (sx,sy,sz), and the write when it moves. */
static void wire_update_strength(struct world *w, int x, int y, int z, int sx, int sy, int sz)
{
    int meta = meta_at(w, x, y, z);
    int strength = wire_strength_at(w, sx, sy, sz, 0);
    int direct = 0;    /* getStrongestIndirectPower, 0 with redstone off */
    int indirect = 0;

    if (direct > 0 && direct > strength - 1) strength = direct;

    for (int i = 0; i < 4; ++i)
    {
        int nx = x, nz = z;

        if (i == 0) nx = x - 1;
        if (i == 1) ++nx;
        if (i == 2) nz = z - 1;
        if (i == 3) ++nz;

        if (nx != sx || nz != sz) indirect = wire_strength_at(w, nx, y, nz, indirect);

        if (normal_cube_default(w, nx, y, nz, 1) && !normal_cube_default(w, x, y + 1, z, 1))
        {
            if ((nx != sx || nz != sz) && y >= sy) indirect = wire_strength_at(w, nx, y + 1, nz, indirect);
        }
        else if (!normal_cube_default(w, nx, y, nz, 1) && (nx != sx || nz != sz) && y <= sy)
        {
            indirect = wire_strength_at(w, nx, y - 1, nz, indirect);
        }
    }

    if (indirect > strength) strength = indirect - 1;
    else if (strength > 0) --strength;
    else strength = 0;

    if (direct > strength - 1) strength = direct;

    if (meta == strength) return;

    world_set_meta_quiet(w, x, y, z, strength);

    if (wire_nchanged < (int)(sizeof wire_changed / sizeof wire_changed[0]))
    {
        int n = wire_nchanged;

        wire_changed[n].x = x; wire_changed[n].y = y; wire_changed[n].z = z; ++n;
        wire_changed[n].x = x - 1; wire_changed[n].y = y; wire_changed[n].z = z; ++n;
        wire_changed[n].x = x + 1; wire_changed[n].y = y; wire_changed[n].z = z; ++n;
        wire_changed[n].x = x; wire_changed[n].y = y - 1; wire_changed[n].z = z; ++n;
        wire_changed[n].x = x; wire_changed[n].y = y + 1; wire_changed[n].z = z; ++n;
        wire_changed[n].x = x; wire_changed[n].y = y; wire_changed[n].z = z - 1; ++n;
        wire_changed[n].x = x; wire_changed[n].y = y; wire_changed[n].z = z + 1; ++n;
        wire_nchanged = n;
    }
}

/* func_150177_e: the wire's own strength, then every position whose wire moved
 * notifies its neighbours. */
static void wire_propagate(struct world *w, int x, int y, int z)
{
    struct { int x, y, z; } copy[256];
    int n;

    wire_update_strength(w, x, y, z, x, y, z);
    n = wire_nchanged;
    memcpy(copy, wire_changed, (size_t)n * sizeof copy[0]);
    wire_nchanged = 0;

    for (int i = 0; i < n; ++i) NOTIFY(w, copy[i].x, copy[i].y, copy[i].z, ID_REDSTONE_WIRE);
}

/* func_150172_m: if a wire sits here, it and its six neighbours are notified. */
static void wire_notify_around(struct world *w, int x, int y, int z)
{
    if (block_at(w, x, y, z) != ID_REDSTONE_WIRE) return;

    NOTIFY(w, x, y, z, ID_REDSTONE_WIRE);
    NOTIFY(w, x - 1, y, z, ID_REDSTONE_WIRE);
    NOTIFY(w, x + 1, y, z, ID_REDSTONE_WIRE);
    NOTIFY(w, x, y, z - 1, ID_REDSTONE_WIRE);
    NOTIFY(w, x, y, z + 1, ID_REDSTONE_WIRE);
    NOTIFY(w, x, y - 1, z, ID_REDSTONE_WIRE);
    NOTIFY(w, x, y + 1, z, ID_REDSTONE_WIRE);
}

/* BlockRedstoneWire.onBlockAdded. */
static void wire_on_added(struct world *w, int x, int y, int z)
{
    wire_propagate(w, x, y, z);
    NOTIFY(w, x, y + 1, z, ID_REDSTONE_WIRE);
    NOTIFY(w, x, y - 1, z, ID_REDSTONE_WIRE);
    BWL_EMIT(BWL_WIRE_AROUND, w, x - 1, y, z, 0);
    BWL_EMIT(BWL_WIRE_AROUND, w, x + 1, y, z, 0);
    BWL_EMIT(BWL_WIRE_AROUND, w, x, y, z - 1, 0);
    BWL_EMIT(BWL_WIRE_AROUND, w, x, y, z + 1, 0);
    BWL_EMIT(BWL_WIRE_DIAG, w, x, y, z, -1, 0);
    BWL_EMIT(BWL_WIRE_DIAG, w, x, y, z, 1, 0);
    BWL_EMIT(BWL_WIRE_DIAG, w, x, y, z, 0, -1);
    BWL_EMIT(BWL_WIRE_DIAG, w, x, y, z, 0, 1);
}

/* one of wire_on_added's four diagonal rounds: above the side when the side
 * is a normal cube, below it otherwise, read when its turn comes */
static void wire_diag(struct world *w, int x, int y, int z, int dx, int dz)
{
    if (normal_cube_default(w, x + dx, y, z + dz, 1)) wire_notify_around(w, x + dx, y + 1, z + dz);
    else wire_notify_around(w, x + dx, y - 1, z + dz);
}

/* BlockRedstoneWire.canPlaceBlockAt: a solid top surface below, or glowstone. */
static int wire_can_place_at(struct world *w, int x, int y, int z)
{
    return support_top(w, x, y - 1, z) || block_at(w, x, y - 1, z) == ID_GLOWSTONE;
}

/* BlockRedstoneWire.onNeighborBlockChange. */
static void wire_neighbor(struct world *w, int x, int y, int z)
{
    if (wire_can_place_at(w, x, y, z))
    {
        wire_propagate(w, x, y, z);
    }
    else
    {
        /* BlockRedstoneWire.getItemDropped is Items.redstone, damage 0 */
        drop_item_stack(w, x, y, z, ITEM_REDSTONE, 0);
        SET_BLOCK(w, x, y, z, 0, 0, 3);
    }
}

/* ---- BlockRedstoneDiode (the repeater) ---- */

/* func_149911_e: the neighbour the diode faces, then that neighbour's own six.
 * source is the diode's own block id, the argument Java passes as `this`. */
static void diode_notify(struct world *w, int x, int y, int z, int meta, int source)
{
    int d = meta & 3;

    if (d == 1)
    {
        notify_one(w, x + 1, y, z, source);
        notify_all_but(w, x + 1, y, z, source, 4);
    }
    else if (d == 3)
    {
        notify_one(w, x - 1, y, z, source);
        notify_all_but(w, x - 1, y, z, source, 5);
    }
    else if (d == 2)
    {
        notify_one(w, x, y, z + 1, source);
        notify_all_but(w, x, y, z + 1, source, 2);
    }
    else
    {
        notify_one(w, x, y, z - 1, source);
        notify_all_but(w, x, y, z - 1, source, 3);
    }
}

/* The diode's func_149911_e from outside the callbacks: the activation's
 * func_149972_c and BlockRedstoneComparator.breakBlock end in it. */
void blockcb_diode_notify(struct world *w, int x, int y, int z, int source)
{
    BWL_CALL(BWL_DIODE_NOTIFY, w, x, y, z, source);
}

/* BlockRedstoneDiode.onNeighborBlockChange: canBlockStay first (a solid floor),
 * then func_149897_b. The comparator's reads the container behind it
 * (comparator.c); the repeater's schedules only when its input (World's
 * indirect power, 0 with redstone off, or a wire's strength, which nothing
 * raises) disagrees with its powered state, which never happens here. */
static void diode_neighbor(struct world *w, int x, int y, int z, int source)
{
    if (!support_top(w, x, y - 1, z))
    {
        /* getItemDropped is Items.repeater or Items.comparator, damage 0; the
         * popped block itself is the notifications' source, as Java's `this` */
        int this_id = block_at(w, x, y, z);
        int comparator = this_id == ID_UNPOWERED_COMPARATOR || this_id == ID_POWERED_COMPARATOR;

        drop_item_stack(w, x, y, z, comparator ? ITEM_COMPARATOR : ITEM_REPEATER, 0);
        SET_BLOCK(w, x, y, z, 0, 0, 3);

        NOTIFY(w, x + 1, y, z, this_id);
        NOTIFY(w, x - 1, y, z, this_id);
        NOTIFY(w, x, y, z + 1, this_id);
        NOTIFY(w, x, y, z - 1, this_id);
        NOTIFY(w, x, y - 1, z, this_id);
        NOTIFY(w, x, y + 1, z, this_id);
        return;
    }

    (void)source;

    int id = block_at(w, x, y, z);

    if (comparator_is(id)) comparator_on_neighbor(w, x, y, z, id);
}

/* BlockButton.func_150042_a: the button's own six neighbours, then the
 * attached face's block, by orientation (everything but 1..4 is a floor or
 * ceiling button, whose attach block is below). */
static void button_notify(struct world *w, int x, int y, int z, int facing, int id)
{
    NOTIFY(w, x, y, z, id);

    if (facing == 1) NOTIFY(w, x - 1, y, z, id);
    else if (facing == 2) NOTIFY(w, x + 1, y, z, id);
    else if (facing == 3) NOTIFY(w, x, y, z - 1, id);
    else if (facing == 4) NOTIFY(w, x, y, z + 1, id);
    else NOTIFY(w, x, y - 1, z, id);
}

/* The activation's func_150042_a from outside the callbacks. */
void blockcb_button_notify(struct world *w, int x, int y, int z, int facing, int id)
{
    BWL_CALL(BWL_BUTTON_NOTIFY, w, x, y, z, facing, id);
}

/* BlockButton.onNeighborBlockChange: func_150044_m's canPlaceBlockAt first (a
 * normal cube on one horizontal side, the pop that takes a floor button), then
 * the facing's own support test. */
static void button_neighbor(struct world *w, int x, int y, int z)
{
    if (!normal_cube_default(w, x - 1, y, z, 1) && !normal_cube_default(w, x + 1, y, z, 1)
        && !normal_cube_default(w, x, y, z - 1, 1) && !normal_cube_default(w, x, y, z + 1, 1))
    {
        drop_item_stack(w, x, y, z, block_at(w, x, y, z), 0);
        SET_BLOCK(w, x, y, z, 0, 0, 3);
        return;
    }

    int var6 = meta_at(w, x, y, z) & 7;
    int var7 = 0;

    if (var6 == 1 && !normal_cube_default(w, x - 1, y, z, 1)) var7 = 1;
    else if (var6 == 2 && !normal_cube_default(w, x + 1, y, z, 1)) var7 = 1;
    else if (var6 == 3 && !normal_cube_default(w, x, y, z - 1, 1)) var7 = 1;
    else if (var6 == 4 && !normal_cube_default(w, x, y, z + 1, 1)) var7 = 1;

    if (var7)
    {
        drop_item_stack(w, x, y, z, block_at(w, x, y, z), 0);
        SET_BLOCK(w, x, y, z, 0, 0, 3);
    }
}

/* ---- BlockLever ---- */

/* BlockLever.canPlaceBlockAt, the support per facing (bits 0..7). */
static int lever_can_place_at(struct world *w, int x, int y, int z)
{
    int m = meta_at(w, x, y, z) & 7;

    if (m == 1) return normal_cube_default(w, x - 1, y, z, 1);
    if (m == 2) return normal_cube_default(w, x + 1, y, z, 1);
    if (m == 3) return normal_cube_default(w, x, y, z - 1, 1);
    if (m == 4) return normal_cube_default(w, x, y, z + 1, 1);
    if (m == 5 || m == 6) return support_top(w, x, y - 1, z);
    if (m == 0 || m == 7) return normal_cube_default(w, x, y + 1, z, 1);

    return 0;
}

/* BlockLever.onNeighborBlockChange: func_149820_e (the support test that drops
 * the lever), then the facing's own extra test. */
static void lever_neighbor(struct world *w, int x, int y, int z)
{
    if (!lever_can_place_at(w, x, y, z))
    {
        drop_item_stack(w, x, y, z, ID_LEVER, 0);
        SET_BLOCK(w, x, y, z, 0, 0, 3);
        return;
    }

    int m = meta_at(w, x, y, z) & 7;
    int drop = 0;

    if (!normal_cube_default(w, x - 1, y, z, 1) && m == 1) drop = 1;
    if (!normal_cube_default(w, x + 1, y, z, 1) && m == 2) drop = 1;
    if (!normal_cube_default(w, x, y, z - 1, 1) && m == 3) drop = 1;
    if (!normal_cube_default(w, x, y, z + 1, 1) && m == 4) drop = 1;
    if (!support_top(w, x, y - 1, z) && m == 5) drop = 1;
    if (!support_top(w, x, y - 1, z) && m == 6) drop = 1;
    if (!normal_cube_default(w, x, y + 1, z, 1) && m == 0) drop = 1;
    if (!normal_cube_default(w, x, y + 1, z, 1) && m == 7) drop = 1;

    if (drop)
    {
        drop_item_stack(w, x, y, z, ID_LEVER, 0);
        SET_BLOCK(w, x, y, z, 0, 0, 3);
    }
}

/* ---- BlockDispenser ---- */

/* Block.func_149730_j(): the block's own `opaque` flag, which the constructor
 * sets to isOpaqueCube() and only the double slab overrides (BlockSlab forces
 * it true); the dispenser's facing test is the only caller here. */
static int block_opaque(int id)
{
    id &= 4095;
    return BLOCKS[id].opaque_cube || id == 43 || id == 125;   /* double slabs */
}

/* BlockDispenser.func_149938_m: the facing the four neighbours call for. */
static void dispenser_facing(struct world *w, int x, int y, int z)
{
    int back = block_at(w, x, y, z - 1);
    int front = block_at(w, x, y, z + 1);
    int side1 = block_at(w, x - 1, y, z);
    int side2 = block_at(w, x + 1, y, z);
    int facing = 3;

    if (block_opaque(back) && !block_opaque(front)) facing = 3;
    if (block_opaque(front) && !block_opaque(back)) facing = 2;
    if (block_opaque(side1) && !block_opaque(side2)) facing = 5;
    if (block_opaque(side2) && !block_opaque(side1)) facing = 4;

    world_set_meta_quiet(w, x, y, z, facing);
}


/* --------------------------------------------------------------- dispatch */
/* World.isBlockIndirectlyGettingPowered reads the six neighbours' power
 * outputs. No block the probes place or the terrain generates can provide
 * power (the shape table is slabs, stairs, fences, panes, walls, snow, cactus,
 * soul sand, farmland, ladders, vines, web, carpet, trapdoors, fence gates,
 * beds, cake, glass, stone, water, lava, sponge, lily pads, snow and air), so
 * the registry's canProvidePower flag decides it; a world with redstone needs
 * the isProvidingWeakPower / isProvidingStrongPower pair ported. */
static int is_indirectly_powered(struct world *w, int x, int y, int z)
{
    (void)w;
    (void)x;
    (void)y;
    (void)z;

    /* Every power input World.isBlockIndirectlyGettingPowered reads runs
     * through World.isBlockProvidingPowerTo, which a redstone-off world
     * shorts to 0; no block these callbacks can touch provides power either.
     * A world with redstone needs the isProvidingWeakPower /
     * isProvidingStrongPower pair ported. */
    return 0;
}

static int source_provides_power(int source)
{
    return BLOCKS[source & 4095].provides_power;
}

/* -------------------------------------------------- torch and rail support
 *
 * The two blocks whose support test can drop them, and with the drop an item
 * entity and World.rand draws. Both bodies run while a structure block half's
 * environment is installed (blockcb_env); with none, the metadata moves and
 * the support still tests, but the drop draws nothing, as in a world whose
 * Random the probe does not replay. */

/* World.isBlockNormalCubeDefault: the loaded chunk's own value, the material
 * being opaque and the block rendering as a normal cube. The chunk comes from
 * ChunkProviderServer.provideChunk, which with loadChunkOnProvideRequest off
 * returns null instead of generating. */
static int normal_cube_default(struct world *w, int x, int y, int z, int def)
{
    if (x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000) return def;

    struct chunk *c = w->no_generate ? world_chunk(w, x >> 4, z >> 4) : world_load_chunk(w, x >> 4, z >> 4);

    if (c == NULL || c->mask == 0) return def;

    int id = block_at(w, x, y, z);

    return (int)(MATERIALS[material_of(id)].is_opaque && BLOCKS[id].normal_block);
}

/* World.doesBlockHaveSolidTopSurface. */
static int support_top(struct world *w, int x, int y, int z)
{
    int id = block_at(w, x, y, z);
    const struct block_def *b = &BLOCKS[id & 4095];

    if (MATERIALS[b->material].is_opaque && b->normal_block) return 1;

    int meta = meta_at(w, x, y, z);

    if (is_class(id, BC_STAIRS)) return (meta & 4) == 4;
    if (is_class(id, BC_STONESLAB) || is_class(id, BC_WOODSLAB)) return (meta & 8) == 8;
    if (is_class(id, BC_HOPPER)) return 1;
    if (is_class(id, BC_SNOW)) return (meta & 7) == 7;
    return 0;
}

/* drop_item_stack with the stack damage spelled out: the door drops a door
 * item with damageDropped 0, the torch and the rail keep the meta they
 * carried. Block.dropBlockAsItem_do: the chance float and the three position
 * jitters from World.rand, then the EntityItem constructor's draws, the Entity
 * boilerplate (id, rand, UUID) and its four Math.random draws (hoverStart,
 * rotationYaw, motionX, motionZ). The chance float belongs to
 * dropBlockAsItemWithChance, which wraps this; dropBlockAsItem_do calls it
 * bare. */
/* The EntityItem dropBlockAsItem_do builds at a jittered position: the Entity
 * boilerplate (id, rand, UUID) and its four Math.random draws (hoverStart,
 * rotationYaw, motionX, motionZ), into the environment's sink. */
void blockcb_env_drop_sink(void *ctx, double x, double y, double z, int item, int damage, int count)
{
    (void)ctx;
    int eid = det_next_entity_id_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role);
    det_rng erand = det_new_random_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role);
    uint64_t rand_state = det_rng_state(&erand);
    int64_t msb, lsb;
    det_uuid_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role, &msb, &lsb);

    float hover = (float)(det_math_random_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role) * 3.141592653589793 * 2.0);
    float yaw = (float)(det_math_random_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role) * 360.0);
    double mx = (double)(float)(det_math_random_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role) * 0.20000000298023224 - 0.10000000149011612);
    double mz = (double)(float)(det_math_random_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role) * 0.20000000298023224 - 0.10000000149011612);

    nw_env->blockcb.env.item_drop(nw_env->blockcb.env.ctx, eid, rand_state, msb, lsb, x, y, z, yaw, hover, mx, mz, item, damage, count);
}

static void drop_item_spawn(struct world *w, int x, int y, int z, int item, int damage)
{
    bwl_quiet();

    if (nw_env->blockcb.env.world_rand == NULL || nw_env->blockcb.env.det == NULL || nw_env->blockcb.env.item_drop == NULL)
    {
        /* a scheduled or random tick (a flow replacing a flower pot, a
         * skull): the tick's published World.rand and its drop sink */
        if (randomtick_tick_rand_stream() != NULL)
        {
            randomtick_drop_stack_do(x, y, z, item, damage);
            return;
        }

        /* outside a tick environment (the placement probe): the draws come
         * from the case's world Random and Math.random */
        place_item_drop(w, x, y, z, item, damage);
        return;
    }

    float fx = jr_float(nw_env->blockcb.env.world_rand);
    float fy = jr_float(nw_env->blockcb.env.world_rand);
    float fz = jr_float(nw_env->blockcb.env.world_rand);
    double jx = (double)(fx * 0.7f) + (double)(1.0f - 0.7f) * 0.5;
    double jy = (double)(fy * 0.7f) + (double)(1.0f - 0.7f) * 0.5;
    double jz = (double)(fz * 0.7f) + (double)(1.0f - 0.7f) * 0.5;

    int eid = det_next_entity_id_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role);
    det_rng erand = det_new_random_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role);
    uint64_t rand_state = det_rng_state(&erand);
    int64_t msb, lsb;
    det_uuid_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role, &msb, &lsb);

    float hover = (float)(det_math_random_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role) * 3.141592653589793 * 2.0);
    float yaw = (float)(det_math_random_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role) * 360.0);
    double mx = (double)(float)(det_math_random_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role) * 0.20000000298023224 - 0.10000000149011612);
    double mz = (double)(float)(det_math_random_role(nw_env->blockcb.env.det, nw_env->blockcb.env.role) * 0.20000000298023224 - 0.10000000149011612);

    nw_env->blockcb.env.item_drop(nw_env->blockcb.env.ctx, eid, rand_state, msb, lsb, (double)x + jx, (double)y + jy, (double)z + jz,
                          yaw, hover, mx, mz, item, damage, 1);
}

static void drop_item_stack(struct world *w, int x, int y, int z, int item, int damage)
{
    /* inside a tick (the spawner's torch) the drop belongs to the tick's own
     * sink: World.rand's chance float and position jitters, then the
     * EntityItem the sink builds. The torch is the only caller this reaches
     * today and its drop is Block's own (the block's item, damage 0), the
     * case break_draws already carries. */
    if (nw_env->blockcb.env.world_rand == NULL || nw_env->blockcb.env.det == NULL || nw_env->blockcb.env.item_drop == NULL)
    {
        /* a scheduled or random tick: the chance float on the tick's
         * published World.rand, then the spawn on the same stream */
        jrand *tr = randomtick_tick_rand_stream();

        if (tr != NULL && jr_float(tr) > 1.0f) return;

        drop_item_spawn(w, x, y, z, item, damage);
        return;
    }

    /* Block.dropBlockAsItemWithChance: quantityDroppedWithBonus(fortune 0)
     * draws nothing, the chance float draws once */
    if (jr_float(nw_env->blockcb.env.world_rand) > 1.0f) return;

    drop_item_spawn(w, x, y, z, item, damage);
}

/* Block.dropBlockAsItem_do: the plain spawn, no chance float. */
static void drop_item_stack_do(struct world *w, int x, int y, int z, int item, int damage)
{
    drop_item_spawn(w, x, y, z, item, damage);
}

/* Block.dropBlockAsItem_do: the two blocks this file's own drops use, whose
 * dropped item is their own id and whose damage is Block.damageDropped's 0
 * (BlockTorch and BlockRailBase do not override it). */
static void drop_as_item(struct world *w, int x, int y, int z, int meta, int id)
{
    (void)meta;
    drop_item_stack(w, x, y, z, id, 0);
}

/* BlockDoor.func_150014_a: flip the open bit of the pair's lower meta, flag 2
 * at the half the call named. With redstone off it fires when a neighbour
 * that canProvidePower (a button, a lever, a plate, a torch placed or
 * removed beside it) changes: the door closes (cov-blkpower-s1). */
static void door_set_open(struct world *w, int x, int y, int z, int open)
{
    int m = world_get_meta(w, x, y, z);
    int upper = (m & 8) != 0;
    int lower_meta = upper ? world_get_meta(w, x, y - 1, z) : m;
    int upper_meta = upper ? m : world_get_meta(w, x, y + 1, z);
    int var6 = (lower_meta & 7) | (upper ? 8 : 0) | ((upper_meta & 1) != 0 ? 16 : 0);

    if (((var6 & 4) != 0) == open) return;

    int var8 = (var6 & 7) ^ 4;

    if ((var6 & 8) == 0) world_set_meta_quiet(w, x, y, z, var8);
    else world_set_meta_quiet(w, x, y - 1, z, var8);
    env_aux_sfx(w, 1003, x, y, z, 0);       /* playAuxSFXAtEntity(null, 1003) at the called half */
}

/* BlockDoor.onNeighborBlockChange. Nothing is ever powered, so the branch
 * runs func_150014_a(false) when the changed neighbour canProvidePower: an
 * open door closes. The drop is BlockDoor.dropBlockAsItem: the
 * door item from the lower half's meta, damageDropped 0. The wooden and the
 * iron door share it. */
static int door_step(struct bwl_frame *f)
{
    struct world *w = f->op.w;
    int x = f->op.x, y = f->op.y, z = f->op.z, source = f->op.a[0], self = f->op.a[1];
    int *popped = &f->u.l[0];

    switch (f->pc)
    {
    case 0:
        if ((world_get_meta(w, x, y, z) & 8) != 0)
        {
            /* the upper half: its lower half gone takes it, then the lower
             * half's own check */
            f->pc = 10;
            if (block_at(w, x, y - 1, z) != self)
            {
                SET_BLOCK(w, x, y, z, 0, 0, 3);
                return 0;
            }
            goto upper_tail;
        }

        *popped = 0;
        f->pc = 1;

        if (block_at(w, x, y + 1, z) != self)
        {
            SET_BLOCK(w, x, y, z, 0, 0, 3);
            *popped = 1;
            return 0;
        }
        /* fall through */

    case 1:
        f->pc = 3;

        if (!solid_top_surface(w, x, y - 1, z))
        {
            SET_BLOCK(w, x, y, z, 0, 0, 3);
            *popped = 1;
            f->pc = 2;
            return 0;
        }

        goto lower_tail;

    case 2:
        f->pc = 3;

        if (block_at(w, x, y + 1, z) == self)
        {
            SET_BLOCK(w, x, y + 1, z, 0, 0, 3);
            return 0;
        }
        /* fall through */

    case 3:
    lower_tail:
        if (*popped)
        {
            /* getItemDropped: Items.iron_door for the iron material, else
             * Items.wooden_door (ItemDoor, not an ItemBlock) */
            drop_item_stack(w, x, y, z, self == 71 ? 330 : 324, 0);
        }
        else
        {
            int powered = is_indirectly_powered(w, x, y, z) || is_indirectly_powered(w, x, y + 1, z);

            if ((powered || source_provides_power(source)) && source != self)
            {
                door_set_open(w, x, y, z, powered);
            }
        }

        return 1;

    case 10:
    default:
    upper_tail:
        if (source != self) BWL_EMIT(BWL_DOOR, w, x, y - 1, z, source, self);
        return 1;
    }
}

/* `this` is the wooden or the iron door the cell holds */
static void door_neighbor(struct world *w, int x, int y, int z, int source)
{
    BWL_EMIT(BWL_DOOR, w, x, y, z, source, block_at(w, x, y, z));
}

/* func_150107_m: a floor a torch can hang from. */
static int torch_below_ok(struct world *w, int x, int y, int z)
{
    if (support_top(w, x, y, z)) return 1;

    int b = block_at(w, x, y, z);
    return b == ID_FENCE || b == ID_NETHER_BRICK_FENCE || b == ID_GLASS || b == ID_COBBLE_WALL;
}

/* BlockTorch.canPlaceBlockAt. */
static int torch_can_place(struct world *w, int x, int y, int z)
{
    if (normal_cube_default(w, x - 1, y, z, 1)) return 1;
    if (normal_cube_default(w, x + 1, y, z, 1)) return 1;
    if (normal_cube_default(w, x, y, z - 1, 1)) return 1;
    if (normal_cube_default(w, x, y, z + 1, 1)) return 1;
    return torch_below_ok(w, x, y - 1, z);
}

/* func_150109_e: drop and remove the torch when it cannot stay; 1 when it
 * stays. */
static int torch_stay(struct world *w, int x, int y, int z)
{
    if (!torch_can_place(w, x, y, z))
    {
        if (block_at(w, x, y, z) == ID_TORCH || block_at(w, x, y, z) == 76)
        {
            drop_as_item(w, x, y, z, meta_at(w, x, y, z), block_at(w, x, y, z));
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        }

        return 0;
    }

    return 1;
}

static void torch_on_added(struct world *w, int x, int y, int z)
{
    if (meta_at(w, x, y, z) == 0)
    {
        if (normal_cube_default(w, x - 1, y, z, 1)) world_set_meta_quiet(w, x, y, z, 1);
        else if (normal_cube_default(w, x + 1, y, z, 1)) world_set_meta_quiet(w, x, y, z, 2);
        else if (normal_cube_default(w, x, y, z - 1, 1)) world_set_meta_quiet(w, x, y, z, 3);
        else if (normal_cube_default(w, x, y, z + 1, 1)) world_set_meta_quiet(w, x, y, z, 4);
        else if (torch_below_ok(w, x, y - 1, z)) world_set_meta_quiet(w, x, y, z, 5);
    }

    /* BlockRedstoneTorch.onBlockAdded: the lit torch runs a full neighbour
     * round around each of its six neighbours (field_150113_a is true for
     * it; the reads change nothing in a redstone-off world, but the round
     * still reaches the static liquids) */
    if (block_at(w, x, y, z) == 76)
    {
        NOTIFY(w, x, y - 1, z, 76);
        NOTIFY(w, x, y + 1, z, 76);
        NOTIFY(w, x - 1, y, z, 76);
        NOTIFY(w, x + 1, y, z, 76);
        NOTIFY(w, x, y, z - 1, 76);
        NOTIFY(w, x, y, z + 1, 76);
        BWL_EMIT(BWL_TORCH_STAY, w, x, y, z, 0);
        return;
    }

    torch_stay(w, x, y, z);
}

/* BlockFurnace.func_149930_e: the facing a new furnace takes from its four
 * horizontal neighbours, written as a metadata change with flag 2
 * (Chunk.setBlockMetadata reports the change; no neighbour notify). */
static void furnace_on_block_added(struct world *w, int x, int y, int z)
{
    /* func_149930_e reads the block at z - 1 first (var5), then z + 1, x - 1
     * and x + 1 */
    int back = BLOCKS[block_at(w, x, y, z - 1) & 4095].opaque_cube;
    int front = BLOCKS[block_at(w, x, y, z + 1) & 4095].opaque_cube;
    int side1 = BLOCKS[block_at(w, x - 1, y, z) & 4095].opaque_cube;
    int side2 = BLOCKS[block_at(w, x + 1, y, z) & 4095].opaque_cube;
    int facing = 3;

    if (back && !front) facing = 3;
    if (front && !back) facing = 2;
    if (side1 && !side2) facing = 5;
    if (side2 && !side1) facing = 4;

    world_set_meta_quiet(w, x, y, z, facing);
}

/* BlockTorch.updateTick (inherited by BlockRedstoneTorch's super call): a
 * meta-0 torch runs its onBlockAdded round; a placed wall torch (meta 1..5)
 * ticks without effect. The redstone torch's own state machine reads power
 * this world never provides and writes nothing there. */
void block_torch_update_tick(struct world *w, int x, int y, int z)
{
    if (meta_at(w, x, y, z) == 0) BWL_CALL(BWL_TORCH_ADDED, w, x, y, z, 0);
}

/* func_150108_b, onNeighborBlockChange. */
static void torch_neighbor(struct world *w, int x, int y, int z)
{
    if (!torch_stay(w, x, y, z)) return;

    int m = meta_at(w, x, y, z);

    if ((!normal_cube_default(w, x - 1, y, z, 1) && m == 1)
        || (!normal_cube_default(w, x + 1, y, z, 1) && m == 2)
        || (!normal_cube_default(w, x, y, z - 1, 1) && m == 3)
        || (!normal_cube_default(w, x, y, z + 1, 1) && m == 4)
        || (!torch_below_ok(w, x, y - 1, z) && m == 5))
    {
        drop_as_item(w, x, y, z, meta_at(w, x, y, z), block_at(w, x, y, z));
        SET_BLOCK(w, x, y, z, 0, 0, 3);
    }
}

/* ------------------------------------------------------------ the Rail machine
 *
 * BlockRailBase.Rail, the shape a plain rail takes from its neighbours. Only
 * the plain rail reaches here in the world the probes build, but the machine
 * below carries the field_150656_f (powered) branch as the class does. */

struct rail_pt { int x, y, z; };

struct rail {
    struct world *w;
    int x, y, z;
    int powered;              /* field_150656_f */
    struct rail_pt link[8];   /* field_150657_g; a reshape adds one before the prune */
    int nlink;
};

static int is_rail_block(int id)
{
    return bc_rail[id & 4095];
}

/* BlockRailBase.field_150053_a: the golden, detector and activator rails, the
 * three whose onBlockAdded runs a neighbour check of their own and whose
 * breakBlock tells the block below. */
static int is_rail_powered_form(int id)
{
    return is_class(id, BC_RAILPOWERED) || is_class(id, BC_RAILDETECTOR);
}

/* func_150049_b_, isRailBlock over an IBlockAccess. */
static int rail_is_at(struct world *w, int x, int y, int z)
{
    return is_rail_block(block_at(w, x, y, z));
}

/* func_150648_a: the two positions a shape connects to. */
static void rail_adjacency(struct rail *r, int shape)
{
    r->nlink = 0;

    switch (shape)
    {
    case 0:
        r->link[r->nlink++] = (struct rail_pt){r->x, r->y, r->z - 1};
        r->link[r->nlink++] = (struct rail_pt){r->x, r->y, r->z + 1};
        break;

    case 1:
        r->link[r->nlink++] = (struct rail_pt){r->x - 1, r->y, r->z};
        r->link[r->nlink++] = (struct rail_pt){r->x + 1, r->y, r->z};
        break;

    case 2:
        r->link[r->nlink++] = (struct rail_pt){r->x - 1, r->y, r->z};
        r->link[r->nlink++] = (struct rail_pt){r->x + 1, r->y + 1, r->z};
        break;

    case 3:
        r->link[r->nlink++] = (struct rail_pt){r->x - 1, r->y + 1, r->z};
        r->link[r->nlink++] = (struct rail_pt){r->x + 1, r->y, r->z};
        break;

    case 4:
        r->link[r->nlink++] = (struct rail_pt){r->x, r->y + 1, r->z - 1};
        r->link[r->nlink++] = (struct rail_pt){r->x, r->y, r->z + 1};
        break;

    case 5:
        r->link[r->nlink++] = (struct rail_pt){r->x, r->y, r->z - 1};
        r->link[r->nlink++] = (struct rail_pt){r->x, r->y + 1, r->z + 1};
        break;

    case 6:
        r->link[r->nlink++] = (struct rail_pt){r->x + 1, r->y, r->z};
        r->link[r->nlink++] = (struct rail_pt){r->x, r->y, r->z + 1};
        break;

    case 7:
        r->link[r->nlink++] = (struct rail_pt){r->x - 1, r->y, r->z};
        r->link[r->nlink++] = (struct rail_pt){r->x, r->y, r->z + 1};
        break;

    case 8:
        r->link[r->nlink++] = (struct rail_pt){r->x - 1, r->y, r->z};
        r->link[r->nlink++] = (struct rail_pt){r->x, r->y, r->z - 1};
        break;

    case 9:
        r->link[r->nlink++] = (struct rail_pt){r->x + 1, r->y, r->z};
        r->link[r->nlink++] = (struct rail_pt){r->x, r->y, r->z - 1};
        break;
    }
}

/* the Rail(World, x, y, z) constructor. field_150656_f is BlockRailBase's
 * field_150053_a: the golden, detector and activator rails carry the power
 * bit (masked off before the adjacency is built) and reshape without curves
 * (func_150645_c and func_150655_a skip the 6..9 shapes for them). */
static void rail_init(struct rail *r, struct world *w, int x, int y, int z)
{
    r->w = w;
    r->x = x;
    r->y = y;
    r->z = z;
    r->powered = is_rail_powered_form(block_at(w, x, y, z));

    int meta = meta_at(w, x, y, z);

    if (r->powered) meta &= ~8;
    rail_adjacency(r, meta);
}

/* func_150654_a: a Rail at the position, or one above or below it. */
static int rail_pick(struct world *w, int x, int y, int z, struct rail *out)
{
    if (rail_is_at(w, x, y, z)) { rail_init(out, w, x, y, z); return 1; }
    if (rail_is_at(w, x, y + 1, z)) { rail_init(out, w, x, y + 1, z); return 1; }
    if (rail_is_at(w, x, y - 1, z)) { rail_init(out, w, x, y - 1, z); return 1; }
    return 0;
}

/* func_150653_a: a link matches the other rail's x and z. */
static int rail_connects(const struct rail *r, const struct rail *other)
{
    for (int i = 0; i < r->nlink; ++i)
        if (r->link[i].x == other->x && r->link[i].z == other->z) return 1;

    return 0;
}

/* func_150652_b: a link matches a position's x and z. */
static int rail_link_at(const struct rail *r, int x, int z)
{
    for (int i = 0; i < r->nlink; ++i)
        if (r->link[i].x == x && r->link[i].z == z) return 1;

    return 0;
}

/* func_150651_b: keep only the links a rail still sits at and that connect
 * back. */
static void rail_refresh(struct rail *r)
{
    for (int i = 0; i < r->nlink; ++i)
    {
        struct rail other;

        if (rail_pick(r->w, r->link[i].x, r->link[i].y, r->link[i].z, &other)
            && rail_connects(&other, r))
        {
            r->link[i].x = other.x;
            r->link[i].y = other.y;
            r->link[i].z = other.z;
        }
        else
        {
            r->link[i] = r->link[r->nlink - 1];
            --r->nlink;
            --i;
        }
    }
}

/* func_150649_b. */
static int rail_takes(const struct rail *r, const struct rail *other)
{
    return rail_connects(r, other) || r->nlink != 2;
}

/* func_150647_c: the rail at (x, y, z), or above or below, still takes this
 * rail as a connection. */
static int rail_linked_takes(struct rail *r, int x, int y, int z)
{
    struct rail other;

    if (!rail_pick(r->w, x, y, z, &other)) return 0;

    rail_refresh(&other);
    return rail_takes(&other, r);
}

/* func_150645_c: reshape this rail toward the other, metadata flags 3. */
static void rail_toward(struct rail *r, const struct rail *other)
{
    r->link[r->nlink].x = other->x;
    r->link[r->nlink].y = other->y;
    r->link[r->nlink].z = other->z;
    ++r->nlink;

    int zn = rail_link_at(r, r->x, r->z - 1), zp = rail_link_at(r, r->x, r->z + 1);
    int xn = rail_link_at(r, r->x - 1, r->z), xp = rail_link_at(r, r->x + 1, r->z);
    int shape = -1;

    if (zn || zp) shape = 0;
    if (xn || xp) shape = 1;

    if (!r->powered)
    {
        if (zp && xp && !zn && !xn) shape = 6;
        if (zp && xn && !zn && !xp) shape = 7;
        if (zn && xn && !zp && !xp) shape = 8;
        if (zn && xp && !zp && !xn) shape = 9;
    }

    if (shape == 0)
    {
        if (rail_is_at(r->w, r->x, r->y + 1, r->z - 1)) shape = 4;
        if (rail_is_at(r->w, r->x, r->y + 1, r->z + 1)) shape = 5;
    }

    if (shape == 1)
    {
        if (rail_is_at(r->w, r->x + 1, r->y + 1, r->z)) shape = 2;
        if (rail_is_at(r->w, r->x - 1, r->y + 1, r->z)) shape = 3;
    }

    if (shape < 0) shape = 0;

    int meta = shape;

    if (r->powered) meta = (meta_at(r->w, r->x, r->y, r->z) & 8) | shape;
    SET_META_NOTIFY(r->w, r->x, r->y, r->z, meta);
}

/* func_150655_a up to its write: 1 when the rail writes its shape (then the
 * links' reshape follows, rail_shape_step), 0 when it keeps it. */
static int rail_shape(struct rail *r, int powered, int place)
{
    int zn = rail_linked_takes(r, r->x, r->y, r->z - 1);
    int zp = rail_linked_takes(r, r->x, r->y, r->z + 1);
    int xn = rail_linked_takes(r, r->x - 1, r->y, r->z);
    int xp = rail_linked_takes(r, r->x + 1, r->y, r->z);
    int shape = -1;

    if ((zn || zp) && !xn && !xp) shape = 0;
    if ((xn || xp) && !zn && !zp) shape = 1;

    if (!r->powered)
    {
        if (zp && xp && !zn && !xn) shape = 6;
        if (zp && xn && !zn && !xp) shape = 7;
        if (zn && xn && !zp && !xp) shape = 8;
        if (zn && xp && !zp && !xn) shape = 9;
    }

    if (shape == -1)
    {
        if (zn || zp) shape = 0;
        if (xn || xp) shape = 1;

        if (!r->powered)
        {
            if (powered)
            {
                if (zp && xp) shape = 6;
                if (xn && zp) shape = 7;
                if (xp && zn) shape = 9;
                if (zn && xn) shape = 8;
            }
            else
            {
                if (zn && xn) shape = 8;
                if (xp && zn) shape = 9;
                if (xn && zp) shape = 7;
                if (zp && xp) shape = 6;
            }
        }
    }

    if (shape == 0)
    {
        if (rail_is_at(r->w, r->x, r->y + 1, r->z - 1)) shape = 4;
        if (rail_is_at(r->w, r->x, r->y + 1, r->z + 1)) shape = 5;
    }

    if (shape == 1)
    {
        if (rail_is_at(r->w, r->x + 1, r->y + 1, r->z)) shape = 2;
        if (rail_is_at(r->w, r->x - 1, r->y + 1, r->z)) shape = 3;
    }

    if (shape < 0) shape = 0;
    rail_adjacency(r, shape);

    int meta = shape;

    if (r->powered) meta = (meta_at(r->w, r->x, r->y, r->z) & 8) | shape;

    if (place || meta_at(r->w, r->x, r->y, r->z) != meta)
    {
        SET_META_NOTIFY(r->w, r->x, r->y, r->z, meta);
        return 1;
    }

    return 0;
}

/* func_150052_a: the Rail at the position and its func_150655_a. The Rail
 * lives in the frame; after its own write, each link's rail that takes it
 * turns toward it, one per step. */
_Static_assert(sizeof(struct rail) <= sizeof(((struct bwl_frame *)0)->u), "a Rail fits a frame's locals");

static int rail_reshape_step(struct bwl_frame *f)
{
    struct rail *r = (struct rail *)(void *)f->u.l;

    if (f->pc == 0)
    {
        rail_init(r, f->op.w, f->op.x, f->op.y, f->op.z);

        if (!rail_shape(r, is_indirectly_powered(f->op.w, f->op.x, f->op.y, f->op.z), f->op.a[0])) return 1;

        f->pc = 1;
        return 0;
    }

    while (f->i < r->nlink)
    {
        int i = f->i++;
        struct rail other;

        if (rail_pick(r->w, r->link[i].x, r->link[i].y, r->link[i].z, &other))
        {
            rail_refresh(&other);

            if (rail_takes(&other, r))
            {
                rail_toward(&other, r);
                return 0;
            }
        }
    }

    return 1;
}

/* BlockRailBase.onNeighborBlockChange's support half: the column each ascend
 * shape climbs must have a solid top, and so must the block below. */
static int rail_support_holds(struct world *w, int x, int y, int z)
{
    int var7 = meta_at(w, x, y, z);

    if (is_rail_powered_form(block_at(w, x, y, z) & 4095)) var7 &= 7;

    if (!support_top(w, x, y - 1, z)) return 0;
    if (var7 == 2 && !support_top(w, x + 1, y, z)) return 0;
    if (var7 == 3 && !support_top(w, x - 1, y, z)) return 0;
    if (var7 == 4 && !support_top(w, x, y, z - 1)) return 0;
    if (var7 == 5 && !support_top(w, x, y, z + 1)) return 0;

    return 1;
}

/* func_150650_a over the world: the four side columns that hold a rail at,
 * one above or one below the rail's height. */
static int rail_side_count(struct world *w, int x, int y, int z)
{
    int n = 0;

    if (rail_is_at(w, x, y, z - 1) || rail_is_at(w, x, y + 1, z - 1) || rail_is_at(w, x, y - 1, z - 1)) ++n;
    if (rail_is_at(w, x, y, z + 1) || rail_is_at(w, x, y + 1, z + 1) || rail_is_at(w, x, y - 1, z + 1)) ++n;
    if (rail_is_at(w, x - 1, y, z) || rail_is_at(w, x - 1, y + 1, z) || rail_is_at(w, x - 1, y - 1, z)) ++n;
    if (rail_is_at(w, x + 1, y, z) || rail_is_at(w, x + 1, y + 1, z) || rail_is_at(w, x + 1, y - 1, z)) ++n;

    return n;
}

/* func_150052_a over the world for one position: the reshaper a plain rail's
 * func_150048_a re-runs when a power-providing block changed beside it. */
static void rail_reshape(struct world *w, int x, int y, int z, int place)
{
    BWL_EMIT(BWL_RAIL_RESHAPE, w, x, y, z, place);
}

/* BlockRailDetector.func_150054_a, without the minecart query (the probe
 * worlds hold no minecart, so var7 is always false). With redstone off
 * (config.yaml) isBlockIndirectlyGettingPowered is false too, so a set power
 * bit is cleared; the block itself and the one below are told, and
 * markBlockRangeForRenderUpdate and func_147453_f write nothing here. */
static void detector_power_state(struct world *w, int x, int y, int z)
{
    int m = meta_at(w, x, y, z);

    if ((m & 8) == 0) return;

    SET_META_NOTIFY(w, x, y, z, m & 7);
    NOTIFY(w, x, y, z, ID_DETECTOR_RAIL);
    NOTIFY(w, x, y - 1, z, ID_DETECTOR_RAIL);
}

/* func_150052_a, then the powered forms' own onNeighborBlockChange
 * (BlockRailBase.onBlockAdded's second half): the support test, whose failure
 * drops the rail, then the class hook func_150048_a. The hook is dead here in
 * every case: the source is the rail itself, which cannot provide power for
 * the plain rail's re-curve, and the golden and activator rails' hook needs a
 * redstone signal the probe worlds never have. BlockRailDetector then runs
 * func_150054_a once more directly (onBlockAdded's tail). */
static void rail_on_added(struct world *w, int x, int y, int z)
{
    int id = block_at(w, x, y, z) & 4095;

    rail_reshape(w, x, y, z, 1);
    BWL_EMIT(BWL_RAIL_ADDED_TAIL, w, x, y, z, id);
}

/* rail_on_added after the reshape */
static int rail_added_tail_step(struct bwl_frame *f)
{
    struct world *w = f->op.w;
    int x = f->op.x, y = f->op.y, z = f->op.z;

    if (f->pc == 0)
    {
        f->pc = 1;

        if (is_rail_powered_form(f->op.a[0]) && !rail_support_holds(w, x, y, z))
        {
            drop_as_item(w, x, y, z, meta_at(w, x, y, z), ID_RAIL);
            SET_BLOCK(w, x, y, z, 0, 0, 3);
            return 0;
        }
    }

    if (is_class(block_at(w, x, y, z), BC_RAILDETECTOR)) detector_power_state(w, x, y, z);
    return 1;
}

/* BlockRailBase.onNeighborBlockChange: the support half, then the class hook
 * func_150048_a. Only the plain rail's hook can write here: a detector rail
 * as the source (the one block that canProvidePower) with three side rails
 * re-curves it. The golden and activator rails' hook searches for a powered
 * rail to propagate from; no rail in the probe worlds is ever powered. */
static void rail_neighbor(struct world *w, int x, int y, int z, int source)
{
    if (!rail_support_holds(w, x, y, z))
    {
        drop_as_item(w, x, y, z, meta_at(w, x, y, z), ID_RAIL);
        SET_BLOCK(w, x, y, z, 0, 0, 3);
        return;
    }

    if (block_at(w, x, y, z) == ID_RAIL && source_provides_power(source) && rail_side_count(w, x, y, z) == 3)
        rail_reshape(w, x, y, z, 0);
}

/* BlockHopper.func_149919_e: World.isBlockIndirectlyGettingPowered is 0 with
 * redstone off, so the hopper is enabled; a disabled bit 8 some metadata
 * write left comes off (flag 4, no notification). */
static void hopper_enable(struct world *w, int x, int y, int z)
{
    int m = meta_at(w, x, y, z);

    if ((m & 8) == 8) world_set_meta_quiet(w, x, y, z, m & 7);
}

void bwl_on_added(struct world *w, int id, int meta, int x, int y, int z)
{
    switch (id & 4095)
    {
    case BLK_HOPPER: /* BlockHopper.onBlockAdded: super (empty), func_149919_e */
        hopper_enable(w, x, y, z);
        break;

    case 12: /* BlockFalling.onBlockAdded: a scheduled tick two from now */
    case 13:
    case ID_ANVIL: /* BlockAnvil inherits BlockFalling's onBlockAdded */
        ticks_bwl_sched(w, x, y, z, id & 4095, 2);
        break;

    case ID_DRAGON_EGG: /* BlockDragonEgg.onBlockAdded, tick rate 5 */
        ticks_bwl_sched(w, x, y, z, id & 4095, 5);
        break;

    case 8:  /* BlockDynamicLiquid.onBlockAdded: super (func_149805_n), then a
              * scheduled tick at the block's tick rate for a block that is
              * still there */
    case 10:
        lava_water_check(w, id, x, y, z);

        if (bwl_pending()) BWL_EMIT(BWL_LIQUID_ADDED, w, x, y, z, id);
        else liquid_added_tail(w, id, x, y, z);

        break;

    case 9:  /* BlockStaticLiquid inherits BlockLiquid's onBlockAdded */
    case 11:
        lava_water_check(w, id, x, y, z);
        break;


    case ID_FIRE:
        fire_on_block_added(w, x, y, z);
        break;

    case ID_TORCH:
    case 76: /* BlockRedstoneTorch: the lit torch's onAdded runs its own
              * neighbour rounds */
        torch_on_added(w, x, y, z);
        break;

    case ID_TRIPWIRE:
        tripwire_on_added(w, x, y, z);
        break;

    case ID_REDSTONE_WIRE:
        wire_on_added(w, x, y, z);
        break;

    case ID_UNPOWERED_REPEATER:
    case ID_POWERED_REPEATER:
    case 149: /* BlockRedstoneComparator */
    case 150:
        /* BlockRedstoneDiode.onBlockAdded: func_149911_e's two notifications;
         * the comparator's setTileEntity is the world core's own demand
         * creation */
        diode_notify(w, x, y, z, meta_at(w, x, y, z), id);
        break;

    case ID_DISPENSER:
    case 158: /* the dropper, whose class extends the dispenser's */
        dispenser_facing(w, x, y, z);
        break;

    case ID_FURNACE:
    case ID_LIT_FURNACE:
        furnace_on_block_added(w, x, y, z);
        break;

    case ID_RAIL:
    case ID_GOLDEN_RAIL:
    case ID_DETECTOR_RAIL:
    case ID_ACTIVATOR_RAIL:
        rail_on_added(w, x, y, z);
        break;

    case 119:
        /* BlockEndPortal.onBlockAdded: outside the overworld the block goes
         * back to air (setBlockToAir) unless field_149948_a is set, which
         * only the dragon's exit portal does: an eye-lit ring in the End or
         * the Nether makes no portal */
        if (w->dim != 0 && !nw_env->blockcb.end_portal_kept) SET_BLOCK(w, x, y, z, 0, 0, 3);
        break;

    default:
        /* Block's empty body. BlockStairs passes its own onBlockAdded to its
         * model block, which is empty for every stair the table places. */
        break;
    }
}

void bwl_on_neighbor(struct world *w, int id, int meta, int x, int y, int z, int source)
{
    if ((id & 4095) == 12 || (id & 4095) == 13 || (id & 4095) == ID_ANVIL)
    {
        /* BlockFalling.onNeighborBlockChange: func_149738_a is 2; the anvil
         * inherits it */
        ticks_bwl_sched(w, x, y, z, id & 4095, 2);
        return;
    }

    if ((id & 4095) == ID_DRAGON_EGG)
    {
        /* BlockDragonEgg.onNeighborBlockChange, tick rate 5 */
        ticks_bwl_sched(w, x, y, z, id & 4095, 5);
        return;
    }

    (void)meta;

    switch (id & 4095)
    {
    case 8:  /* BlockLiquid.onNeighborBlockChange, inherited by the dynamic form */
    case 10:
        lava_water_check(w, id, x, y, z);
        break;

    case ID_TORCH:
    case 76:
        torch_neighbor(w, x, y, z);
        break;

    case 54:  /* BlockChest.onNeighborBlockChange: updateContainingBlockInfo */
    case 146:
    {
        struct chunk *ch = world_chunk(w, x >> 4, z >> 4);
        struct tile_entity *te = ch ? te_find(&ch->tes, x, y, z) : NULL;
        if (te && te->kind == TE_CHEST) te->u.chest.adjacent_checked = 0;
        break;
    }

    case ID_TRIPWIRE:
        tripwire_neighbor(w, x, y, z);
        break;

    case ID_TRIPWIRE_HOOK:
        hook_neighbor(w, x, y, z, source);
        break;

    case ID_REDSTONE_WIRE:
        wire_neighbor(w, x, y, z);
        break;

    case ID_LEVER:
        lever_neighbor(w, x, y, z);
        break;

    case BLK_HOPPER: /* BlockHopper.onNeighborBlockChange: func_149919_e */
        hopper_enable(w, x, y, z);
        break;

    case BLK_DISPENSER: /* BlockDispenser.onNeighborBlockChange: never powered */
    case BLK_DROPPER:   /* (redstone off), so only a triggered bit 8 comes off */
    {
        int m = meta_at(w, x, y, z);

        if ((m & 8) != 0) world_set_meta_quiet(w, x, y, z, m & -9);
        break;
    }

    case ID_BTN_STONE:
    case ID_BTN_WOOD:
        button_neighbor(w, x, y, z);
        break;

    case ID_UNPOWERED_REPEATER:
    case ID_POWERED_REPEATER:
    case ID_UNPOWERED_COMPARATOR:
    case ID_POWERED_COMPARATOR:
        diode_neighbor(w, x, y, z, source);
        break;

    case ID_RAIL:
    case ID_GOLDEN_RAIL:
    case ID_DETECTOR_RAIL:
    case ID_ACTIVATOR_RAIL:
        rail_neighbor(w, x, y, z, source);
        break;

    case 9:  /* BlockStaticLiquid.onNeighborBlockChange */
    case 11:
        static_liquid_neighbor(w, id, x, y, z);
        break;

    case ID_DOOR:
    case 71: /* the iron door */
        door_neighbor(w, x, y, z, source);
        break;

    case FIRE_BLOCK: /* BlockFire.onNeighborBlockChange: the same placement
                      * test, removing the fire when it fails */
        if (!fire_can_place(w, x, y, z)) SET_BLOCK(w, x, y, z, 0, 0, 3);
        break;

    /* BlockSign.onNeighborBlockChange: a standing sign needs a solid floor, a
     * wall sign its wall; the sign item drops */
    case 63:
    case 68:
    {
        int idb = id & 4095;
        int ok;

        if (idb == 63) ok = MATERIALS[material_of(block_at(w, x, y - 1, z))].is_solid;
        else
        {
            int m = meta_at(w, x, y, z);

            ok = (m == 2 && MATERIALS[material_of(block_at(w, x, y, z + 1))].is_solid) ||
                 (m == 3 && MATERIALS[material_of(block_at(w, x, y, z - 1))].is_solid) ||
                 (m == 4 && MATERIALS[material_of(block_at(w, x + 1, y, z))].is_solid) ||
                 (m == 5 && MATERIALS[material_of(block_at(w, x - 1, y, z))].is_solid);
        }

        if (!ok)
        {
            place_block_drop(w, x, y, z, idb, meta_at(w, x, y, z));
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        }
        break;
    }

    /* BlockBasePressurePlate.onNeighborBlockChange, which the weighted
     * plates inherit: no floor under the plate drops it */
    case 70:
    case 72:
    case 147:
    case 148:
        if (!solid_top_at(w, x, y - 1, z) &&
            block_at(w, x, y - 1, z) != 85 && block_at(w, x, y - 1, z) != 113)
        {
            breaking_drop(w, x, y, z, id, meta_at(w, x, y, z));
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        }
        break;

    case ID_TRAPDOOR:
    {
        int m = meta_at(w, x, y, z);
        int vx = x, vz = z;

        if ((m & 3) == 0) vz = z + 1;
        else if ((m & 3) == 1) vz = z - 1;
        else if ((m & 3) == 2) vx = x + 1;
        else vx = x - 1;

        if (!trapdoor_support(block_at(w, vx, y, vz)))
        {
            /* BlockTrapDoor.onNeighborBlockChange: setBlockToAir, then the
             * trapdoor item drops */
            SET_BLOCK(w, x, y, z, 0, 0, 3);
            BWL_EMIT(BWL_TRAPDOOR_TAIL, w, x, y, z, source, 1);
            break;
        }

        trapdoor_tail(w, x, y, z, source, 0);
        break;
    }

    case ID_PORTAL: /* BlockPortal.onNeighborBlockChange: the frame test again,
                     * against the frame axis this portal block carries */
    {
        int axis = meta_at(w, x, y, z) & 3;
        struct portal_size a, b;

        portal_size_init(w, &a, x, y, z, 1);
        portal_size_init(w, &b, x, y, z, 2);

        if (axis == 1 && (!portal_size_ok(&a) || a.existing < a.width * a.height))
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        else if (axis == 2 && (!portal_size_ok(&b) || b.existing < b.width * b.height))
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        else if (axis == 0 && !portal_size_ok(&a) && !portal_size_ok(&b))
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        break;
    }

    case 127: /* BlockCocoa.onNeighborBlockChange: the pod's facing points at
               * the log it hangs from (meta & 3 is a Direction); no log with
               * the jungle species there drops it as a flags-2 air write */
    {
        int m = meta_at(w, x, y, z);
        int dir = m & 3;
        int lx = x + (dir == 1 ? -1 : dir == 3 ? 1 : 0);
        int lz = z + (dir == 0 ? 1 : dir == 2 ? -1 : 0);

        if (block_at(w, lx, y, lz) != 17 || (meta_at(w, lx, y, lz) & 3) != 3)
        {
            /* dropBlockAsItem on World.rand: the tick's stream, as the
             * other breaking callbacks spend it */
            breaking_drop(w, x, y, z, 127, m);
            SET_BLOCK(w, x, y, z, 0, 0, 2);
        }
        break;
    }

    case ID_SNOW_LAYER:
        if (!snow_can_place(w, x, y, z))
        {
            /* BlockSnow.quantityDropped is 0: the drop spends nothing */
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        }
        break;

    case ID_CACTUS:
        if (!cactus_can_stay(w, x, y, z))
        {
            /* World.func_147480_a: its 2001 first */
            env_aux_sfx(w, 2001, x, y, z, ID_CACTUS + (meta_at(w, x, y, z) << 12));
            breaking_drop(w, x, y, z, id, meta_at(w, x, y, z));
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        }
        break;

    case ID_LADDER:
    {
        int m = meta_at(w, x, y, z);
        int ok = 0;

        if (m == 2 && BLOCKS[block_at(w, x, y, z + 1)].normal_cube) ok = 1;
        if (m == 3 && BLOCKS[block_at(w, x, y, z - 1)].normal_cube) ok = 1;
        if (m == 4 && BLOCKS[block_at(w, x + 1, y, z)].normal_cube) ok = 1;
        if (m == 5 && BLOCKS[block_at(w, x - 1, y, z)].normal_cube) ok = 1;

        if (!ok)
        {
            /* BlockLadder.onNeighborBlockChange: the ladder item drops, then
             * setBlockToAir */
            place_block_drop(w, x, y, z, ID_LADDER, m);
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        }
        break;
    }

    case ID_VINE:
        if (!vine_update(w, x, y, z))
        {
            /* BlockVine.quantityDropped is 0: the drop spends nothing */
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        }
        break;

    case ID_CARPET:
        if (!carpet_can_stay(w, x, y, z))
        {
            /* BlockCarpet.func_150090_e: the drop, then setBlockToAir */
            place_block_drop(w, x, y, z, ID_CARPET, meta_at(w, x, y, z));
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        }
        break;

    case ID_BED:
    {
        int m = meta_at(w, x, y, z);
        int dx, dz;

        bed_head_offset(m, &dx, &dz);

        if ((m & 8) != 0)
        {
            if (block_at(w, x - dx, y, z - dz) != ID_BED) SET_BLOCK(w, x, y, z, 0, 0, 3);
        }
        else if (block_at(w, x + dx, y, z + dz) != ID_BED)
        {
            SET_BLOCK(w, x, y, z, 0, 0, 3);
            /* the pop's own drop: Items.bed, damage 0 */
            BWL_EMIT(BWL_DROP_STACK, w, x, y, z, ITEM_BED, 0);
        }

        break;
    }

    case ID_CAKE:
        /* BlockCake.canBlockStay: a solid floor. */
        if (!MATERIALS[material_of(block_at(w, x, y - 1, z))].is_solid) SET_BLOCK(w, x, y, z, 0, 0, 3);
        break;

    case ID_FARMLAND:
        /* BlockFarmland.onNeighborBlockChange: a solid block above turns the
         * farmland back into dirt. */
        if (MATERIALS[material_of(block_at(w, x, y + 1, z))].is_solid)
            SET_BLOCK(w, x, y, z, ID_DIRT, 0, 3);
        break;

    case ID_FENCE_GATE:
    {
        int m = meta_at(w, x, y, z);
        int powered = is_indirectly_powered(w, x, y, z);

        if (powered || source_provides_power(source))
        {
            if (powered && (m & 4) == 0)
            {
                world_set_meta_quiet(w, x, y, z, m | 4);
                env_aux_sfx(w, 1003, x, y, z, 0);
            }
            else if (!powered && (m & 4) != 0)
            {
                world_set_meta_quiet(w, x, y, z, m & ~4);
                env_aux_sfx(w, 1003, x, y, z, 0);
            }
        }

        break;
    }

    default:
        if (is_bush_family(id & 4095))
        {
            if (!bush_can_stay(w, id & 4095, x, y, z))
            {
                /* BlockDoublePlant.func_149855_e drops only from the lower
                 * half: the upper one goes to air with no drop roll */
                if (!is_class(id, BC_DOUBLEPLANT) || (meta_at(w, x, y, z) & 8) == 0)
                    breaking_drop(w, x, y, z, id & 4095, meta_at(w, x, y, z));
                bush_neighbor(w, id & 4095, x, y, z);
            }
        }
        else if (is_class(id, BC_REED) && !reed_can_stay(w, x, y, z))
        {
            breaking_drop(w, x, y, z, id, meta_at(w, x, y, z));
            SET_BLOCK(w, x, y, z, 0, 0, 3);
        }
        break;
    }
}
/* Block.breakBlock for the block a write replaced. Only BlockRailBase
 * overrides it among the blocks the probes write over: the leaves and log
 * bodies live in world.c's block_break, everything else has the empty
 * inherited body. */
void bwl_on_broken(struct world *w, int x, int y, int z, int old_id)
{
    int id = old_id & 4095;

    /* BlockChest, BlockFurnace, BlockDispenser, BlockHopper and
     * BlockBrewingStand.breakBlock: the inventory spill from the block's own
     * Random (spill.c), then World.func_147453_f (comparator.c: the
     * comparators beside it re-read their input, now air) and
     * BlockContainer's removal (the world path's). The furnace skips both
     * while func_149931_a swaps lit and unlit (field_149934_M); the brewing
     * stand's breakBlock has no func_147453_f. The dig spills before its
     * write, which leaves the slots empty here. */
    int cmp_break = id == BLK_CHEST || id == BLK_TRAPPED_CHEST || id == BLK_DISPENSER || id == BLK_DROPPER ||
                    id == BLK_HOPPER ||
                    ((id == BLK_FURNACE || id == BLK_LIT_FURNACE) && !tileticks_furnace_swapping());

    if (nw_env->blockcb.env.item_spill != NULL && nw_env->blockcb.env.det != NULL && block_rand_index(id) >= 0
        && !((id == BLK_FURNACE || id == BLK_LIT_FURNACE) && tileticks_furnace_swapping()))
    {
        container_spill(w, x, y, z, id, NULL, nw_env->blockcb.env.det, nw_env->blockcb.env.spill_role, nw_env->blockcb.env.item_spill,
                        nw_env->blockcb.env.ctx);
        if (cmp_break) comparator_notify_emit(w, x, y, z, id);
        return;
    }

    if (cmp_break)
    {
        comparator_notify_emit(w, x, y, z, id);
        return;
    }

    /* BlockRedstoneTorch.breakBlock: the lit torch (field_150113_a) runs a
     * full neighbour round around each of its six neighbours, as its
     * onBlockAdded does; nothing reads power in a redstone-off world, but
     * the rounds reach the static liquids (setNotStationary) and the
     * plants */
    if (id == 76)
    {
        NOTIFY(w, x, y - 1, z, 76);
        NOTIFY(w, x, y + 1, z, 76);
        NOTIFY(w, x - 1, y, z, 76);
        NOTIFY(w, x + 1, y, z, 76);
        NOTIFY(w, x, y, z - 1, 76);
        NOTIFY(w, x, y, z + 1, 76);
        return;
    }

    /* BlockRedstoneWire.breakBlock: the (empty) super, the six rounds, then
     * func_150177_e, then func_150172_m on the four sides and the four
     * diagonal cells (above a normal-cube side, else below). func_150177_e
     * reads the metadata still in place, the wire's, against its
     * neighbours' wires: with redstone off every wire is at 0, so it moves
     * nothing and notifies nothing, and is left out. */
    if (id == ID_REDSTONE_WIRE)
    {
        NOTIFY(w, x, y + 1, z, ID_REDSTONE_WIRE);
        NOTIFY(w, x, y - 1, z, ID_REDSTONE_WIRE);
        NOTIFY(w, x + 1, y, z, ID_REDSTONE_WIRE);
        NOTIFY(w, x - 1, y, z, ID_REDSTONE_WIRE);
        NOTIFY(w, x, y, z + 1, ID_REDSTONE_WIRE);
        NOTIFY(w, x, y, z - 1, ID_REDSTONE_WIRE);
        BWL_EMIT(BWL_WIRE_AROUND, w, x - 1, y, z, 0);
        BWL_EMIT(BWL_WIRE_AROUND, w, x + 1, y, z, 0);
        BWL_EMIT(BWL_WIRE_AROUND, w, x, y, z - 1, 0);
        BWL_EMIT(BWL_WIRE_AROUND, w, x, y, z + 1, 0);
        BWL_EMIT(BWL_WIRE_DIAG, w, x, y, z, -1, 0);
        BWL_EMIT(BWL_WIRE_DIAG, w, x, y, z, 1, 0);
        BWL_EMIT(BWL_WIRE_DIAG, w, x, y, z, 0, -1);
        BWL_EMIT(BWL_WIRE_DIAG, w, x, y, z, 0, 1);
        return;
    }

    /* BlockTripWire.breakBlock: func_150138_a(meta | 1), the facing hooks
     * of both runs updated as if this wire were tripped (and disarmed when
     * shears set bit 8 first). The cell already holds the new block, its
     * metadata is still the wire's. */
    if (id == ID_TRIPWIRE)
    {
        BWL_EMIT(BWL_TRIPWIRE_HOOKS, w, x, y, z, meta_at(w, x, y, z) | 1);
        return;
    }

    /* BlockTripWireHook.breakBlock: an attached or powered hook runs
     * func_150136_a(removing, meta, false, -1, 0) (the far hook and the
     * wires between let go), then a powered one's func_150134_a
     * notifications, then the (empty) super */
    if (id == ID_TRIPWIRE_HOOK)
    {
        int m = meta_at(w, x, y, z);

        if ((m & 4) == 4 || (m & 8) == 8) BWL_EMIT(BWL_HOOK_UPDATE, w, x, y, z, 1, m, 0, -1, 0);
        if ((m & 8) == 8) hook_notify(w, x, y, z, m & 3);
        return;
    }

    /* BlockFlowerPot.breakBlock: the pot's content item, through
     * dropBlockAsItem_do (no chance float), then the (empty) super. The tile
     * entity is still in place here: the world path removes it after this. */
    if (id == ID_FLOWER_POT)
    {
        struct tile_entity *te = world_tile_entity(w, x, y, z);

        if (te != NULL && te->kind == TE_FLOWER_POT && te->u.pot.item > 0)
            drop_item_stack_do(w, x, y, z, te->u.pot.item, te->u.pot.data);

        return;
    }

    /* BlockJukebox.breakBlock: func_149925_e's disc drop (playAuxSFX 1005 and
     * playRecord move no state, the three World.rand floats and the EntityItem
     * as dropBlockAsItem_do), then the (empty) super. */
    if (id == ID_JUKEBOX)
    {
        struct tile_entity *te = world_tile_entity(w, x, y, z);

        if (te != NULL && te->kind == TE_JUKEBOX && te->u.jukebox.item > 0 && te->u.jukebox.count > 0)
            drop_item_stack_do(w, x, y, z, te->u.jukebox.item, te->u.jukebox.damage);

        return;
    }

    /* BlockLever.breakBlock: a powered lever's sweep (the self cell's six
     * neighbours, then the facing's extra, the lever's own facing map) before
     * the (empty) super. The neighbours the sweep reaches schedule the falling
     * blocks' updates, which take tick ids during the setup. */
    if (id == ID_LEVER)
    {
        int m = meta_at(w, x, y, z);

        if ((m & 8) != 0)
        {
            int facing = m & 7;

            NOTIFY(w, x, y, z, id);

            if (facing == 1) NOTIFY(w, x - 1, y, z, id);
            else if (facing == 2) NOTIFY(w, x + 1, y, z, id);
            else if (facing == 3) NOTIFY(w, x, y, z - 1, id);
            else if (facing == 4) NOTIFY(w, x, y, z + 1, id);
            else if (facing == 5 || facing == 6) NOTIFY(w, x, y - 1, z, id);
            else NOTIFY(w, x, y + 1, z, id);
        }

        return;
    }

    /* BlockBasePressurePlate.breakBlock: a pressed plate's func_150064_a_
     * (the six neighbours of its own cell, then of the cell below) before
     * the (empty) super. func_150060_c: the stone and wooden plates' power is
     * 15 at metadata 1, the weighted plates' is the metadata itself. */
    if (id == 70 || id == 72 || id == 147 || id == 148)
    {
        int m = meta_at(w, x, y, z);
        int power = id == 147 || id == 148 ? m : m == 1 ? 15 : 0;

        if (power > 0)
        {
            NOTIFY(w, x, y, z, id);
            NOTIFY(w, x, y - 1, z, id);
        }

        return;
    }

    /* BlockButton.breakBlock: a powered button's func_150042_a sweep before
     * the (empty) super */
    if (id == ID_BTN_STONE || id == ID_BTN_WOOD)
    {
        if ((meta_at(w, x, y, z) & 8) != 0) button_notify(w, x, y, z, meta_at(w, x, y, z) & 7, id);
        return;
    }

    /* BlockRedstoneRepeater.breakBlock: super (empty), then func_149911_e's
     * sweep unconditionally; BlockRedstoneComparator.breakBlock's removeTileEntity
     * the world core ran before this and its sweep is the same one. The sweep
     * reads the metadata still in place (the new block's id is stored, the old
     * block's metadata not yet replaced) */
    if (id == ID_UNPOWERED_REPEATER || id == ID_POWERED_REPEATER
        || id == ID_UNPOWERED_COMPARATOR || id == ID_POWERED_COMPARATOR)
    {
        diode_notify(w, x, y, z, meta_at(w, x, y, z), id);
        return;
    }

    if (!is_rail_block(id)) return;

    int powered_form = id == ID_GOLDEN_RAIL || id == ID_DETECTOR_RAIL ||
                       (is_class(id, BC_RAILPOWERED) && id != ID_RAIL);
    int var7 = powered_form ? meta_at(w, x, y, z) & 7 : meta_at(w, x, y, z);

    if (var7 == 2 || var7 == 3 || var7 == 4 || var7 == 5)
        NOTIFY(w, x, y + 1, z, id);

    if (powered_form)
    {
        NOTIFY(w, x, y, z, id);
        NOTIFY(w, x, y - 1, z, id);
    }
}

/* ---------------------------------------------------------- entry points */
void block_on_neighbor_changed(struct world *w, int id, int meta, int x, int y, int z, int source)
{
    BWL_CALL(BWL_NEIGHBOR, w, x, y, z, source, 1, id, meta);
}

/* ------------------------------------------------ the continuation steps */
int blockcb_bwl_step(struct bwl_frame *f)
{
    struct world *w = f->op.w;
    int x = f->op.x, y = f->op.y, z = f->op.z;
    const int *a = f->op.a;

    switch (f->op.kind)
    {
    case BWL_LAVA_FIZZ:
        lava_fizz();
        return 1;

    case BWL_CMP_NOTIFY:
        return comparator_notify_step(f);

    case BWL_LIQUID_ADDED:
        liquid_added_tail(w, a[0], x, y, z);
        return 1;

    case BWL_STATIC_NEIGHBOR:
        static_liquid_tail(w, a[0], x, y, z);
        return 1;

    case BWL_PORTAL_PLACE:
        return portal_place_step(f);

    case BWL_TORCH_ADDED:
        torch_on_added(w, x, y, z);
        return 1;

    case BWL_TORCH_STAY:
        torch_stay(w, x, y, z);
        return 1;

    case BWL_TRIPWIRE_HOOKS:
        return tripwire_hooks_step(f);

    case BWL_HOOK_UPDATE:
        hook_update(w, x, y, z, a[0], a[1], a[2], a[3], a[4]);
        return 1;

    case BWL_HOOK_SOUND:
        hook_sound(w, x, y, z, a[0], a[1], a[2], a[3]);
        return 1;

    case BWL_WIRE_AROUND:
        wire_notify_around(w, x, y, z);
        return 1;

    case BWL_WIRE_DIAG:
        wire_diag(w, x, y, z, a[0], a[1]);
        return 1;

    case BWL_DIODE_NOTIFY:
        diode_notify(w, x, y, z, meta_at(w, x, y, z), a[0]);
        return 1;

    case BWL_BUTTON_NOTIFY:
        button_notify(w, x, y, z, a[0], a[1]);
        return 1;

    case BWL_RAIL_RESHAPE:
        return rail_reshape_step(f);

    case BWL_RAIL_ADDED_TAIL:
        return rail_added_tail_step(f);

    case BWL_DETECTOR:
        detector_power_state(w, x, y, z);
        return 1;

    case BWL_DOOR:
        return door_step(f);

    case BWL_TRAPDOOR_TAIL:
        trapdoor_tail(w, x, y, z, a[0], a[1]);
        return 1;

    case BWL_DROP_STACK:
        drop_item_stack(w, x, y, z, a[0], a[1]);
        return 1;

    default:
        return 1;
    }
}
