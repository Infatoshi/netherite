/* StructureVillagePieces' addComponentParts, every kind's block half: the well,
 * the roads, the three houses, the small house with its blacksmith chest, the
 * church, the hall, the garden house, the two fields and the wood hut, with
 * their doors, torches, ladders, stairs and the villagers the houses spawn.
 * Every Random draw is in Java's order; see structure_blocks.c for the helpers
 * and the env the writes run in. Desert villages swap log, cobblestone, planks,
 * both stairs and gravel for sandstone (Village.func_151558_b / func_151557_c). */
#include "structure_blocks.h"

#include "blocks.h"
#include "loot.h"

#include <string.h>

/* the block ids the pieces place */
enum {
    VBLK_AIR = 0, VBLK_COBBLE = 4, VBLK_PLANKS = 5, VBLK_FLOWING_WATER = 8,
    VBLK_WATER = 9, VBLK_FLOWING_LAVA = 10, VBLK_GRAVEL = 13, VBLK_DIRT = 3,
    VBLK_LOG = 17, VBLK_SANDSTONE = 24, VBLK_WOOL = 35, VBLK_DOUBLE_SLAB = 43,
    VBLK_STONE_SLAB = 44, VBLK_BOOKSHELF = 47, VBLK_TORCH = 50, VBLK_OAK_STAIRS = 53,
    VBLK_CHEST = 54, VBLK_CRAFTING = 58, VBLK_WHEAT = 59, VBLK_FARMLAND = 60,
    VBLK_FURNACE = 61, VBLK_WOODEN_DOOR = 64, VBLK_LADDER = 65, VBLK_STONE_STAIRS = 67,
    VBLK_FENCE = 85, VBLK_IRON_BARS = 101, VBLK_GLASS_PANE = 102,
    VBLK_SANDSTONE_STAIRS = 128, VBLK_CARROTS = 141, VBLK_POTATOES = 142,
    VBLK_LOG2 = 162, VBLK_PRESSURE_PLATE = 72
};

/* ------------------------------------------------------------ the world reads
 *
 * World.getTopSolidOrLiquidBlock: from the top filled segment + 15 down, the
 * first block whose material blocksMovement and is not leaves, + 1. */

static int top_filled_segment(struct chunk *c)
{
    for (int s = 15; s >= 0; --s)
        if ((c->mask >> s) & 1) return s << 4;
    return 0;
}

static int top_solid_or_liquid(struct world *w, int x, int z)
{
    struct chunk *c = world_load_chunk(w, x >> 4, z >> 4);

    x &= 15;
    z &= 15;

    for (int y = top_filled_segment(c) + 15; y > 0; --y)
    {
        int id = ((c->mask >> (y >> 4)) & 1) ? chunk_cell_id(c, ((x) << 12) | ((z) << 8) | y) & 4095 : 0;

        if (MATERIALS[BLOCKS[id].material].blocks_movement
            && strcmp(MATERIALS[BLOCKS[id].material].name, "leaves") != 0) return y + 1;
    }

    return -1;
}

/* Village.getAverageGroundLevel, over the step chunk box; the provider's
 * average ground level is 64 for the default overworld. */
static int average_ground_level(struct sc_ctx *c, const struct piece *p)
{
    long sum = 0;
    int n = 0;

    for (int z = p->bb.minZ; z <= p->bb.maxZ; ++z)
        for (int x = p->bb.minX; x <= p->bb.maxX; ++x)
            if (sc_in_step_box(c, x, 64, z))
            {
                int top = top_solid_or_liquid(c->w, x, z);

                if (top < 64) top = 64;
                sum += top;
                ++n;
            }

    return n == 0 ? -1 : (int)(sum / n);
}

/* The field_143015_k guard every village piece opens with: settle on the
 * average ground level, shifting the box so its top sits one under
 * `height`. The well passes height 4: its offset is k - maxY + 3. Returns 0
 * when the level came out -1 (the piece returns true and tries again next
 * step), 1 to go on. */
static int settle(struct sc_ctx *c, struct piece *p, int height)
{
    if (p->u.village.hpos >= 0) return 1;

    p->u.village.hpos = average_ground_level(c, p);

    if (p->u.village.hpos < 0) return 0;

    bbox_offset(&p->bb, 0, p->u.village.hpos - p->bb.maxY + height - 1, 0);
    return 1;
}

/* ------------------------------------------------------------ desert swap
 * Village.func_151558_b / func_151557_c, applied by the Village overrides of
 * placeBlockAtCurrentPosition, fillWithBlocks and fillColumnDown. */

static int swap_id(int desert, int id)
{
    if (!desert) return id;

    if (id == VBLK_LOG || id == VBLK_LOG2 || id == VBLK_COBBLE || id == VBLK_PLANKS || id == VBLK_GRAVEL)
        return VBLK_SANDSTONE;
    if (id == VBLK_OAK_STAIRS || id == VBLK_STONE_STAIRS) return VBLK_SANDSTONE_STAIRS;
    return id;
}

static int swap_meta(int desert, int id, int meta)
{
    if (!desert) return meta;

    if (id == VBLK_LOG || id == VBLK_LOG2 || id == VBLK_COBBLE) return 0;
    if (id == VBLK_PLANKS) return 2;
    return meta;
}

/* Village.func_151550_a. */
static void v_place(struct sc_ctx *c, const struct piece *p, int id, int meta, int tx, int ty, int tz)
{
    sc_place(c, p, swap_id(p->u.village.desert, id), swap_meta(p->u.village.desert, id, meta),
             tx, ty, tz);
}

/* Village.func_151549_a: fillWithBlocks with both blocks swapped. */
static void v_fill(struct sc_ctx *c, const struct piece *p, int minX, int minY, int minZ, int maxX, int maxY,
                   int maxZ, int outer, int inner, int replace)
{
    sc_fill_meta(c, p, minX, minY, minZ, maxX, maxY, maxZ,
                 swap_id(p->u.village.desert, outer), swap_meta(p->u.village.desert, outer, 0),
                 swap_id(p->u.village.desert, inner), swap_meta(p->u.village.desert, inner, 0),
                 replace);
}

/* Village.func_151554_b. */
static void v_fill_down(struct sc_ctx *c, const struct piece *p, int id, int meta, int tx, int ty, int tz)
{
    sc_fill_down(c, p, swap_id(p->u.village.desert, id), swap_meta(p->u.village.desert, id, meta),
                 tx, ty, tz);
}

