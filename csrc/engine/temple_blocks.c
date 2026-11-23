/* ComponentScatteredFeaturePieces' addComponentParts, the three temple kinds'
 * block halves: the desert pyramid (its rooms, the TNT trap under the pressure
 * plate and the four chests), the jungle pyramid (its randomized cobblestone
 * shell, the two dispenser traps, the tripwire and redstone wiring and the two
 * chests) and the witch hut (which spawns an EntityWitch). Every Random draw is
 * in Java's order; see structure_blocks.c for the helpers and the env the
 * writes run in. The piece's height (Feature.func_74935_a) is settled here too,
 * on the first step that runs the piece: it moves the piece's bounding box down
 * to the ground the step's box covers.
 *
 * The block callbacks these pieces' ids carry (the dispenser's facing, the
 * tripwire and tripwire hook state machines, the redstone wire's propagation,
 * the lever's support test) live in blockcb.c. */
#include "temple_blocks.h"

#include "blocks.h"
#include "loot.h"
#include "structure_blocks.h"
#include "tileentity.h"
#include "world.h"

#include <string.h>

/* The blocks these pieces name, from Blocks. */
enum {
    TB_AIR = 0, TB_COBBLESTONE = 4, TB_PLANKS = 5, TB_LOG = 17, TB_DISPENSER = 23,
    TB_SANDSTONE = 24, TB_STICKY_PISTON = 29, TB_WOOL = 35, TB_STONE_SLAB = 44,
    TB_TNT = 46, TB_MOSSY_COBBLESTONE = 48, TB_OAK_STAIRS = 53, TB_REDSTONE_WIRE = 55,
    TB_CRAFTING_TABLE = 58, TB_STONE_STAIRS = 67, TB_LEVER = 69, TB_STONE_PRESSURE_PLATE = 70,
    TB_FENCE = 85, TB_UNPOWERED_REPEATER = 93, TB_STONEBRICK = 98, TB_VINE = 106,
    TB_CAULDRON = 118, TB_SANDSTONE_STAIRS = 128, TB_TRIPWIRE_HOOK = 131, TB_TRIPWIRE = 132,
    TB_SPRUCE_STAIRS = 134, TB_FLOWER_POT = 140
};

/* BlockLever.func_149819_b: the metadata a lever's facing turns into. */
static int lever_attach(int meta)
{
    switch (meta)
    {
        case 0: return 0;
        case 1: return 5;
        case 2: return 4;
        case 3: return 3;
        case 4: return 2;
        case 5: return 1;
        default: return -1;
    }
}

/* Direction.offsetX, offsetZ and rotateOpposite, the four horizontal
 * directions. */
static const int OFF_X[4] = {0, -1, 0, 1};
static const int OFF_Z[4] = {1, 0, -1, 0};

/* WorldProvider.getAverageGroundLevel: 64 in every non-flat overworld. */
#define AVERAGE_GROUND_LEVEL 64

static int imax(int a, int b) { return a > b ? a : b; }

/* ------------------------------------------------------------- the height */

/* Chunk.getTopFilledSegment: the yBase of the topmost section, 0 when the
 * chunk has none. */
static int top_filled_segment(const struct chunk *c)
{
    for (int s = 15; s >= 0; --s)
        if (c->mask & (1 << s)) return s * 16;

    return 0;
}

/* World.getTopSolidOrLiquidBlock: from the top of the chunk's highest section
 * down, the first block whose material blocks movement and is not leaves, one
 * above it; -1 when the column has none. */
static int top_solid_or_liquid(struct world *w, int x, int z)
{
    struct chunk *c = world_load_chunk(w, x >> 4, z >> 4);

    for (int y = top_filled_segment(c) + 15; y > 0; --y)
    {
        int id = world_get_block(w, x, y, z);
        const struct material_def *m = &MATERIALS[BLOCKS[id & 4095].material];

        if (m->blocks_movement && strcmp(m->name, "leaves") != 0) return y + 1;
    }

    return -1;
}

/* Feature.func_74935_a: the height the piece settles at, the mean of the
 * ground under its footprint where the step's box covers it, then the piece's
 * bounding box moves down by that height (the HPos field caches it). */
static int feature_ground_level(struct sc_ctx *c, struct piece *p, int yoff)
{
    if (p->u.temple.hpos >= 0) return 1;

    int sum = 0, count = 0;

    for (int z = p->bb.minZ; z <= p->bb.maxZ; ++z)
    {
        for (int x = p->bb.minX; x <= p->bb.maxX; ++x)
        {
            if (!sc_in_step_box(c, x, 64, z)) continue;

            sum += imax(top_solid_or_liquid(c->w, x, z), AVERAGE_GROUND_LEVEL);
            ++count;
        }
    }

    if (count == 0) return 0;

    p->u.temple.hpos = sum / count;
    bbox_offset(&p->bb, 0, p->u.temple.hpos - p->bb.minY + yoff, 0);
    return 1;
}

/* --------------------------------------------------------- desert pyramid */

