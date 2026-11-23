/* StructureNetherBridgePieces' addComponentParts, every kind's block half: the
 * crossings, the straight, the five corridors, the stairs, the throne room
 * (its blaze spawner), the entrance (its lava bath, run under
 * World.scheduledUpdatesAreImmediate), the dead end (its own Random from the
 * fill seed) and the nether wart room. Every piece returns true: the fortress
 * pieces have no liquid guard and are never dropped. The loot table is the
 * Piece base's field_111019_a, loot.c's "netherBridge", with no book entry. */
#include "fortress.h"

#include "features_springs.h"
#include "ticks.h"

#include <stdlib.h>
#include <string.h>

/* the blocks the fortress pieces name, from Blocks */
enum {
    FB_AIR = 0, FB_FLOWING_LAVA = 10, FB_MOB_SPAWNER = 52, FB_CHEST = 54,
    FB_SOUL_SAND = 88, FB_NETHER_BRICK = 112, FB_NETHER_BRICK_FENCE = 113,
    FB_NETHER_BRICK_STAIRS = 114, FB_NETHER_WART = 115
};

static char *dup_text(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);

    if (p) memcpy(p, s, n);
    return p;
}

/* Crossing3.addComponentParts; Start extends Crossing3 and adds no writes. */
static int crossing3_parts(struct sc_ctx *c, struct piece *p)
{
    sc_fill(c, p, 7, 3, 0, 11, 4, 18, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 3, 7, 18, 4, 11, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 8, 5, 0, 10, 7, 18, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 0, 5, 8, 18, 7, 10, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 7, 5, 0, 7, 5, 7, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 7, 5, 11, 7, 5, 18, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 11, 5, 0, 11, 5, 7, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 11, 5, 11, 11, 5, 18, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 5, 7, 7, 5, 7, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 11, 5, 7, 18, 5, 7, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 5, 11, 7, 5, 11, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 11, 5, 11, 18, 5, 11, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 7, 2, 0, 11, 2, 5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 7, 2, 13, 11, 2, 18, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 7, 0, 0, 11, 1, 3, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 7, 0, 15, 11, 1, 18, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

    for (int var4 = 7; var4 <= 11; ++var4)
        for (int var5 = 0; var5 <= 2; ++var5)
        {
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, var5);
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, 18 - var5);
        }

    sc_fill(c, p, 0, 2, 7, 5, 2, 11, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 13, 2, 7, 18, 2, 11, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 0, 7, 3, 1, 11, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 15, 0, 7, 18, 1, 11, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

    for (int var4 = 0; var4 <= 2; ++var4)
        for (int var5 = 7; var5 <= 11; ++var5)
        {
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, var5);
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, 18 - var4, -1, var5);
        }

    return 1;
}

/* Straight.addComponentParts. */
static int straight_parts(struct sc_ctx *c, struct piece *p)
{
    sc_fill(c, p, 0, 3, 0, 4, 4, 18, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 5, 0, 3, 7, 18, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 0, 5, 0, 0, 5, 18, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 4, 5, 0, 4, 5, 18, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 0, 4, 2, 5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 13, 4, 2, 18, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 0, 0, 4, 1, 3, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 0, 15, 4, 1, 18, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

    for (int var4 = 0; var4 <= 4; ++var4)
        for (int var5 = 0; var5 <= 2; ++var5)
        {
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, var5);
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, 18 - var5);
        }

    sc_fill(c, p, 0, 1, 1, 0, 4, 1, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 0, 3, 4, 0, 4, 4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 0, 3, 14, 0, 4, 14, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 0, 1, 17, 0, 4, 17, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 4, 1, 1, 4, 4, 1, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 4, 3, 4, 4, 4, 4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 4, 3, 14, 4, 4, 14, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 4, 1, 17, 4, 4, 17, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    return 1;
}

/* A corridor's chest, Corridor (3,2,3) or Corridor2 (1,2,3). Java guards the
 * call with isVecInside before it, so the count draw `2 + nextInt(4)` happens
 * only when the chest position lies in the step chunk box; the argument's
 * evaluation is that draw. The flag clears even when the chest then fails the
 * air check and nothing is placed. */
static void corridor_chest(struct sc_ctx *c, struct piece *p, int tx, int ty, int tz)
{
    int x = sc_x_with_offset(p, tx, tz), y = sc_y_with_offset(p, ty), z = sc_z_with_offset(p, tx, tz);

    if (!sc_in_step_box(c, x, y, z)) return;

    p->u.fortress.chest = 0;

    int count = 2 + jr_int_n(c->rand, 4);

    sc_chest_contents(c, p, loot_table_by_name("netherBridge"), NULL, tx, ty, tz, count);
}