/* MathHelper.getRandomIntegerInRange(rand, 2, 7): a crop's age. */
static int crop_age(jrand *rand)
{
    return 2 + jr_int_n(rand, 6);
}

/* ------------------------------------------------------------ doors
 *
 * StructureComponent.placeDoorAtCurrentPosition: the box guard is on the lower
 * half only; ItemDoor.func_150924_a then places both halves and notifies their
 * neighbours. The writes are raw World.setBlock calls, outside sc_place's box
 * guard for the upper half. */
static int block_normal_cube(struct sc_ctx *c, int x, int y, int z)
{
    return BLOCKS[world_get_block(c->w, x, y, z) & 4095].normal_cube;
}

static void place_door(struct sc_ctx *c, const struct piece *p, int tx, int ty, int tz, int meta)
{
    int x = sc_x_with_offset(p, tx, tz), y = sc_y_with_offset(p, ty), z = sc_z_with_offset(p, tx, tz);

    if (!sc_in_step_box(c, x, y, z)) return;

    int dx = 0, dz = 0;

    if (meta == 0) dz = 1;
    if (meta == 1) dx = -1;
    if (meta == 2) dz = -1;
    if (meta == 3) dx = 1;

    int left = block_normal_cube(c, x - dx, y, z - dz) + block_normal_cube(c, x - dx, y + 1, z - dz);
    int right = block_normal_cube(c, x + dx, y, z + dz) + block_normal_cube(c, x + dx, y + 1, z + dz);
    int left_door = world_get_block(c->w, x - dx, y, z - dz) == VBLK_WOODEN_DOOR
        || world_get_block(c->w, x - dx, y + 1, z - dz) == VBLK_WOODEN_DOOR;
    int right_door = world_get_block(c->w, x + dx, y, z + dz) == VBLK_WOODEN_DOOR
        || world_get_block(c->w, x + dx, y + 1, z + dz) == VBLK_WOODEN_DOOR;
    int hinge = (left_door && !right_door) || right > left;

    world_set_block(c->w, x, y, z, VBLK_WOODEN_DOOR, meta, 2);
    world_set_block(c->w, x, y + 1, z, VBLK_WOODEN_DOOR, 8 | (hinge ? 1 : 0), 2);
    world_notify_neighbors(c->w, x, y, z, VBLK_WOODEN_DOOR);
    world_notify_neighbors(c->w, x, y + 1, z, VBLK_WOODEN_DOOR);
}

static int hall_type(int i) { return i == 0 ? 4 : 0; }

/* Village.spawnVillagers: an EntityVillager per slot from the piece's own
 * VCount up to `count`, stopped by the step chunk box, each spawned (the
 * native side constructs the record the probe observed). */
static void spawn_villagers(struct sc_ctx *c, struct piece *p, int tx, int ty, int tz, int count,
                           int (*type)(int))
{
    for (int i = p->u.village.vcount; i < count; ++i)
    {
        int x = sc_x_with_offset(p, tx + i, tz), y = sc_y_with_offset(p, ty), z = sc_z_with_offset(p, tx + i, tz);

        if (!sc_in_step_box(c, x, y, z)) break;

        ++p->u.village.vcount;
        sc_villager_new(c, (double)x + 0.5, (double)y, (double)z + 0.5, type(i));
    }
}

static int farmer_type(int i)
{
    (void)i;
    return 0;
}

static int librarian_type(int i)
{
    (void)i;
    return 1;
}

static int priest_type(int i)
{
    (void)i;
    return 2;
}

static int blacksmith_type(int i)
{
    (void)i;
    return 3;
}

/* ------------------------------------------------------------ pieces */

/* Well.addComponentParts (the Start's own piece). */
static int well_parts(struct sc_ctx *c, struct piece *p)
{
    if (!settle(c, p, 4)) return 1;

    v_fill(c, p, 1, 0, 1, 4, 12, 4, VBLK_COBBLE, VBLK_FLOWING_WATER, 0);
    v_place(c, p, VBLK_AIR, 0, 2, 12, 2);
    v_place(c, p, VBLK_AIR, 0, 3, 12, 2);
    v_place(c, p, VBLK_AIR, 0, 2, 12, 3);
    v_place(c, p, VBLK_AIR, 0, 3, 12, 3);
    v_place(c, p, VBLK_FENCE, 0, 1, 13, 1);
    v_place(c, p, VBLK_FENCE, 0, 1, 14, 1);
    v_place(c, p, VBLK_FENCE, 0, 4, 13, 1);
    v_place(c, p, VBLK_FENCE, 0, 4, 14, 1);
    v_place(c, p, VBLK_FENCE, 0, 1, 13, 4);
    v_place(c, p, VBLK_FENCE, 0, 1, 14, 4);
    v_place(c, p, VBLK_FENCE, 0, 4, 13, 4);
    v_place(c, p, VBLK_FENCE, 0, 4, 14, 4);
    v_fill(c, p, 1, 15, 1, 4, 15, 4, VBLK_COBBLE, VBLK_COBBLE, 0);

    for (int var4 = 0; var4 <= 5; ++var4)
        for (int var5 = 0; var5 <= 5; ++var5)
            if (var5 == 0 || var5 == 5 || var4 == 0 || var4 == 5)
            {
                v_place(c, p, VBLK_GRAVEL, 0, var5, 11, var4);
                sc_clear_upwards(c, p, var5, 12, var4);
            }

    return 1;
}

/* Path.addComponentParts: absolute coordinates, the road's desert swap on the
 * gravel. */
static int path_parts(struct sc_ctx *c, struct piece *p)
{
    int road = swap_id(p->u.village.desert, VBLK_GRAVEL);

    for (int x = p->bb.minX; x <= p->bb.maxX; ++x)
        for (int z = p->bb.minZ; z <= p->bb.maxZ; ++z)
            if (sc_in_step_box(c, x, 64, z))
            {
                int y = top_solid_or_liquid(c->w, x, z) - 1;

                world_set_block(c->w, x, y, z, road, 0, 2);
            }

    return 1;
}