/* JunglePyramid and DesertPyramid's chests draw their table's book stack and
 * their count before the call, Java's argument order (func_92080_a's
 * func_92114_b(rand) argument is evaluated before 2 + rand.nextInt(5)), and
 * before the chest's own position test. */
static int temple_chest(struct sc_ctx *c, struct piece *p, const char *table_name, int tx, int ty, int tz, int *placed)
{
    const struct loot_table *table = loot_table_by_name(table_name);
    struct loot_book book;

    loot_book_build(c->rand, &table->entries[table->n - 1], &book);
    int count = 2 + jr_int_n(c->rand, 5);
    int r = sc_chest_contents(c, p, table, &book, tx, ty, tz, count);

    loot_book_free(&book);
    *placed = r;
    return r;
}

/* DesertPyramid.addComponentParts. The pyramid keeps the y its constructor was
 * given (64): unlike the jungle pyramid and the hut it never calls
 * func_74935_a, so its base sits at 60 whatever the ground under it is. */
static int desert_pyramid_parts(struct sc_ctx *c, struct piece *p)
{
    int X = p->u.temple.size_x, Z = p->u.temple.size_z;
    int var4, var5;

    sc_fill(c, p, 0, -4, 0, X - 1, 0, Z - 1, TB_SANDSTONE, TB_SANDSTONE, 0);

    for (var4 = 1; var4 <= 9; ++var4)
    {
        sc_fill(c, p, var4, var4, var4, X - 1 - var4, var4, Z - 1 - var4, TB_SANDSTONE, TB_SANDSTONE, 0);
        sc_fill(c, p, var4 + 1, var4, var4 + 1, X - 2 - var4, var4, Z - 2 - var4, TB_AIR, TB_AIR, 0);
    }

    for (var4 = 0; var4 < X; ++var4)
        for (var5 = 0; var5 < Z; ++var5)
            sc_fill_down(c, p, TB_SANDSTONE, 0, var4, -5, var5);

    var4 = sc_orient_meta(TB_SANDSTONE_STAIRS, 3, p->coord_base_mode);
    var5 = sc_orient_meta(TB_SANDSTONE_STAIRS, 2, p->coord_base_mode);
    int var13 = sc_orient_meta(TB_SANDSTONE_STAIRS, 0, p->coord_base_mode);
    int var7 = sc_orient_meta(TB_SANDSTONE_STAIRS, 1, p->coord_base_mode);
    int var8 = 1;
    int var9 = 11;

    sc_fill(c, p, 0, 0, 0, 4, 9, 4, TB_SANDSTONE, TB_AIR, 0);
    sc_fill(c, p, 1, 10, 1, 3, 10, 3, TB_SANDSTONE, TB_SANDSTONE, 0);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var4, 2, 10, 0);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var5, 2, 10, 4);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var13, 0, 10, 2);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var7, 4, 10, 2);
    sc_fill(c, p, X - 5, 0, 0, X - 1, 9, 4, TB_SANDSTONE, TB_AIR, 0);
    sc_fill(c, p, X - 4, 10, 1, X - 2, 10, 3, TB_SANDSTONE, TB_SANDSTONE, 0);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var4, X - 3, 10, 0);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var5, X - 3, 10, 4);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var13, X - 5, 10, 2);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var7, X - 1, 10, 2);
    sc_fill(c, p, 8, 0, 0, 12, 4, 4, TB_SANDSTONE, TB_AIR, 0);
    sc_fill(c, p, 9, 1, 0, 11, 3, 4, TB_AIR, TB_AIR, 0);
    sc_place(c, p, TB_SANDSTONE, 2, 9, 1, 1);
    sc_place(c, p, TB_SANDSTONE, 2, 9, 2, 1);
    sc_place(c, p, TB_SANDSTONE, 2, 9, 3, 1);
    sc_place(c, p, TB_SANDSTONE, 2, 10, 3, 1);
    sc_place(c, p, TB_SANDSTONE, 2, 11, 3, 1);
    sc_place(c, p, TB_SANDSTONE, 2, 11, 2, 1);
    sc_place(c, p, TB_SANDSTONE, 2, 11, 1, 1);
    sc_fill(c, p, 4, 1, 1, 8, 3, 3, TB_SANDSTONE, TB_AIR, 0);
    sc_fill(c, p, 4, 1, 2, 8, 2, 2, TB_AIR, TB_AIR, 0);
    sc_fill(c, p, 12, 1, 1, 16, 3, 3, TB_SANDSTONE, TB_AIR, 0);
    sc_fill(c, p, 12, 1, 2, 16, 2, 2, TB_AIR, TB_AIR, 0);
    sc_fill(c, p, 5, 4, 5, X - 6, 4, Z - 6, TB_SANDSTONE, TB_SANDSTONE, 0);
    sc_fill(c, p, 9, 4, 9, 11, 4, 11, TB_AIR, TB_AIR, 0);
    sc_fill_meta(c, p, 8, 1, 8, 8, 3, 8, TB_SANDSTONE, 2, TB_SANDSTONE, 2, 0);
    sc_fill_meta(c, p, 12, 1, 8, 12, 3, 8, TB_SANDSTONE, 2, TB_SANDSTONE, 2, 0);
    sc_fill_meta(c, p, 8, 1, 12, 8, 3, 12, TB_SANDSTONE, 2, TB_SANDSTONE, 2, 0);
    sc_fill_meta(c, p, 12, 1, 12, 12, 3, 12, TB_SANDSTONE, 2, TB_SANDSTONE, 2, 0);
    sc_fill(c, p, 1, 1, 5, 4, 4, 11, TB_SANDSTONE, TB_SANDSTONE, 0);
    sc_fill(c, p, X - 5, 1, 5, X - 2, 4, 11, TB_SANDSTONE, TB_SANDSTONE, 0);
    sc_fill(c, p, 6, 7, 9, 6, 7, 11, TB_SANDSTONE, TB_SANDSTONE, 0);
    sc_fill(c, p, X - 7, 7, 9, X - 7, 7, 11, TB_SANDSTONE, TB_SANDSTONE, 0);
    sc_fill_meta(c, p, 5, 5, 9, 5, 7, 11, TB_SANDSTONE, 2, TB_SANDSTONE, 2, 0);
    sc_fill_meta(c, p, X - 6, 5, 9, X - 6, 7, 11, TB_SANDSTONE, 2, TB_SANDSTONE, 2, 0);
    sc_place(c, p, TB_AIR, 0, 5, 5, 10);
    sc_place(c, p, TB_AIR, 0, 5, 6, 10);
    sc_place(c, p, TB_AIR, 0, 6, 6, 10);
    sc_place(c, p, TB_AIR, 0, X - 6, 5, 10);
    sc_place(c, p, TB_AIR, 0, X - 6, 6, 10);
    sc_place(c, p, TB_AIR, 0, X - 7, 6, 10);
    sc_fill(c, p, 2, 4, 4, 2, 6, 4, TB_AIR, TB_AIR, 0);
    sc_fill(c, p, X - 3, 4, 4, X - 3, 6, 4, TB_AIR, TB_AIR, 0);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var4, 2, 4, 5);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var4, 2, 3, 4);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var4, X - 3, 4, 5);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var4, X - 3, 3, 4);
    sc_fill(c, p, 1, 1, 3, 2, 2, 3, TB_SANDSTONE, TB_SANDSTONE, 0);
    sc_fill(c, p, X - 3, 1, 3, X - 2, 2, 3, TB_SANDSTONE, TB_SANDSTONE, 0);
    sc_place(c, p, TB_SANDSTONE_STAIRS, 0, 1, 1, 2);
    sc_place(c, p, TB_SANDSTONE_STAIRS, 0, X - 2, 1, 2);
    sc_place(c, p, TB_STONE_SLAB, 1, 1, 2, 2);
    sc_place(c, p, TB_STONE_SLAB, 1, X - 2, 2, 2);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var7, 2, 1, 2);
    sc_place(c, p, TB_SANDSTONE_STAIRS, var13, X - 3, 1, 2);
    sc_fill(c, p, 4, 3, 5, 4, 3, 18, TB_SANDSTONE, TB_SANDSTONE, 0);
    sc_fill(c, p, X - 5, 3, 5, X - 5, 3, 17, TB_SANDSTONE, TB_SANDSTONE, 0);
    sc_fill(c, p, 3, 1, 5, 4, 2, 16, TB_AIR, TB_AIR, 0);
    sc_fill(c, p, X - 6, 1, 5, X - 5, 2, 16, TB_AIR, TB_AIR, 0);

    int var10;

    for (var10 = 5; var10 <= 17; var10 += 2)
    {
        sc_place(c, p, TB_SANDSTONE, 2, 4, 1, var10);
        sc_place(c, p, TB_SANDSTONE, 1, 4, 2, var10);
        sc_place(c, p, TB_SANDSTONE, 2, X - 5, 1, var10);
        sc_place(c, p, TB_SANDSTONE, 1, X - 5, 2, var10);
    }

    sc_place(c, p, TB_WOOL, var8, 10, 0, 7);
    sc_place(c, p, TB_WOOL, var8, 10, 0, 8);
    sc_place(c, p, TB_WOOL, var8, 9, 0, 9);
    sc_place(c, p, TB_WOOL, var8, 11, 0, 9);
    sc_place(c, p, TB_WOOL, var8, 8, 0, 10);
    sc_place(c, p, TB_WOOL, var8, 12, 0, 10);
    sc_place(c, p, TB_WOOL, var8, 7, 0, 10);
    sc_place(c, p, TB_WOOL, var8, 13, 0, 10);
    sc_place(c, p, TB_WOOL, var8, 9, 0, 11);
    sc_place(c, p, TB_WOOL, var8, 11, 0, 11);
    sc_place(c, p, TB_WOOL, var8, 10, 0, 12);
    sc_place(c, p, TB_WOOL, var8, 10, 0, 13);
    sc_place(c, p, TB_WOOL, var9, 10, 0, 10);

    for (var10 = 0; var10 <= X - 1; var10 += X - 1)
    {
        sc_place(c, p, TB_SANDSTONE, 2, var10, 2, 1);
        sc_place(c, p, TB_WOOL, var8, var10, 2, 2);
        sc_place(c, p, TB_SANDSTONE, 2, var10, 2, 3);
        sc_place(c, p, TB_SANDSTONE, 2, var10, 3, 1);
        sc_place(c, p, TB_WOOL, var8, var10, 3, 2);
        sc_place(c, p, TB_SANDSTONE, 2, var10, 3, 3);
        sc_place(c, p, TB_WOOL, var8, var10, 4, 1);
        sc_place(c, p, TB_SANDSTONE, 1, var10, 4, 2);
        sc_place(c, p, TB_WOOL, var8, var10, 4, 3);
        sc_place(c, p, TB_SANDSTONE, 2, var10, 5, 1);
        sc_place(c, p, TB_WOOL, var8, var10, 5, 2);
        sc_place(c, p, TB_SANDSTONE, 2, var10, 5, 3);
        sc_place(c, p, TB_WOOL, var8, var10, 6, 1);
        sc_place(c, p, TB_SANDSTONE, 1, var10, 6, 2);
        sc_place(c, p, TB_WOOL, var8, var10, 6, 3);
        sc_place(c, p, TB_WOOL, var8, var10, 7, 1);
        sc_place(c, p, TB_WOOL, var8, var10, 7, 2);
        sc_place(c, p, TB_WOOL, var8, var10, 7, 3);
        sc_place(c, p, TB_SANDSTONE, 2, var10, 8, 1);
        sc_place(c, p, TB_SANDSTONE, 2, var10, 8, 2);
        sc_place(c, p, TB_SANDSTONE, 2, var10, 8, 3);
    }

    for (var10 = 2; var10 <= X - 3; var10 += X - 3 - 2)
    {
        sc_place(c, p, TB_SANDSTONE, 2, var10 - 1, 2, 0);
        sc_place(c, p, TB_WOOL, var8, var10, 2, 0);
        sc_place(c, p, TB_SANDSTONE, 2, var10 + 1, 2, 0);
        sc_place(c, p, TB_SANDSTONE, 2, var10 - 1, 3, 0);
        sc_place(c, p, TB_WOOL, var8, var10, 3, 0);
        sc_place(c, p, TB_SANDSTONE, 2, var10 + 1, 3, 0);
        sc_place(c, p, TB_WOOL, var8, var10 - 1, 4, 0);
        sc_place(c, p, TB_SANDSTONE, 1, var10, 4, 0);
        sc_place(c, p, TB_WOOL, var8, var10 + 1, 4, 0);
        sc_place(c, p, TB_SANDSTONE, 2, var10 - 1, 5, 0);
        sc_place(c, p, TB_WOOL, var8, var10, 5, 0);
        sc_place(c, p, TB_SANDSTONE, 2, var10 + 1, 5, 0);
        sc_place(c, p, TB_WOOL, var8, var10 - 1, 6, 0);
        sc_place(c, p, TB_SANDSTONE, 1, var10, 6, 0);
        sc_place(c, p, TB_WOOL, var8, var10 + 1, 6, 0);
        sc_place(c, p, TB_WOOL, var8, var10 - 1, 7, 0);
        sc_place(c, p, TB_WOOL, var8, var10, 7, 0);
        sc_place(c, p, TB_WOOL, var8, var10 + 1, 7, 0);
        sc_place(c, p, TB_SANDSTONE, 2, var10 - 1, 8, 0);
        sc_place(c, p, TB_SANDSTONE, 2, var10, 8, 0);
        sc_place(c, p, TB_SANDSTONE, 2, var10 + 1, 8, 0);
    }

    sc_fill_meta(c, p, 8, 4, 0, 12, 6, 0, TB_SANDSTONE, 2, TB_SANDSTONE, 2, 0);
    sc_place(c, p, TB_AIR, 0, 8, 6, 0);
    sc_place(c, p, TB_AIR, 0, 12, 6, 0);
    sc_place(c, p, TB_WOOL, var8, 9, 5, 0);
    sc_place(c, p, TB_SANDSTONE, 1, 10, 5, 0);
    sc_place(c, p, TB_WOOL, var8, 11, 5, 0);
    sc_fill_meta(c, p, 8, -14, 8, 12, -11, 12, TB_SANDSTONE, 2, TB_SANDSTONE, 2, 0);
    sc_fill_meta(c, p, 8, -10, 8, 12, -10, 12, TB_SANDSTONE, 1, TB_SANDSTONE, 1, 0);
    sc_fill_meta(c, p, 8, -9, 8, 12, -9, 12, TB_SANDSTONE, 2, TB_SANDSTONE, 2, 0);
    sc_fill(c, p, 8, -8, 8, 12, -1, 12, TB_SANDSTONE, TB_SANDSTONE, 0);
    sc_fill(c, p, 9, -11, 9, 11, -1, 11, TB_AIR, TB_AIR, 0);
    sc_place(c, p, TB_STONE_PRESSURE_PLATE, 0, 10, -11, 10);
    sc_fill(c, p, 9, -13, 9, 11, -13, 11, TB_TNT, TB_AIR, 0);
    sc_place(c, p, TB_AIR, 0, 8, -11, 10);
    sc_place(c, p, TB_AIR, 0, 8, -10, 10);
    sc_place(c, p, TB_SANDSTONE, 1, 7, -10, 10);
    sc_place(c, p, TB_SANDSTONE, 2, 7, -11, 10);
    sc_place(c, p, TB_AIR, 0, 12, -11, 10);
    sc_place(c, p, TB_AIR, 0, 12, -10, 10);
    sc_place(c, p, TB_SANDSTONE, 1, 13, -10, 10);
    sc_place(c, p, TB_SANDSTONE, 2, 13, -11, 10);
    sc_place(c, p, TB_AIR, 0, 10, -11, 8);
    sc_place(c, p, TB_AIR, 0, 10, -10, 8);
    sc_place(c, p, TB_SANDSTONE, 1, 10, -10, 7);
    sc_place(c, p, TB_SANDSTONE, 2, 10, -11, 7);
    sc_place(c, p, TB_AIR, 0, 10, -11, 12);
    sc_place(c, p, TB_AIR, 0, 10, -10, 12);
    sc_place(c, p, TB_SANDSTONE, 1, 10, -10, 13);
    sc_place(c, p, TB_SANDSTONE, 2, 10, -11, 13);

    for (var10 = 0; var10 < 4; ++var10)
    {
        if (p->u.temple.v.desert.has_placed_chest[var10]) continue;

        int var11 = OFF_X[var10] * 2;
        int var12 = OFF_Z[var10] * 2;

        temple_chest(c, p, "desertTemple", 10 + var11, -11, 10 + var12, &p->u.temple.v.desert.has_placed_chest[var10]);
    }

    return 1;
}