/* Corridor.addComponentParts. */
static int corridor_parts(struct sc_ctx *c, struct piece *p)
{
    sc_fill(c, p, 0, 0, 0, 4, 1, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 0, 4, 5, 4, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 4, 2, 0, 4, 5, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 4, 3, 1, 4, 4, 1, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 4, 3, 3, 4, 4, 3, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 0, 2, 0, 0, 5, 0, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 4, 3, 5, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 3, 4, 1, 4, 4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 3, 3, 4, 3, 4, 4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK, 0);

    if (p->u.fortress.chest) corridor_chest(c, p, 3, 2, 3);

    sc_fill(c, p, 0, 6, 0, 4, 6, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

    for (int var4 = 0; var4 <= 4; ++var4)
        for (int var5 = 0; var5 <= 4; ++var5)
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, var5);

    return 1;
}

/* Corridor2.addComponentParts. */
static int corridor2_parts(struct sc_ctx *c, struct piece *p)
{
    sc_fill(c, p, 0, 0, 0, 4, 1, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 0, 4, 5, 4, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 0, 2, 0, 0, 5, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 3, 1, 0, 4, 1, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 0, 3, 3, 0, 4, 3, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 4, 2, 0, 4, 5, 0, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 2, 4, 4, 5, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 3, 4, 1, 4, 4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 3, 3, 4, 3, 4, 4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK, 0);

    if (p->u.fortress.chest) corridor_chest(c, p, 1, 2, 3);

    sc_fill(c, p, 0, 6, 0, 4, 6, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

    for (int var4 = 0; var4 <= 4; ++var4)
        for (int var5 = 0; var5 <= 4; ++var5)
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, var5);

    return 1;
}