/* Torch.addComponentParts. */
static int torch_parts(struct sc_ctx *c, struct piece *p)
{
    if (!settle(c, p, 4)) return 1;

    v_fill(c, p, 0, 0, 0, 2, 3, 1, VBLK_AIR, VBLK_AIR, 0);
    v_place(c, p, VBLK_FENCE, 0, 1, 0, 0);
    v_place(c, p, VBLK_FENCE, 0, 1, 1, 0);
    v_place(c, p, VBLK_FENCE, 0, 1, 2, 0);
    v_place(c, p, VBLK_WOOL, 15, 1, 3, 0);
    v_place(c, p, VBLK_TORCH, 0, 0, 3, 0);
    v_place(c, p, VBLK_TORCH, 0, 1, 3, 1);
    v_place(c, p, VBLK_TORCH, 0, 2, 3, 0);
    v_place(c, p, VBLK_TORCH, 0, 1, 3, -1);
    return 1;
}

/* Field1.addComponentParts. */
static int field1_parts(struct sc_ctx *c, struct piece *p)
{
    if (!settle(c, p, 4)) return 1;

    v_fill(c, p, 0, 1, 0, 12, 4, 8, VBLK_AIR, VBLK_AIR, 0);
    v_fill(c, p, 1, 0, 1, 2, 0, 7, VBLK_FARMLAND, VBLK_FARMLAND, 0);
    v_fill(c, p, 4, 0, 1, 5, 0, 7, VBLK_FARMLAND, VBLK_FARMLAND, 0);
    v_fill(c, p, 7, 0, 1, 8, 0, 7, VBLK_FARMLAND, VBLK_FARMLAND, 0);
    v_fill(c, p, 10, 0, 1, 11, 0, 7, VBLK_FARMLAND, VBLK_FARMLAND, 0);
    v_fill(c, p, 0, 0, 0, 0, 0, 8, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 6, 0, 0, 6, 0, 8, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 12, 0, 0, 12, 0, 8, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 1, 0, 0, 11, 0, 0, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 1, 0, 8, 11, 0, 8, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 3, 0, 1, 3, 0, 7, VBLK_WATER, VBLK_WATER, 0);
    v_fill(c, p, 9, 0, 1, 9, 0, 7, VBLK_WATER, VBLK_WATER, 0);

    for (int z = 1; z <= 7; ++z)
    {
        v_place(c, p, p->u.village.ca, crop_age(c->rand), 1, 1, z);
        v_place(c, p, p->u.village.ca, crop_age(c->rand), 2, 1, z);
        v_place(c, p, p->u.village.cb, crop_age(c->rand), 4, 1, z);
        v_place(c, p, p->u.village.cb, crop_age(c->rand), 5, 1, z);
        v_place(c, p, p->u.village.cc, crop_age(c->rand), 7, 1, z);
        v_place(c, p, p->u.village.cc, crop_age(c->rand), 8, 1, z);
        v_place(c, p, p->u.village.cd, crop_age(c->rand), 10, 1, z);
        v_place(c, p, p->u.village.cd, crop_age(c->rand), 11, 1, z);
    }

    for (int z = 0; z < 9; ++z)
        for (int x = 0; x < 13; ++x)
        {
            sc_clear_upwards(c, p, x, 4, z);
            v_fill_down(c, p, VBLK_DIRT, 0, x, -1, z);
        }

    return 1;
}

/* Field2.addComponentParts. */
static int field2_parts(struct sc_ctx *c, struct piece *p)
{
    if (!settle(c, p, 4)) return 1;

    v_fill(c, p, 0, 1, 0, 6, 4, 8, VBLK_AIR, VBLK_AIR, 0);
    v_fill(c, p, 1, 0, 1, 2, 0, 7, VBLK_FARMLAND, VBLK_FARMLAND, 0);
    v_fill(c, p, 4, 0, 1, 5, 0, 7, VBLK_FARMLAND, VBLK_FARMLAND, 0);
    v_fill(c, p, 0, 0, 0, 0, 0, 8, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 6, 0, 0, 6, 0, 8, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 1, 0, 0, 5, 0, 0, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 1, 0, 8, 5, 0, 8, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 3, 0, 1, 3, 0, 7, VBLK_WATER, VBLK_WATER, 0);

    for (int z = 1; z <= 7; ++z)
    {
        v_place(c, p, p->u.village.ca, crop_age(c->rand), 1, 1, z);
        v_place(c, p, p->u.village.ca, crop_age(c->rand), 2, 1, z);
        v_place(c, p, p->u.village.cb, crop_age(c->rand), 4, 1, z);
        v_place(c, p, p->u.village.cb, crop_age(c->rand), 5, 1, z);
    }

    for (int z = 0; z < 9; ++z)
        for (int x = 0; x < 7; ++x)
        {
            sc_clear_upwards(c, p, x, 4, z);
            v_fill_down(c, p, VBLK_DIRT, 0, x, -1, z);
        }

    return 1;
}