/* --------------------------------------------------------- jungle pyramid */

/* JunglePyramid.Stones.selectBlocks: 40% cobblestone, else mossy cobblestone,
 * one float draw per cell; the metadata stays the selector's 0. */
static void jungle_stones_select(void *ud, int on_face, int *id, int *meta)
{
    struct sc_ctx *c = ud;

    (void)on_face;

    *id = jr_float(c->rand) < 0.4f ? TB_COBBLESTONE : TB_MOSSY_COBBLESTONE;
    *meta = 0;
}

/* The jungle pyramid's two arrow traps: generateStructureDispenserContents over
 * junglePyramidsDispenserContents with a fixed count of 2 and no book. */
static int jungle_dispenser(struct sc_ctx *c, struct piece *p, int tx, int ty, int tz, int dir, int *placed)
{
    const struct loot_table *table = loot_table_by_name("jungleDispenser");

    *placed = sc_dispenser_contents(c, p, table, NULL, tx, ty, tz, dir, 2);
    return *placed;
}

static int jungle_pyramid_parts(struct sc_ctx *c, struct piece *p)
{
    if (!feature_ground_level(c, p, 0)) return 0;

    int var4 = sc_orient_meta(TB_STONE_STAIRS, 3, p->coord_base_mode);
    int var5 = sc_orient_meta(TB_STONE_STAIRS, 2, p->coord_base_mode);
    int var6 = sc_orient_meta(TB_STONE_STAIRS, 0, p->coord_base_mode);
    int var7 = sc_orient_meta(TB_STONE_STAIRS, 1, p->coord_base_mode);
    int X = p->u.temple.size_x, Z = p->u.temple.size_z;

    sc_fill_randomized(c, p, 0, -4, 0, X - 1, 0, Z - 1, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 2, 1, 2, 9, 2, 2, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 2, 1, 12, 9, 2, 12, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 2, 1, 3, 2, 2, 11, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 9, 1, 3, 9, 2, 11, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 1, 3, 1, 10, 6, 1, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 1, 3, 13, 10, 6, 13, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 1, 3, 2, 1, 6, 12, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 10, 3, 2, 10, 6, 12, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 2, 3, 2, 9, 3, 12, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 2, 6, 2, 9, 6, 12, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 3, 7, 3, 8, 7, 11, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 4, 8, 4, 7, 8, 10, 0, jungle_stones_select, c);
    sc_fill_air(c, p, 3, 1, 3, 8, 2, 11);
    sc_fill_air(c, p, 4, 3, 6, 7, 3, 9);
    sc_fill_air(c, p, 2, 4, 2, 9, 5, 12);
    sc_fill_air(c, p, 4, 6, 5, 7, 6, 9);
    sc_fill_air(c, p, 5, 7, 6, 6, 7, 8);
    sc_fill_air(c, p, 5, 1, 2, 6, 2, 2);
    sc_fill_air(c, p, 5, 2, 12, 6, 2, 12);
    sc_fill_air(c, p, 5, 5, 1, 6, 5, 1);
    sc_fill_air(c, p, 5, 5, 13, 6, 5, 13);
    sc_place(c, p, TB_AIR, 0, 1, 5, 5);
    sc_place(c, p, TB_AIR, 0, 10, 5, 5);
    sc_place(c, p, TB_AIR, 0, 1, 5, 9);
    sc_place(c, p, TB_AIR, 0, 10, 5, 9);

    int var8, var9;

    for (var8 = 0; var8 <= 14; var8 += 14)
    {
        sc_fill_randomized(c, p, 2, 4, var8, 2, 5, var8, 0, jungle_stones_select, c);
        sc_fill_randomized(c, p, 4, 4, var8, 4, 5, var8, 0, jungle_stones_select, c);
        sc_fill_randomized(c, p, 7, 4, var8, 7, 5, var8, 0, jungle_stones_select, c);
        sc_fill_randomized(c, p, 9, 4, var8, 9, 5, var8, 0, jungle_stones_select, c);
    }

    sc_fill_randomized(c, p, 5, 6, 0, 6, 6, 0, 0, jungle_stones_select, c);

    for (var8 = 0; var8 <= 11; var8 += 11)
    {
        for (var9 = 2; var9 <= 12; var9 += 2)
            sc_fill_randomized(c, p, var8, 4, var9, var8, 5, var9, 0, jungle_stones_select, c);

        sc_fill_randomized(c, p, var8, 6, 5, var8, 6, 5, 0, jungle_stones_select, c);
        sc_fill_randomized(c, p, var8, 6, 9, var8, 6, 9, 0, jungle_stones_select, c);
    }

    sc_fill_randomized(c, p, 2, 7, 2, 2, 9, 2, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 9, 7, 2, 9, 9, 2, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 2, 7, 12, 2, 9, 12, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 9, 7, 12, 9, 9, 12, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 4, 9, 4, 4, 9, 4, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 7, 9, 4, 7, 9, 4, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 4, 9, 10, 4, 9, 10, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 7, 9, 10, 7, 9, 10, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 5, 9, 7, 6, 9, 7, 0, jungle_stones_select, c);
    sc_place(c, p, TB_STONE_STAIRS, var4, 5, 9, 6);
    sc_place(c, p, TB_STONE_STAIRS, var4, 6, 9, 6);
    sc_place(c, p, TB_STONE_STAIRS, var5, 5, 9, 8);
    sc_place(c, p, TB_STONE_STAIRS, var5, 6, 9, 8);
    sc_place(c, p, TB_STONE_STAIRS, var4, 4, 0, 0);
    sc_place(c, p, TB_STONE_STAIRS, var4, 5, 0, 0);
    sc_place(c, p, TB_STONE_STAIRS, var4, 6, 0, 0);
    sc_place(c, p, TB_STONE_STAIRS, var4, 7, 0, 0);
    sc_place(c, p, TB_STONE_STAIRS, var4, 4, 1, 8);
    sc_place(c, p, TB_STONE_STAIRS, var4, 4, 2, 9);
    sc_place(c, p, TB_STONE_STAIRS, var4, 4, 3, 10);
    sc_place(c, p, TB_STONE_STAIRS, var4, 7, 1, 8);
    sc_place(c, p, TB_STONE_STAIRS, var4, 7, 2, 9);
    sc_place(c, p, TB_STONE_STAIRS, var4, 7, 3, 10);
    sc_fill_randomized(c, p, 4, 1, 9, 4, 1, 9, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 7, 1, 9, 7, 1, 9, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 4, 1, 10, 7, 2, 10, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 5, 4, 5, 6, 4, 5, 0, jungle_stones_select, c);
    sc_place(c, p, TB_STONE_STAIRS, var6, 4, 4, 5);
    sc_place(c, p, TB_STONE_STAIRS, var7, 7, 4, 5);

    for (var8 = 0; var8 < 4; ++var8)
    {
        sc_place(c, p, TB_STONE_STAIRS, var5, 5, 0 - var8, 6 + var8);
        sc_place(c, p, TB_STONE_STAIRS, var5, 6, 0 - var8, 6 + var8);
        sc_fill_air(c, p, 5, 0 - var8, 7 + var8, 6, 0 - var8, 9 + var8);
    }

    sc_fill_air(c, p, 1, -3, 12, 10, -1, 13);
    sc_fill_air(c, p, 1, -3, 1, 3, -1, 13);
    sc_fill_air(c, p, 1, -3, 1, 9, -1, 5);

    for (var8 = 1; var8 <= 13; var8 += 2)
        sc_fill_randomized(c, p, 1, -3, var8, 1, -2, var8, 0, jungle_stones_select, c);

    for (var8 = 2; var8 <= 12; var8 += 2)
        sc_fill_randomized(c, p, 1, -1, var8, 3, -1, var8, 0, jungle_stones_select, c);

    sc_fill_randomized(c, p, 2, -2, 1, 5, -2, 1, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 7, -2, 1, 9, -2, 1, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 6, -3, 1, 6, -3, 1, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 6, -1, 1, 6, -1, 1, 0, jungle_stones_select, c);
    sc_place(c, p, TB_TRIPWIRE_HOOK, sc_orient_meta(TB_TRIPWIRE_HOOK, 3, p->coord_base_mode) | 4, 1, -3, 8);
    sc_place(c, p, TB_TRIPWIRE_HOOK, sc_orient_meta(TB_TRIPWIRE_HOOK, 1, p->coord_base_mode) | 4, 4, -3, 8);
    sc_place(c, p, TB_TRIPWIRE, 4, 2, -3, 8);
    sc_place(c, p, TB_TRIPWIRE, 4, 3, -3, 8);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 5, -3, 7);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 5, -3, 6);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 5, -3, 5);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 5, -3, 4);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 5, -3, 3);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 5, -3, 2);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 5, -3, 1);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 4, -3, 1);
    sc_place(c, p, TB_MOSSY_COBBLESTONE, 0, 3, -3, 1);

    if (!p->u.temple.v.jungle.placed_trap1)
        jungle_dispenser(c, p, 3, -2, 1, 2, &p->u.temple.v.jungle.placed_trap1);

    sc_place(c, p, TB_VINE, 15, 3, -2, 2);
    sc_place(c, p, TB_TRIPWIRE_HOOK, sc_orient_meta(TB_TRIPWIRE_HOOK, 2, p->coord_base_mode) | 4, 7, -3, 1);
    sc_place(c, p, TB_TRIPWIRE_HOOK, sc_orient_meta(TB_TRIPWIRE_HOOK, 0, p->coord_base_mode) | 4, 7, -3, 5);
    sc_place(c, p, TB_TRIPWIRE, 4, 7, -3, 2);
    sc_place(c, p, TB_TRIPWIRE, 4, 7, -3, 3);
    sc_place(c, p, TB_TRIPWIRE, 4, 7, -3, 4);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 8, -3, 6);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 9, -3, 6);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 9, -3, 5);
    sc_place(c, p, TB_MOSSY_COBBLESTONE, 0, 9, -3, 4);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 9, -2, 4);

    if (!p->u.temple.v.jungle.placed_trap2)
        jungle_dispenser(c, p, 9, -2, 3, 4, &p->u.temple.v.jungle.placed_trap2);

    sc_place(c, p, TB_VINE, 15, 8, -1, 3);
    sc_place(c, p, TB_VINE, 15, 8, -2, 3);

    if (!p->u.temple.v.jungle.placed_main_chest)
        temple_chest(c, p, "jungleTemple", 8, -3, 3, &p->u.temple.v.jungle.placed_main_chest);

    sc_place(c, p, TB_MOSSY_COBBLESTONE, 0, 9, -3, 2);
    sc_place(c, p, TB_MOSSY_COBBLESTONE, 0, 8, -3, 1);
    sc_place(c, p, TB_MOSSY_COBBLESTONE, 0, 4, -3, 5);
    sc_place(c, p, TB_MOSSY_COBBLESTONE, 0, 5, -2, 5);
    sc_place(c, p, TB_MOSSY_COBBLESTONE, 0, 5, -1, 5);
    sc_place(c, p, TB_MOSSY_COBBLESTONE, 0, 6, -3, 5);
    sc_place(c, p, TB_MOSSY_COBBLESTONE, 0, 7, -2, 5);
    sc_place(c, p, TB_MOSSY_COBBLESTONE, 0, 7, -1, 5);
    sc_place(c, p, TB_MOSSY_COBBLESTONE, 0, 8, -3, 5);
    sc_fill_randomized(c, p, 9, -1, 1, 9, -1, 5, 0, jungle_stones_select, c);
    sc_fill_air(c, p, 8, -3, 8, 10, -1, 10);
    sc_place(c, p, TB_STONEBRICK, 3, 8, -2, 11);
    sc_place(c, p, TB_STONEBRICK, 3, 9, -2, 11);
    sc_place(c, p, TB_STONEBRICK, 3, 10, -2, 11);
    sc_place(c, p, TB_LEVER, lever_attach(sc_orient_meta(TB_LEVER, 2, p->coord_base_mode)), 8, -2, 12);
    sc_place(c, p, TB_LEVER, lever_attach(sc_orient_meta(TB_LEVER, 2, p->coord_base_mode)), 9, -2, 12);
    sc_place(c, p, TB_LEVER, lever_attach(sc_orient_meta(TB_LEVER, 2, p->coord_base_mode)), 10, -2, 12);
    sc_fill_randomized(c, p, 8, -3, 8, 8, -3, 10, 0, jungle_stones_select, c);
    sc_fill_randomized(c, p, 10, -3, 8, 10, -3, 10, 0, jungle_stones_select, c);
    sc_place(c, p, TB_MOSSY_COBBLESTONE, 0, 10, -2, 9);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 8, -2, 9);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 8, -2, 10);
    sc_place(c, p, TB_REDSTONE_WIRE, 0, 10, -1, 9);
    sc_place(c, p, TB_STICKY_PISTON, 1, 9, -2, 8);
    sc_place(c, p, TB_STICKY_PISTON, sc_orient_meta(TB_STICKY_PISTON, 4, p->coord_base_mode), 10, -2, 8);
    sc_place(c, p, TB_STICKY_PISTON, sc_orient_meta(TB_STICKY_PISTON, 4, p->coord_base_mode), 10, -1, 8);
    sc_place(c, p, TB_UNPOWERED_REPEATER, sc_orient_meta(TB_UNPOWERED_REPEATER, 2, p->coord_base_mode), 10, -2, 10);

    if (!p->u.temple.v.jungle.placed_hidden_chest)
        temple_chest(c, p, "jungleTemple", 9, -3, 10, &p->u.temple.v.jungle.placed_hidden_chest);

    return 1;
}