/* Corridor3.addComponentParts, the staircase. */
static int corridor3_parts(struct sc_ctx *c, struct piece *p)
{
    int var4 = sc_orient_meta(FB_NETHER_BRICK_STAIRS, 2, p->coord_base_mode);

    for (int var5 = 0; var5 <= 9; ++var5)
    {
        int var6 = var5 < 6 ? 7 - var5 : 1;
        int var7 = var6 + 5 > 14 - var5 ? var6 + 5 : 14 - var5;

        if (var7 > 13) var7 = 13;
        sc_fill(c, p, 0, 0, var5, 4, var6, var5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
        sc_fill(c, p, 1, var6 + 1, var5, 3, var7 - 1, var5, FB_AIR, FB_AIR, 0);

        if (var5 <= 6)
        {
            sc_place(c, p, FB_NETHER_BRICK_STAIRS, var4, 1, var6 + 1, var5);
            sc_place(c, p, FB_NETHER_BRICK_STAIRS, var4, 2, var6 + 1, var5);
            sc_place(c, p, FB_NETHER_BRICK_STAIRS, var4, 3, var6 + 1, var5);
        }

        sc_fill(c, p, 0, var7, var5, 4, var7, var5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
        sc_fill(c, p, 0, var6 + 1, var5, 0, var7 - 1, var5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
        sc_fill(c, p, 4, var6 + 1, var5, 4, var7 - 1, var5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

        if ((var5 & 1) == 0)
        {
            sc_fill(c, p, 0, var6 + 2, var5, 0, var6 + 3, var5, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
            sc_fill(c, p, 4, var6 + 2, var5, 4, var6 + 3, var5, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
        }

        for (int var9 = 0; var9 <= 4; ++var9)
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var9, -1, var5);
    }

    return 1;
}

/* Corridor4.addComponentParts. */
static int corridor4_parts(struct sc_ctx *c, struct piece *p)
{
    sc_fill(c, p, 0, 0, 0, 8, 1, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 0, 8, 5, 8, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 0, 6, 0, 8, 6, 5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 0, 2, 5, 0, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 6, 2, 0, 8, 5, 0, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 3, 0, 1, 4, 0, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 7, 3, 0, 7, 4, 0, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 0, 2, 4, 8, 2, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 1, 4, 2, 2, 4, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 6, 1, 4, 7, 2, 4, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 0, 3, 8, 8, 3, 8, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 0, 3, 6, 0, 3, 7, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 8, 3, 6, 8, 3, 7, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 0, 3, 4, 0, 5, 5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 8, 3, 4, 8, 5, 5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 3, 5, 2, 5, 5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 6, 3, 5, 7, 5, 5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 4, 5, 1, 5, 5, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 7, 4, 5, 7, 5, 5, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);

    for (int var4 = 0; var4 <= 5; ++var4)
        for (int var5 = 0; var5 <= 8; ++var5)
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var5, -1, var4);

    return 1;
}

/* Corridor5.addComponentParts. */
static int corridor5_parts(struct sc_ctx *c, struct piece *p)
{
    sc_fill(c, p, 0, 0, 0, 4, 1, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 0, 4, 5, 4, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 0, 2, 0, 0, 5, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 4, 2, 0, 4, 5, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 3, 1, 0, 4, 1, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 0, 3, 3, 0, 4, 3, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 4, 3, 1, 4, 4, 1, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 4, 3, 3, 4, 4, 3, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 0, 6, 0, 4, 6, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

    for (int var4 = 0; var4 <= 4; ++var4)
        for (int var5 = 0; var5 <= 4; ++var5)
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, var5);

    return 1;
}

/* Crossing.addComponentParts. */
static int crossing_parts(struct sc_ctx *c, struct piece *p)
{
    sc_fill(c, p, 0, 0, 0, 6, 1, 6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 0, 6, 7, 6, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 0, 2, 0, 1, 6, 0, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 6, 1, 6, 6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 5, 2, 0, 6, 6, 0, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 5, 2, 6, 6, 6, 6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 0, 0, 6, 1, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 5, 0, 6, 6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 6, 2, 0, 6, 6, 1, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 6, 2, 5, 6, 6, 6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 6, 0, 4, 6, 0, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 5, 0, 4, 5, 0, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 2, 6, 6, 4, 6, 6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 5, 6, 4, 5, 6, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 0, 6, 2, 0, 6, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 5, 2, 0, 5, 4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 6, 6, 2, 6, 6, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 6, 5, 2, 6, 5, 4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);

    for (int var4 = 0; var4 <= 6; ++var4)
        for (int var5 = 0; var5 <= 6; ++var5)
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, var5);

    return 1;
}

/* Crossing2.addComponentParts. */
static int crossing2_parts(struct sc_ctx *c, struct piece *p)
{
    sc_fill(c, p, 0, 0, 0, 4, 1, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 0, 4, 5, 4, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 0, 2, 0, 0, 5, 0, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 4, 2, 0, 4, 5, 0, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 4, 0, 5, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 4, 2, 4, 4, 5, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 6, 0, 4, 6, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

    for (int var4 = 0; var4 <= 4; ++var4)
        for (int var5 = 0; var5 <= 4; ++var5)
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, var5);

    return 1;
}

/* Stairs.addComponentParts. */
static int stairs_parts(struct sc_ctx *c, struct piece *p)
{
    sc_fill(c, p, 0, 0, 0, 6, 1, 6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 0, 6, 10, 6, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 0, 2, 0, 1, 8, 0, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 5, 2, 0, 6, 8, 0, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 1, 0, 8, 6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 6, 2, 1, 6, 8, 6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 2, 6, 5, 8, 6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 3, 2, 0, 5, 4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 6, 3, 2, 6, 5, 2, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 6, 3, 4, 6, 5, 4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_place(c, p, FB_NETHER_BRICK, 0, 5, 2, 5);
    sc_fill(c, p, 4, 2, 5, 4, 3, 5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 3, 2, 5, 3, 4, 5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 2, 5, 2, 5, 5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 2, 5, 1, 6, 5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 7, 1, 5, 7, 4, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 6, 8, 2, 6, 8, 4, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 2, 6, 0, 4, 8, 0, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 5, 0, 4, 5, 0, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);

    for (int var4 = 0; var4 <= 6; ++var4)
        for (int var5 = 0; var5 <= 6; ++var5)
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, var5);

    return 1;
}

/* Throne.addComponentParts: the blaze spawner is placed once per piece, the
 * hasSpawner flag carrying across the steps. */
static int throne_parts(struct sc_ctx *c, struct piece *p)
{
    sc_fill(c, p, 0, 2, 0, 6, 7, 7, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 1, 0, 0, 5, 1, 7, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 2, 1, 5, 2, 7, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 3, 2, 5, 3, 7, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 4, 3, 5, 4, 7, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 2, 0, 1, 4, 2, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 5, 2, 0, 5, 4, 2, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 5, 2, 1, 5, 3, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 5, 5, 2, 5, 5, 3, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 5, 3, 0, 5, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 6, 5, 3, 6, 5, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 1, 5, 8, 5, 5, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 1, 6, 3);
    sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 5, 6, 3);
    sc_fill(c, p, 0, 6, 3, 0, 6, 8, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 6, 6, 3, 6, 6, 8, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 1, 6, 8, 5, 7, 8, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 2, 8, 8, 4, 8, 8, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);

    if (!p->u.fortress.has_spawner)
    {
        int var4 = sc_y_with_offset(p, 5);
        int var5 = sc_x_with_offset(p, 3, 5);
        int var6 = sc_z_with_offset(p, 3, 5);

        if (sc_in_step_box(c, var5, var4, var6))
        {
            p->u.fortress.has_spawner = 1;
            world_set_block(c->w, var5, var4, var6, FB_MOB_SPAWNER, 0, 2);
            struct tile_entity *te = world_tile_entity(c->w, var5, var4, var6);

            if (te != NULL)
            {
                free(te->u.spawner.mob_id);
                te->u.spawner.mob_id = dup_text("Blaze");
            }
        }
    }

    for (int var4 = 0; var4 <= 6; ++var4)
        for (int var5 = 0; var5 <= 6; ++var5)
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, var5);

    return 1;
}

/* End.addComponentParts: the fills draw from the dead end's own Random,
 * seeded from the fill seed its constructor drew. */
static int end_parts(struct sc_ctx *c, struct piece *p)
{
    jrand rand;

    jr_seed(&rand, (int64_t)p->u.fortress.fill_seed);

    for (int var5 = 0; var5 <= 4; ++var5)
        for (int var6 = 3; var6 <= 4; ++var6)
        {
            int var7 = jr_int_n(&rand, 8);

            sc_fill(c, p, var5, var6, 0, var5, var6, var7, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
        }

    int var5 = jr_int_n(&rand, 8);

    sc_fill(c, p, 0, 5, 0, 0, 5, var5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    var5 = jr_int_n(&rand, 8);
    sc_fill(c, p, 4, 5, 0, 4, 5, var5, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

    for (int i = 0; i <= 4; ++i)
    {
        int var6 = jr_int_n(&rand, 5);

        sc_fill(c, p, i, 2, 0, i, 2, var6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    }

    for (int i = 0; i <= 4; ++i)
        for (int var6 = 0; var6 <= 1; ++var6)
        {
            int var7 = jr_int_n(&rand, 3);

            sc_fill(c, p, i, var6, 0, i, var6, var7, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
        }

    return 1;
}

/* Entrance.addComponentParts: the ended with the lava bath, whose updateTick
 * the piece runs at once under World.scheduledUpdatesAreImmediate, with the
 * population Random (the flow's spread draws). */
static int entrance_parts(struct sc_ctx *c, struct piece *p)
{
    sc_fill(c, p, 0, 3, 0, 12, 4, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 5, 0, 12, 13, 12, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 0, 5, 0, 1, 12, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 11, 5, 0, 12, 12, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 5, 11, 4, 12, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 8, 5, 11, 10, 12, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 5, 9, 11, 7, 12, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 5, 0, 4, 12, 1, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 8, 5, 0, 10, 12, 1, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 5, 9, 0, 7, 12, 1, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 11, 2, 10, 12, 10, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 5, 8, 0, 7, 8, 0, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);

    for (int var4 = 1; var4 <= 11; var4 += 2)
    {
        sc_fill(c, p, var4, 10, 0, var4, 11, 0, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
        sc_fill(c, p, var4, 10, 12, var4, 11, 12, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
        sc_fill(c, p, 0, 10, var4, 0, 11, var4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
        sc_fill(c, p, 12, 10, var4, 12, 11, var4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
        sc_place(c, p, FB_NETHER_BRICK, 0, var4, 13, 0);
        sc_place(c, p, FB_NETHER_BRICK, 0, var4, 13, 12);
        sc_place(c, p, FB_NETHER_BRICK, 0, 0, 13, var4);
        sc_place(c, p, FB_NETHER_BRICK, 0, 12, 13, var4);
        sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, var4 + 1, 13, 0);
        sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, var4 + 1, 13, 12);
        sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 0, 13, var4 + 1);
        sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 12, 13, var4 + 1);
    }

    sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 0, 13, 0);
    sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 0, 13, 12);
    sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 0, 13, 0);
    sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 12, 13, 0);

    for (int var4 = 3; var4 <= 9; var4 += 2)
    {
        sc_fill(c, p, 1, 7, var4, 1, 8, var4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
        sc_fill(c, p, 11, 7, var4, 11, 8, var4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    }

    sc_fill(c, p, 4, 2, 0, 8, 2, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 4, 12, 2, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 4, 0, 0, 8, 1, 3, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 4, 0, 9, 8, 1, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 0, 4, 3, 1, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 9, 0, 4, 12, 1, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

    for (int var4 = 4; var4 <= 8; ++var4)
        for (int var5 = 0; var5 <= 2; ++var5)
        {
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, var5);
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, 12 - var5);
        }

    for (int var4 = 0; var4 <= 2; ++var4)
        for (int var5 = 4; var5 <= 8; ++var5)
        {
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var4, -1, var5);
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, 12 - var4, -1, var5);
        }

    sc_fill(c, p, 5, 5, 5, 7, 5, 7, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 6, 1, 6, 6, 4, 6, FB_AIR, FB_AIR, 0);
    sc_place(c, p, FB_NETHER_BRICK, 0, 6, 0, 6);
    sc_place(c, p, FB_FLOWING_LAVA, 0, 6, 5, 6);

    int var4 = sc_x_with_offset(p, 6, 6);
    int var5 = sc_y_with_offset(p, 5);
    int var6 = sc_z_with_offset(p, 6, 6);

    if (sc_in_step_box(c, var4, var5, var6))
    {
        ticks_set_immediate(1);
        liquid_update_tick(c->w, var4, var5, var6, FB_FLOWING_LAVA, c->rand);
        ticks_set_immediate(0);
    }

    return 1;
}

/* NetherStalkRoom.addComponentParts: the staircase down to the wart farm. */
static int nether_stalk_parts(struct sc_ctx *c, struct piece *p)
{
    sc_fill(c, p, 0, 3, 0, 12, 4, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 5, 0, 12, 13, 12, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 0, 5, 0, 1, 12, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 11, 5, 0, 12, 12, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 5, 11, 4, 12, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 8, 5, 11, 10, 12, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 5, 9, 11, 7, 12, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 5, 0, 4, 12, 1, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 8, 5, 0, 10, 12, 1, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 5, 9, 0, 7, 12, 1, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 11, 2, 10, 12, 10, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

    for (int var4 = 1; var4 <= 11; var4 += 2)
    {
        sc_fill(c, p, var4, 10, 0, var4, 11, 0, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
        sc_fill(c, p, var4, 10, 12, var4, 11, 12, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
        sc_fill(c, p, 0, 10, var4, 0, 11, var4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
        sc_fill(c, p, 12, 10, var4, 12, 11, var4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
        sc_place(c, p, FB_NETHER_BRICK, 0, var4, 13, 0);
        sc_place(c, p, FB_NETHER_BRICK, 0, var4, 13, 12);
        sc_place(c, p, FB_NETHER_BRICK, 0, 0, 13, var4);
        sc_place(c, p, FB_NETHER_BRICK, 0, 12, 13, var4);
        sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, var4 + 1, 13, 0);
        sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, var4 + 1, 13, 12);
        sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 0, 13, var4 + 1);
        sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 12, 13, var4 + 1);
    }

    sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 0, 13, 0);
    sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 0, 13, 12);
    sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 0, 13, 0);
    sc_place(c, p, FB_NETHER_BRICK_FENCE, 0, 12, 13, 0);

    for (int var4 = 3; var4 <= 9; var4 += 2)
    {
        sc_fill(c, p, 1, 7, var4, 1, 8, var4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
        sc_fill(c, p, 11, 7, var4, 11, 8, var4, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    }

    int var4 = sc_orient_meta(FB_NETHER_BRICK_STAIRS, 3, p->coord_base_mode);

    for (int var5 = 0; var5 <= 6; ++var5)
    {
        int var6 = var5 + 4;

        for (int var7 = 5; var7 <= 7; ++var7)
            sc_place(c, p, FB_NETHER_BRICK_STAIRS, var4, var7, 5 + var5, var6);

        if (var6 >= 5 && var6 <= 8)
            sc_fill(c, p, 5, 5, var6, 7, var5 + 4, var6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
        else if (var6 >= 9 && var6 <= 10)
            sc_fill(c, p, 5, 8, var6, 7, var5 + 4, var6, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

        if (var5 >= 1)
            sc_fill(c, p, 5, 6 + var5, var6, 7, 9 + var5, var6, FB_AIR, FB_AIR, 0);
    }

    for (int var5 = 5; var5 <= 7; ++var5)
        sc_place(c, p, FB_NETHER_BRICK_STAIRS, var4, var5, 12, 11);

    sc_fill(c, p, 5, 6, 7, 5, 7, 7, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 7, 6, 7, 7, 7, 7, FB_NETHER_BRICK_FENCE, FB_NETHER_BRICK_FENCE, 0);
    sc_fill(c, p, 5, 13, 12, 7, 13, 12, FB_AIR, FB_AIR, 0);
    sc_fill(c, p, 2, 5, 2, 3, 5, 3, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 5, 9, 3, 5, 10, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 2, 5, 4, 2, 5, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 9, 5, 2, 10, 5, 3, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 9, 5, 9, 10, 5, 10, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 10, 5, 4, 10, 5, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

    int var5 = sc_orient_meta(FB_NETHER_BRICK_STAIRS, 0, p->coord_base_mode);
    int var6 = sc_orient_meta(FB_NETHER_BRICK_STAIRS, 1, p->coord_base_mode);

    sc_place(c, p, FB_NETHER_BRICK_STAIRS, var6, 4, 5, 2);
    sc_place(c, p, FB_NETHER_BRICK_STAIRS, var6, 4, 5, 3);
    sc_place(c, p, FB_NETHER_BRICK_STAIRS, var6, 4, 5, 9);
    sc_place(c, p, FB_NETHER_BRICK_STAIRS, var6, 4, 5, 10);
    sc_place(c, p, FB_NETHER_BRICK_STAIRS, var5, 8, 5, 2);
    sc_place(c, p, FB_NETHER_BRICK_STAIRS, var5, 8, 5, 3);
    sc_place(c, p, FB_NETHER_BRICK_STAIRS, var5, 8, 5, 9);
    sc_place(c, p, FB_NETHER_BRICK_STAIRS, var5, 8, 5, 10);
    sc_fill(c, p, 3, 4, 4, 4, 4, 8, FB_SOUL_SAND, FB_SOUL_SAND, 0);
    sc_fill(c, p, 8, 4, 4, 9, 4, 8, FB_SOUL_SAND, FB_SOUL_SAND, 0);
    sc_fill(c, p, 3, 5, 4, 4, 5, 8, FB_NETHER_WART, FB_NETHER_WART, 0);
    sc_fill(c, p, 8, 5, 4, 9, 5, 8, FB_NETHER_WART, FB_NETHER_WART, 0);
    sc_fill(c, p, 4, 2, 0, 8, 2, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 2, 4, 12, 2, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 4, 0, 0, 8, 1, 3, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 4, 0, 9, 8, 1, 12, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 0, 0, 4, 3, 1, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);
    sc_fill(c, p, 9, 0, 4, 12, 1, 8, FB_NETHER_BRICK, FB_NETHER_BRICK, 0);

    for (int var7 = 4; var7 <= 8; ++var7)
        for (int var8 = 0; var8 <= 2; ++var8)
        {
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var7, -1, var8);
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var7, -1, 12 - var8);
        }

    for (int var7 = 0; var7 <= 2; ++var7)
        for (int var8 = 4; var8 <= 8; ++var8)
        {
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, var7, -1, var8);
            sc_fill_down(c, p, FB_NETHER_BRICK, 0, 12 - var7, -1, var8);
        }

    return 1;
}

int fortress_blocks(struct sc_ctx *c, struct piece *p)
{
    switch (p->u.fortress.kind)
    {
    case F_START:
    case F_CROSSING3: return crossing3_parts(c, p);
    case F_STRAIGHT: return straight_parts(c, p);
    case F_CORRIDOR: return corridor_parts(c, p);
    case F_CORRIDOR2: return corridor2_parts(c, p);
    case F_CORRIDOR3: return corridor3_parts(c, p);
    case F_CORRIDOR4: return corridor4_parts(c, p);
    case F_CORRIDOR5: return corridor5_parts(c, p);
    case F_CROSSING: return crossing_parts(c, p);
    case F_CROSSING2: return crossing2_parts(c, p);
    case F_END: return end_parts(c, p);
    case F_ENTRANCE: return entrance_parts(c, p);
    case F_NETHER_STALK: return nether_stalk_parts(c, p);
    case F_STAIRS: return stairs_parts(c, p);
    case F_THRONE: return throne_parts(c, p);
    default: return 1;
    }
}