/* House4Garden.addComponentParts. */
static int house4_parts(struct sc_ctx *c, struct piece *p)
{
    if (!settle(c, p, 6)) return 1;

    v_fill(c, p, 0, 0, 0, 4, 0, 4, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 4, 0, 4, 4, 4, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 1, 4, 1, 3, 4, 3, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_place(c, p, VBLK_COBBLE, 0, 0, 1, 0);
    v_place(c, p, VBLK_COBBLE, 0, 0, 2, 0);
    v_place(c, p, VBLK_COBBLE, 0, 0, 3, 0);
    v_place(c, p, VBLK_COBBLE, 0, 4, 1, 0);
    v_place(c, p, VBLK_COBBLE, 0, 4, 2, 0);
    v_place(c, p, VBLK_COBBLE, 0, 4, 3, 0);
    v_place(c, p, VBLK_COBBLE, 0, 0, 1, 4);
    v_place(c, p, VBLK_COBBLE, 0, 0, 2, 4);
    v_place(c, p, VBLK_COBBLE, 0, 0, 3, 4);
    v_place(c, p, VBLK_COBBLE, 0, 4, 1, 4);
    v_place(c, p, VBLK_COBBLE, 0, 4, 2, 4);
    v_place(c, p, VBLK_COBBLE, 0, 4, 3, 4);
    v_fill(c, p, 0, 1, 1, 0, 3, 3, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 4, 1, 1, 4, 3, 3, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 1, 1, 4, 3, 3, 4, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 2, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 2, 2, 4);
    v_place(c, p, VBLK_GLASS_PANE, 0, 4, 2, 2);
    v_place(c, p, VBLK_PLANKS, 0, 1, 1, 0);
    v_place(c, p, VBLK_PLANKS, 0, 1, 2, 0);
    v_place(c, p, VBLK_PLANKS, 0, 1, 3, 0);
    v_place(c, p, VBLK_PLANKS, 0, 2, 3, 0);
    v_place(c, p, VBLK_PLANKS, 0, 3, 3, 0);
    v_place(c, p, VBLK_PLANKS, 0, 3, 2, 0);
    v_place(c, p, VBLK_PLANKS, 0, 3, 1, 0);

    if (sc_block_at(c, p, 2, 0, -1) == VBLK_AIR && sc_block_at(c, p, 2, -1, -1) != VBLK_AIR)
        v_place(c, p, VBLK_STONE_STAIRS, sc_orient_meta(VBLK_STONE_STAIRS, 3, p->coord_base_mode), 2, 0, -1);

    v_fill(c, p, 1, 1, 1, 3, 3, 3, VBLK_AIR, VBLK_AIR, 0);

    if (p->u.village.terrace)
    {
        for (int x = 0; x <= 4; ++x) v_place(c, p, VBLK_FENCE, 0, x, 5, 0);
        for (int x = 0; x <= 4; ++x) v_place(c, p, VBLK_FENCE, 0, x, 5, 4);
        for (int z = 1; z <= 3; ++z) v_place(c, p, VBLK_FENCE, 0, 4, 5, z);
        for (int z = 1; z <= 3; ++z) v_place(c, p, VBLK_FENCE, 0, 0, 5, z);
    }

    if (p->u.village.terrace)
    {
        int ladder = sc_orient_meta(VBLK_LADDER, 3, p->coord_base_mode);

        v_place(c, p, VBLK_LADDER, ladder, 3, 1, 3);
        v_place(c, p, VBLK_LADDER, ladder, 3, 2, 3);
        v_place(c, p, VBLK_LADDER, ladder, 3, 3, 3);
        v_place(c, p, VBLK_LADDER, ladder, 3, 4, 3);
    }

    v_place(c, p, VBLK_TORCH, 0, 2, 3, 1);

    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 5; ++x)
        {
            sc_clear_upwards(c, p, x, 6, z);
            v_fill_down(c, p, VBLK_COBBLE, 0, x, -1, z);
        }

    spawn_villagers(c, p, 1, 1, 2, 1, farmer_type);
    return 1;
}

/* WoodHut.addComponentParts. */
static int woodhut_parts(struct sc_ctx *c, struct piece *p)
{
    if (!settle(c, p, 6)) return 1;

    v_fill(c, p, 1, 1, 1, 3, 5, 4, VBLK_AIR, VBLK_AIR, 0);
    v_fill(c, p, 0, 0, 0, 3, 0, 4, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 1, 0, 1, 2, 0, 3, VBLK_DIRT, VBLK_DIRT, 0);

    if (p->u.village.tall_house)
        v_fill(c, p, 1, 4, 1, 2, 4, 3, VBLK_LOG, VBLK_LOG, 0);
    else
        v_fill(c, p, 1, 5, 1, 2, 5, 3, VBLK_LOG, VBLK_LOG, 0);

    v_place(c, p, VBLK_LOG, 0, 1, 4, 0);
    v_place(c, p, VBLK_LOG, 0, 2, 4, 0);
    v_place(c, p, VBLK_LOG, 0, 1, 4, 4);
    v_place(c, p, VBLK_LOG, 0, 2, 4, 4);
    v_place(c, p, VBLK_LOG, 0, 0, 4, 1);
    v_place(c, p, VBLK_LOG, 0, 0, 4, 2);
    v_place(c, p, VBLK_LOG, 0, 0, 4, 3);
    v_place(c, p, VBLK_LOG, 0, 3, 4, 1);
    v_place(c, p, VBLK_LOG, 0, 3, 4, 2);
    v_place(c, p, VBLK_LOG, 0, 3, 4, 3);
    v_fill(c, p, 0, 1, 0, 0, 3, 0, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 3, 1, 0, 3, 3, 0, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 0, 1, 4, 0, 3, 4, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 3, 1, 4, 3, 3, 4, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 0, 1, 1, 0, 3, 3, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 3, 1, 1, 3, 3, 3, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 1, 1, 0, 2, 3, 0, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 1, 1, 4, 2, 3, 4, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 2, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 3, 2, 2);

    if (p->u.village.table > 0)
    {
        v_place(c, p, VBLK_FENCE, 0, p->u.village.table, 1, 3);
        v_place(c, p, VBLK_PRESSURE_PLATE, 0, p->u.village.table, 2, 3);
    }

    v_place(c, p, VBLK_AIR, 0, 1, 1, 0);
    v_place(c, p, VBLK_AIR, 0, 1, 2, 0);
    place_door(c, p, 1, 1, 0, sc_orient_meta(VBLK_WOODEN_DOOR, 1, p->coord_base_mode));

    if (sc_block_at(c, p, 1, 0, -1) == VBLK_AIR && sc_block_at(c, p, 1, -1, -1) != VBLK_AIR)
        v_place(c, p, VBLK_STONE_STAIRS, sc_orient_meta(VBLK_STONE_STAIRS, 3, p->coord_base_mode), 1, 0, -1);

    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 4; ++x)
        {
            sc_clear_upwards(c, p, x, 6, z);
            v_fill_down(c, p, VBLK_COBBLE, 0, x, -1, z);
        }

    spawn_villagers(c, p, 1, 1, 2, 1, farmer_type);
    return 1;
}