/* ------------------------------------------------------------- witch hut */

static int swamp_hut_parts(struct sc_ctx *c, struct piece *p)
{
    if (!feature_ground_level(c, p, 0)) return 0;

    sc_fill_meta(c, p, 1, 1, 1, 5, 1, 7, TB_PLANKS, 1, TB_PLANKS, 1, 0);
    sc_fill_meta(c, p, 1, 4, 2, 5, 4, 7, TB_PLANKS, 1, TB_PLANKS, 1, 0);
    sc_fill_meta(c, p, 2, 1, 0, 4, 1, 0, TB_PLANKS, 1, TB_PLANKS, 1, 0);
    sc_fill_meta(c, p, 2, 2, 2, 3, 3, 2, TB_PLANKS, 1, TB_PLANKS, 1, 0);
    sc_fill_meta(c, p, 1, 2, 3, 1, 3, 6, TB_PLANKS, 1, TB_PLANKS, 1, 0);
    sc_fill_meta(c, p, 5, 2, 3, 5, 3, 6, TB_PLANKS, 1, TB_PLANKS, 1, 0);
    sc_fill_meta(c, p, 2, 2, 7, 4, 3, 7, TB_PLANKS, 1, TB_PLANKS, 1, 0);
    sc_fill(c, p, 1, 0, 2, 1, 3, 2, TB_LOG, TB_LOG, 0);
    sc_fill(c, p, 5, 0, 2, 5, 3, 2, TB_LOG, TB_LOG, 0);
    sc_fill(c, p, 1, 0, 7, 1, 3, 7, TB_LOG, TB_LOG, 0);
    sc_fill(c, p, 5, 0, 7, 5, 3, 7, TB_LOG, TB_LOG, 0);
    sc_place(c, p, TB_FENCE, 0, 2, 3, 2);
    sc_place(c, p, TB_FENCE, 0, 3, 3, 7);
    sc_place(c, p, TB_AIR, 0, 1, 3, 4);
    sc_place(c, p, TB_AIR, 0, 5, 3, 4);
    sc_place(c, p, TB_AIR, 0, 5, 3, 5);
    sc_place(c, p, TB_FLOWER_POT, 7, 1, 3, 5);
    sc_place(c, p, TB_CRAFTING_TABLE, 0, 3, 2, 6);
    sc_place(c, p, TB_CAULDRON, 0, 4, 2, 6);
    sc_place(c, p, TB_FENCE, 0, 1, 2, 1);
    sc_place(c, p, TB_FENCE, 0, 5, 2, 1);

    int var4 = sc_orient_meta(TB_OAK_STAIRS, 3, p->coord_base_mode);
    int var5 = sc_orient_meta(TB_OAK_STAIRS, 1, p->coord_base_mode);
    int var6 = sc_orient_meta(TB_OAK_STAIRS, 0, p->coord_base_mode);
    int var7 = sc_orient_meta(TB_OAK_STAIRS, 2, p->coord_base_mode);

    sc_fill_meta(c, p, 0, 4, 1, 6, 4, 1, TB_SPRUCE_STAIRS, var4, TB_SPRUCE_STAIRS, var4, 0);
    sc_fill_meta(c, p, 0, 4, 2, 0, 4, 7, TB_SPRUCE_STAIRS, var6, TB_SPRUCE_STAIRS, var6, 0);
    sc_fill_meta(c, p, 6, 4, 2, 6, 4, 7, TB_SPRUCE_STAIRS, var5, TB_SPRUCE_STAIRS, var5, 0);
    sc_fill_meta(c, p, 0, 4, 8, 6, 4, 8, TB_SPRUCE_STAIRS, var7, TB_SPRUCE_STAIRS, var7, 0);

    for (int var8 = 2; var8 <= 7; var8 += 5)
        for (int var9 = 1; var9 <= 5; var9 += 4)
            sc_fill_down(c, p, TB_LOG, 0, var9, -1, var8);

    if (!p->u.temple.v.swamp.has_witch)
    {
        int var8 = sc_x_with_offset(p, 2, 5);
        int var9 = sc_y_with_offset(p, 2);
        int var10 = sc_z_with_offset(p, 2, 5);

        if (sc_in_step_box(c, var8, var9, var10))
        {
            p->u.temple.v.swamp.has_witch = 1;
            sc_witch_new(c, (double)var8 + 0.5, (double)var9, (double)var10 + 0.5);
        }
    }

    return 1;
}

int temple_blocks(struct sc_ctx *c, struct piece *p)
{
    switch (p->u.temple.kind)
    {
        case TEMPLE_DESERT_PYRAMID: return desert_pyramid_parts(c, p);
        case TEMPLE_JUNGLE_PYRAMID: return jungle_pyramid_parts(c, p);
        case TEMPLE_SWAMP_HUT: return swamp_hut_parts(c, p);
        default: return 1;
    }
}