/* House1.addComponentParts. */
static int house1_parts(struct sc_ctx *c, struct piece *p)
{
    if (!settle(c, p, 9)) return 1;

    v_fill(c, p, 1, 1, 1, 7, 5, 4, VBLK_AIR, VBLK_AIR, 0);
    v_fill(c, p, 0, 0, 0, 8, 0, 5, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 5, 0, 8, 5, 5, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 6, 1, 8, 6, 4, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 7, 2, 8, 7, 3, VBLK_COBBLE, VBLK_COBBLE, 0);
    int var4 = sc_orient_meta(VBLK_OAK_STAIRS, 3, p->coord_base_mode);
    int var5 = sc_orient_meta(VBLK_OAK_STAIRS, 2, p->coord_base_mode);

    for (int y = -1; y <= 2; ++y)
        for (int x = 0; x <= 8; ++x)
        {
            v_place(c, p, VBLK_OAK_STAIRS, var4, x, 6 + y, y);
            v_place(c, p, VBLK_OAK_STAIRS, var5, x, 6 + y, 5 - y);
        }

    v_fill(c, p, 0, 1, 0, 0, 1, 5, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 1, 1, 5, 8, 1, 5, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 8, 1, 0, 8, 1, 4, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 2, 1, 0, 7, 1, 0, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 2, 0, 0, 4, 0, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 2, 5, 0, 4, 5, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 8, 2, 5, 8, 4, 5, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 8, 2, 0, 8, 4, 0, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 2, 1, 0, 4, 4, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 1, 2, 5, 7, 4, 5, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 8, 2, 1, 8, 4, 4, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 1, 2, 0, 7, 4, 0, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 4, 2, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 5, 2, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 6, 2, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 4, 3, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 5, 3, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 6, 3, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 2, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 2, 3);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 3, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 3, 3);
    v_place(c, p, VBLK_GLASS_PANE, 0, 8, 2, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 8, 2, 3);
    v_place(c, p, VBLK_GLASS_PANE, 0, 8, 3, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 8, 3, 3);
    v_place(c, p, VBLK_GLASS_PANE, 0, 2, 2, 5);
    v_place(c, p, VBLK_GLASS_PANE, 0, 3, 2, 5);
    v_place(c, p, VBLK_GLASS_PANE, 0, 5, 2, 5);
    v_place(c, p, VBLK_GLASS_PANE, 0, 6, 2, 5);
    v_fill(c, p, 1, 4, 1, 7, 4, 1, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 1, 4, 4, 7, 4, 4, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 1, 3, 4, 7, 3, 4, VBLK_BOOKSHELF, VBLK_BOOKSHELF, 0);
    v_place(c, p, VBLK_PLANKS, 0, 7, 1, 4);
    v_place(c, p, VBLK_OAK_STAIRS, sc_orient_meta(VBLK_OAK_STAIRS, 0, p->coord_base_mode), 7, 1, 3);
    int var6 = sc_orient_meta(VBLK_OAK_STAIRS, 3, p->coord_base_mode);

    v_place(c, p, VBLK_OAK_STAIRS, var6, 6, 1, 4);
    v_place(c, p, VBLK_OAK_STAIRS, var6, 5, 1, 4);
    v_place(c, p, VBLK_OAK_STAIRS, var6, 4, 1, 4);
    v_place(c, p, VBLK_OAK_STAIRS, var6, 3, 1, 4);
    v_place(c, p, VBLK_FENCE, 0, 6, 1, 3);
    v_place(c, p, VBLK_PRESSURE_PLATE, 0, 6, 2, 3);
    v_place(c, p, VBLK_FENCE, 0, 4, 1, 3);
    v_place(c, p, VBLK_PRESSURE_PLATE, 0, 4, 2, 3);
    v_place(c, p, VBLK_CRAFTING, 0, 7, 1, 1);
    v_place(c, p, VBLK_AIR, 0, 1, 1, 0);
    v_place(c, p, VBLK_AIR, 0, 1, 2, 0);
    place_door(c, p, 1, 1, 0, sc_orient_meta(VBLK_WOODEN_DOOR, 1, p->coord_base_mode));

    if (sc_block_at(c, p, 1, 0, -1) == VBLK_AIR && sc_block_at(c, p, 1, -1, -1) != VBLK_AIR)
        v_place(c, p, VBLK_STONE_STAIRS, sc_orient_meta(VBLK_STONE_STAIRS, 3, p->coord_base_mode), 1, 0, -1);

    for (int z = 0; z < 6; ++z)
        for (int x = 0; x < 9; ++x)
        {
            sc_clear_upwards(c, p, x, 9, z);
            v_fill_down(c, p, VBLK_COBBLE, 0, x, -1, z);
        }

    spawn_villagers(c, p, 2, 1, 2, 1, librarian_type);
    return 1;
}

/* House2.addComponentParts: the blacksmith, with its chest. */
static int house2_parts(struct sc_ctx *c, struct piece *p)
{
    if (!settle(c, p, 6)) return 1;

    v_fill(c, p, 0, 1, 0, 9, 4, 6, VBLK_AIR, VBLK_AIR, 0);
    v_fill(c, p, 0, 0, 0, 9, 0, 6, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 4, 0, 9, 4, 6, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 5, 0, 9, 5, 6, VBLK_STONE_SLAB, VBLK_STONE_SLAB, 0);
    v_fill(c, p, 1, 5, 1, 8, 5, 5, VBLK_AIR, VBLK_AIR, 0);
    v_fill(c, p, 1, 1, 0, 2, 3, 0, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 0, 1, 0, 0, 4, 0, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 3, 1, 0, 3, 4, 0, VBLK_LOG, VBLK_LOG, 0);
    v_fill(c, p, 0, 1, 6, 0, 4, 6, VBLK_LOG, VBLK_LOG, 0);
    v_place(c, p, VBLK_PLANKS, 0, 3, 3, 1);
    v_fill(c, p, 3, 1, 2, 3, 3, 2, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 4, 1, 3, 5, 3, 3, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 0, 1, 1, 0, 3, 5, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 1, 1, 6, 5, 3, 6, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 5, 1, 0, 5, 3, 0, VBLK_FENCE, VBLK_FENCE, 0);
    v_fill(c, p, 9, 1, 0, 9, 3, 0, VBLK_FENCE, VBLK_FENCE, 0);
    v_fill(c, p, 6, 1, 4, 9, 4, 6, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_place(c, p, VBLK_FLOWING_LAVA, 0, 7, 1, 5);
    v_place(c, p, VBLK_FLOWING_LAVA, 0, 8, 1, 5);
    v_place(c, p, VBLK_IRON_BARS, 0, 9, 2, 5);
    v_place(c, p, VBLK_IRON_BARS, 0, 9, 2, 4);
    v_fill(c, p, 7, 2, 4, 8, 2, 5, VBLK_AIR, VBLK_AIR, 0);
    v_place(c, p, VBLK_COBBLE, 0, 6, 1, 3);
    v_place(c, p, VBLK_FURNACE, 0, 6, 2, 3);
    v_place(c, p, VBLK_FURNACE, 0, 6, 3, 3);
    v_place(c, p, VBLK_DOUBLE_SLAB, 0, 8, 1, 1);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 2, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 2, 4);
    v_place(c, p, VBLK_GLASS_PANE, 0, 2, 2, 6);
    v_place(c, p, VBLK_GLASS_PANE, 0, 4, 2, 6);
    v_place(c, p, VBLK_FENCE, 0, 2, 1, 4);
    v_place(c, p, VBLK_PRESSURE_PLATE, 0, 2, 2, 4);
    v_place(c, p, VBLK_PLANKS, 0, 1, 1, 5);
    v_place(c, p, VBLK_OAK_STAIRS, sc_orient_meta(VBLK_OAK_STAIRS, 3, p->coord_base_mode), 2, 1, 5);
    v_place(c, p, VBLK_OAK_STAIRS, sc_orient_meta(VBLK_OAK_STAIRS, 1, p->coord_base_mode), 1, 1, 4);

    if (!p->u.village.chest)
    {
        int x = sc_x_with_offset(p, 5, 5), y = sc_y_with_offset(p, 1), z = sc_z_with_offset(p, 5, 5);

        if (sc_in_step_box(c, x, y, z))
        {
            p->u.village.chest = 1;
            int count = 3 + jr_int_n(c->rand, 6);

            sc_chest_contents(c, p, loot_table_by_name("blacksmith"), NULL, 5, 1, 5, count);
        }
    }

    for (int x = 6; x <= 8; ++x)
        if (sc_block_at(c, p, x, 0, -1) == VBLK_AIR && sc_block_at(c, p, x, -1, -1) != VBLK_AIR)
            v_place(c, p, VBLK_STONE_STAIRS, sc_orient_meta(VBLK_STONE_STAIRS, 3, p->coord_base_mode), x, 0, -1);

    for (int z = 0; z < 7; ++z)
        for (int x = 0; x < 10; ++x)
        {
            sc_clear_upwards(c, p, x, 6, z);
            v_fill_down(c, p, VBLK_COBBLE, 0, x, -1, z);
        }

    spawn_villagers(c, p, 7, 1, 1, 1, blacksmith_type);
    return 1;
}

/* House3.addComponentParts. */
static int house3_parts(struct sc_ctx *c, struct piece *p)
{
    if (!settle(c, p, 7)) return 1;

    v_fill(c, p, 1, 1, 1, 7, 4, 4, VBLK_AIR, VBLK_AIR, 0);
    v_fill(c, p, 2, 1, 6, 8, 4, 10, VBLK_AIR, VBLK_AIR, 0);
    v_fill(c, p, 2, 0, 5, 8, 0, 10, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 1, 0, 1, 7, 0, 4, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 0, 0, 0, 0, 3, 5, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 8, 0, 0, 8, 3, 10, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 1, 0, 0, 7, 2, 0, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 1, 0, 5, 2, 1, 5, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 2, 0, 6, 2, 3, 10, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 3, 0, 10, 7, 3, 10, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 1, 2, 0, 7, 3, 0, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 1, 2, 5, 2, 3, 5, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 0, 4, 1, 8, 4, 1, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 0, 4, 4, 3, 4, 4, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 0, 5, 2, 8, 5, 3, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_place(c, p, VBLK_PLANKS, 0, 0, 4, 2);
    v_place(c, p, VBLK_PLANKS, 0, 0, 4, 3);
    v_place(c, p, VBLK_PLANKS, 0, 8, 4, 2);
    v_place(c, p, VBLK_PLANKS, 0, 8, 4, 3);
    v_place(c, p, VBLK_PLANKS, 0, 8, 4, 4);
    int var4 = sc_orient_meta(VBLK_OAK_STAIRS, 3, p->coord_base_mode);
    int var5 = sc_orient_meta(VBLK_OAK_STAIRS, 2, p->coord_base_mode);

    for (int y = -1; y <= 2; ++y)
        for (int x = 0; x <= 8; ++x)
        {
            v_place(c, p, VBLK_OAK_STAIRS, var4, x, 4 + y, y);

            if ((y > -1 || x <= 1) && (y > 0 || x <= 3) && (y > 1 || x <= 4 || x >= 6))
                v_place(c, p, VBLK_OAK_STAIRS, var5, x, 4 + y, 5 - y);
        }

    v_fill(c, p, 3, 4, 5, 3, 4, 10, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 7, 4, 2, 7, 4, 10, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 4, 5, 4, 4, 5, 10, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 6, 5, 4, 6, 5, 10, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 5, 6, 3, 5, 6, 10, VBLK_PLANKS, VBLK_PLANKS, 0);
    int var6 = sc_orient_meta(VBLK_OAK_STAIRS, 0, p->coord_base_mode);

    for (int y = 4; y >= 1; --y)
    {
        v_place(c, p, VBLK_PLANKS, 0, y, 2 + y, 7 - y);

        for (int x = 8 - y; x <= 10; ++x)
            v_place(c, p, VBLK_OAK_STAIRS, var6, y, 2 + y, x);
    }

    int var7 = sc_orient_meta(VBLK_OAK_STAIRS, 1, p->coord_base_mode);

    v_place(c, p, VBLK_PLANKS, 0, 6, 6, 3);
    v_place(c, p, VBLK_PLANKS, 0, 7, 5, 4);
    v_place(c, p, VBLK_OAK_STAIRS, var7, 6, 6, 4);

    for (int x = 6; x <= 8; ++x)
        for (int z = 5; z <= 10; ++z)
            v_place(c, p, VBLK_OAK_STAIRS, var7, x, 12 - x, z);

    v_place(c, p, VBLK_LOG, 0, 0, 2, 1);
    v_place(c, p, VBLK_LOG, 0, 0, 2, 4);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 2, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 2, 3);
    v_place(c, p, VBLK_LOG, 0, 4, 2, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 5, 2, 0);
    v_place(c, p, VBLK_LOG, 0, 6, 2, 0);
    v_place(c, p, VBLK_LOG, 0, 8, 2, 1);
    v_place(c, p, VBLK_GLASS_PANE, 0, 8, 2, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 8, 2, 3);
    v_place(c, p, VBLK_LOG, 0, 8, 2, 4);
    v_place(c, p, VBLK_PLANKS, 0, 8, 2, 5);
    v_place(c, p, VBLK_LOG, 0, 8, 2, 6);
    v_place(c, p, VBLK_GLASS_PANE, 0, 8, 2, 7);
    v_place(c, p, VBLK_GLASS_PANE, 0, 8, 2, 8);
    v_place(c, p, VBLK_LOG, 0, 8, 2, 9);
    v_place(c, p, VBLK_LOG, 0, 2, 2, 6);
    v_place(c, p, VBLK_GLASS_PANE, 0, 2, 2, 7);
    v_place(c, p, VBLK_GLASS_PANE, 0, 2, 2, 8);
    v_place(c, p, VBLK_LOG, 0, 2, 2, 9);
    v_place(c, p, VBLK_LOG, 0, 4, 4, 10);
    v_place(c, p, VBLK_GLASS_PANE, 0, 5, 4, 10);
    v_place(c, p, VBLK_LOG, 0, 6, 4, 10);
    v_place(c, p, VBLK_PLANKS, 0, 5, 5, 10);
    v_place(c, p, VBLK_AIR, 0, 2, 1, 0);
    v_place(c, p, VBLK_AIR, 0, 2, 2, 0);
    v_place(c, p, VBLK_TORCH, 0, 2, 3, 1);
    place_door(c, p, 2, 1, 0, sc_orient_meta(VBLK_WOODEN_DOOR, 1, p->coord_base_mode));
    v_fill(c, p, 1, 0, -1, 3, 2, -1, VBLK_AIR, VBLK_AIR, 0);

    if (sc_block_at(c, p, 2, 0, -1) == VBLK_AIR && sc_block_at(c, p, 2, -1, -1) != VBLK_AIR)
        v_place(c, p, VBLK_STONE_STAIRS, sc_orient_meta(VBLK_STONE_STAIRS, 3, p->coord_base_mode), 2, 0, -1);

    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 9; ++x)
        {
            sc_clear_upwards(c, p, x, 7, z);
            v_fill_down(c, p, VBLK_COBBLE, 0, x, -1, z);
        }

    for (int z = 5; z < 11; ++z)
        for (int x = 2; x < 9; ++x)
        {
            sc_clear_upwards(c, p, x, 7, z);
            v_fill_down(c, p, VBLK_COBBLE, 0, x, -1, z);
        }

    spawn_villagers(c, p, 4, 1, 2, 2, farmer_type);
    return 1;
}

/* House3's villagers are the base type. */

/* Church.addComponentParts. */
static int church_parts(struct sc_ctx *c, struct piece *p)
{
    if (!settle(c, p, 12)) return 1;

    v_fill(c, p, 1, 1, 1, 3, 3, 7, VBLK_AIR, VBLK_AIR, 0);
    v_fill(c, p, 1, 5, 1, 3, 9, 3, VBLK_AIR, VBLK_AIR, 0);
    v_fill(c, p, 1, 0, 0, 3, 0, 8, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 1, 1, 0, 3, 10, 0, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 1, 1, 0, 10, 3, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 4, 1, 1, 4, 10, 3, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 0, 4, 0, 4, 7, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 4, 0, 4, 4, 4, 7, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 1, 1, 8, 3, 4, 8, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 1, 5, 4, 3, 10, 4, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 1, 5, 5, 3, 5, 7, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 9, 0, 4, 9, 4, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 0, 4, 0, 4, 4, 4, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_place(c, p, VBLK_COBBLE, 0, 0, 11, 2);
    v_place(c, p, VBLK_COBBLE, 0, 4, 11, 2);
    v_place(c, p, VBLK_COBBLE, 0, 2, 11, 0);
    v_place(c, p, VBLK_COBBLE, 0, 2, 11, 4);
    v_place(c, p, VBLK_COBBLE, 0, 1, 1, 6);
    v_place(c, p, VBLK_COBBLE, 0, 1, 1, 7);
    v_place(c, p, VBLK_COBBLE, 0, 2, 1, 7);
    v_place(c, p, VBLK_COBBLE, 0, 3, 1, 6);
    v_place(c, p, VBLK_COBBLE, 0, 3, 1, 7);
    v_place(c, p, VBLK_STONE_STAIRS, sc_orient_meta(VBLK_STONE_STAIRS, 3, p->coord_base_mode), 1, 1, 5);
    v_place(c, p, VBLK_STONE_STAIRS, sc_orient_meta(VBLK_STONE_STAIRS, 3, p->coord_base_mode), 2, 1, 6);
    v_place(c, p, VBLK_STONE_STAIRS, sc_orient_meta(VBLK_STONE_STAIRS, 3, p->coord_base_mode), 3, 1, 5);
    v_place(c, p, VBLK_STONE_STAIRS, sc_orient_meta(VBLK_STONE_STAIRS, 1, p->coord_base_mode), 1, 2, 7);
    v_place(c, p, VBLK_STONE_STAIRS, sc_orient_meta(VBLK_STONE_STAIRS, 0, p->coord_base_mode), 3, 2, 7);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 2, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 3, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 4, 2, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 4, 3, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 6, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 7, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 4, 6, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 4, 7, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 2, 6, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 2, 7, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 2, 6, 4);
    v_place(c, p, VBLK_GLASS_PANE, 0, 2, 7, 4);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 3, 6);
    v_place(c, p, VBLK_GLASS_PANE, 0, 4, 3, 6);
    v_place(c, p, VBLK_GLASS_PANE, 0, 2, 3, 8);
    v_place(c, p, VBLK_TORCH, 0, 2, 4, 7);
    v_place(c, p, VBLK_TORCH, 0, 1, 4, 6);
    v_place(c, p, VBLK_TORCH, 0, 3, 4, 6);
    v_place(c, p, VBLK_TORCH, 0, 2, 4, 5);
    int ladder = sc_orient_meta(VBLK_LADDER, 4, p->coord_base_mode);

    for (int y = 1; y <= 9; ++y)
        v_place(c, p, VBLK_LADDER, ladder, 3, y, 3);

    v_place(c, p, VBLK_AIR, 0, 2, 1, 0);
    v_place(c, p, VBLK_AIR, 0, 2, 2, 0);
    place_door(c, p, 2, 1, 0, sc_orient_meta(VBLK_WOODEN_DOOR, 1, p->coord_base_mode));

    if (sc_block_at(c, p, 2, 0, -1) == VBLK_AIR && sc_block_at(c, p, 2, -1, -1) != VBLK_AIR)
        v_place(c, p, VBLK_STONE_STAIRS, sc_orient_meta(VBLK_STONE_STAIRS, 3, p->coord_base_mode), 2, 0, -1);

    for (int z = 0; z < 9; ++z)
        for (int x = 0; x < 5; ++x)
        {
            sc_clear_upwards(c, p, x, 12, z);
            v_fill_down(c, p, VBLK_COBBLE, 0, x, -1, z);
        }

    spawn_villagers(c, p, 2, 1, 2, 1, priest_type);
    return 1;
}

/* Hall.addComponentParts. */
static int hall_parts(struct sc_ctx *c, struct piece *p)
{
    if (!settle(c, p, 7)) return 1;

    v_fill(c, p, 1, 1, 1, 7, 4, 4, VBLK_AIR, VBLK_AIR, 0);
    v_fill(c, p, 2, 1, 6, 8, 4, 10, VBLK_AIR, VBLK_AIR, 0);
    v_fill(c, p, 2, 0, 6, 8, 0, 10, VBLK_DIRT, VBLK_DIRT, 0);
    v_place(c, p, VBLK_COBBLE, 0, 6, 0, 6);
    v_fill(c, p, 2, 1, 6, 2, 1, 10, VBLK_FENCE, VBLK_FENCE, 0);
    v_fill(c, p, 8, 1, 6, 8, 1, 10, VBLK_FENCE, VBLK_FENCE, 0);
    v_fill(c, p, 3, 1, 10, 7, 1, 10, VBLK_FENCE, VBLK_FENCE, 0);
    v_fill(c, p, 1, 0, 1, 7, 0, 4, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 0, 0, 0, 0, 3, 5, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 8, 0, 0, 8, 3, 5, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 1, 0, 0, 7, 1, 0, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 1, 0, 5, 7, 1, 5, VBLK_COBBLE, VBLK_COBBLE, 0);
    v_fill(c, p, 1, 2, 0, 7, 3, 0, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 1, 2, 5, 7, 3, 5, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 0, 4, 1, 8, 4, 1, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 0, 4, 4, 8, 4, 4, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_fill(c, p, 0, 5, 2, 8, 5, 3, VBLK_PLANKS, VBLK_PLANKS, 0);
    v_place(c, p, VBLK_PLANKS, 0, 0, 4, 2);
    v_place(c, p, VBLK_PLANKS, 0, 0, 4, 3);
    v_place(c, p, VBLK_PLANKS, 0, 8, 4, 2);
    v_place(c, p, VBLK_PLANKS, 0, 8, 4, 3);
    int var4 = sc_orient_meta(VBLK_OAK_STAIRS, 3, p->coord_base_mode);
    int var5 = sc_orient_meta(VBLK_OAK_STAIRS, 2, p->coord_base_mode);

    for (int y = -1; y <= 2; ++y)
        for (int x = 0; x <= 8; ++x)
        {
            v_place(c, p, VBLK_OAK_STAIRS, var4, x, 4 + y, y);
            v_place(c, p, VBLK_OAK_STAIRS, var5, x, 4 + y, 5 - y);
        }

    v_place(c, p, VBLK_LOG, 0, 0, 2, 1);
    v_place(c, p, VBLK_LOG, 0, 0, 2, 4);
    v_place(c, p, VBLK_LOG, 0, 8, 2, 1);
    v_place(c, p, VBLK_LOG, 0, 8, 2, 4);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 2, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 0, 2, 3);
    v_place(c, p, VBLK_GLASS_PANE, 0, 8, 2, 2);
    v_place(c, p, VBLK_GLASS_PANE, 0, 8, 2, 3);
    v_place(c, p, VBLK_GLASS_PANE, 0, 2, 2, 5);
    v_place(c, p, VBLK_GLASS_PANE, 0, 3, 2, 5);
    v_place(c, p, VBLK_GLASS_PANE, 0, 5, 2, 0);
    v_place(c, p, VBLK_GLASS_PANE, 0, 6, 2, 5);
    v_place(c, p, VBLK_FENCE, 0, 2, 1, 3);
    v_place(c, p, VBLK_PRESSURE_PLATE, 0, 2, 2, 3);
    v_place(c, p, VBLK_PLANKS, 0, 1, 1, 4);
    v_place(c, p, VBLK_OAK_STAIRS, sc_orient_meta(VBLK_OAK_STAIRS, 3, p->coord_base_mode), 2, 1, 4);
    v_place(c, p, VBLK_OAK_STAIRS, sc_orient_meta(VBLK_OAK_STAIRS, 1, p->coord_base_mode), 1, 1, 3);
    v_fill(c, p, 5, 0, 1, 7, 0, 3, VBLK_DOUBLE_SLAB, VBLK_DOUBLE_SLAB, 0);
    v_place(c, p, VBLK_DOUBLE_SLAB, 0, 6, 1, 1);
    v_place(c, p, VBLK_DOUBLE_SLAB, 0, 6, 1, 2);
    v_place(c, p, VBLK_AIR, 0, 2, 1, 0);
    v_place(c, p, VBLK_AIR, 0, 2, 2, 0);
    v_place(c, p, VBLK_TORCH, 0, 2, 3, 1);
    place_door(c, p, 2, 1, 0, sc_orient_meta(VBLK_WOODEN_DOOR, 1, p->coord_base_mode));

    if (sc_block_at(c, p, 2, 0, -1) == VBLK_AIR && sc_block_at(c, p, 2, -1, -1) != VBLK_AIR)
        v_place(c, p, VBLK_STONE_STAIRS, sc_orient_meta(VBLK_STONE_STAIRS, 3, p->coord_base_mode), 2, 0, -1);

    v_place(c, p, VBLK_AIR, 0, 6, 1, 5);
    v_place(c, p, VBLK_AIR, 0, 6, 2, 5);
    v_place(c, p, VBLK_TORCH, 0, 6, 3, 4);
    place_door(c, p, 6, 1, 5, sc_orient_meta(VBLK_WOODEN_DOOR, 1, p->coord_base_mode));

    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 9; ++x)
        {
            sc_clear_upwards(c, p, x, 7, z);
            v_fill_down(c, p, VBLK_COBBLE, 0, x, -1, z);
        }

    spawn_villagers(c, p, 4, 1, 2, 2, hall_type);
    return 1;
}

int village_blocks(struct sc_ctx *c, struct piece *p)
{
    if (p->kind != PIECE_VILLAGE) return 1;

    switch (p->u.village.kind)
    {
    case V_WELL: return well_parts(c, p);
    case V_ROAD: return path_parts(c, p);
    case V_TORCH: return torch_parts(c, p);
    case V_FIELD1: return field1_parts(c, p);
    case V_FIELD2: return field2_parts(c, p);
    case V_WOODHUT: return woodhut_parts(c, p);
    case V_HOUSE4: return house4_parts(c, p);
    case V_HOUSE1: return house1_parts(c, p);
    case V_HOUSE2: return house2_parts(c, p);
    case V_HALL: return hall_parts(c, p);
    case V_CHURCH: return church_parts(c, p);
    case V_HOUSE3: return house3_parts(c, p);
    default: return 1;
    }
}