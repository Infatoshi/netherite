/* StructureStrongholdPieces' addComponentParts, the twelve kinds' block halves:
 * the corridors and turns, the crossings and rooms, the library (chests,
 * bookshelves, the ring), the portal room (end portal frames, lava, the
 * silverfish spawner), the stairs and the prison. Every Random draw is in
 * Java's order: the Stones selector draws nextFloat per face cell of a
 * randomized fill, a chest's book stack and count draw before the call's body
 * runs, and each end portal frame draws once wherever it lands; see
 * structure_blocks.c for the helpers and the env the writes run in. */
#include "stronghold.h"

#include "loot.h"
#include "structure_blocks.h"
#include "tileentity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The blocks this structure places, from Blocks. */
enum {
    SHB_AIR = 0, SHB_COBBLESTONE = 4, SHB_PLANKS = 5, SHB_FLOWING_WATER = 8,
    SHB_FLOWING_LAVA = 10, SHB_TORCH = 50, SHB_MOB_SPAWNER = 52, SHB_CHEST = 54,
    SHB_WOODEN_DOOR = 64, SHB_LADDER = 65, SHB_STONE_STAIRS = 67,
    SHB_DOUBLE_STONE_SLAB = 43, SHB_STONE_SLAB = 44, SHB_BOOKSHELF = 47,
    SHB_FENCE = 85, SHB_IRON_BARS = 101, SHB_MONSTER_EGG = 97, SHB_STONEBRICK = 98,
    SHB_STONE_BRICK_STAIRS = 109, SHB_END_PORTAL_FRAME = 120, SHB_IRON_DOOR = 71,
    SHB_STONE_BUTTON = 77, SHB_WEB = 30
};

static char *dup_text(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);

    if (d) memcpy(d, s, n);
    return d;
}

/* ------------------------------------------------------------- the doors ---
 *
 * Stronghold.placeDoor over the Stronghold.Door the piece carries. */

static void door_opening(struct sc_ctx *c, const struct piece *p, int x, int y, int z)
{
    sc_fill_air(c, p, x, y, z, x + 2, y + 2, z);
}

static void door_wood(struct sc_ctx *c, const struct piece *p, int x, int y, int z)
{
    sc_place(c, p, SHB_STONEBRICK, 0, x, y, z);
    sc_place(c, p, SHB_STONEBRICK, 0, x, y + 1, z);
    sc_place(c, p, SHB_STONEBRICK, 0, x, y + 2, z);
    sc_place(c, p, SHB_STONEBRICK, 0, x + 1, y + 2, z);
    sc_place(c, p, SHB_STONEBRICK, 0, x + 2, y + 2, z);
    sc_place(c, p, SHB_STONEBRICK, 0, x + 2, y + 1, z);
    sc_place(c, p, SHB_STONEBRICK, 0, x + 2, y, z);
    sc_place(c, p, SHB_WOODEN_DOOR, 0, x + 1, y, z);
    sc_place(c, p, SHB_WOODEN_DOOR, 8, x + 1, y + 1, z);
}

static void door_grates(struct sc_ctx *c, const struct piece *p, int x, int y, int z)
{
    sc_place(c, p, SHB_AIR, 0, x + 1, y, z);
    sc_place(c, p, SHB_AIR, 0, x + 1, y + 1, z);
    sc_place(c, p, SHB_IRON_BARS, 0, x, y, z);
    sc_place(c, p, SHB_IRON_BARS, 0, x, y + 1, z);
    sc_place(c, p, SHB_IRON_BARS, 0, x, y + 2, z);
    sc_place(c, p, SHB_IRON_BARS, 0, x + 1, y + 2, z);
    sc_place(c, p, SHB_IRON_BARS, 0, x + 2, y + 2, z);
    sc_place(c, p, SHB_IRON_BARS, 0, x + 2, y + 1, z);
    sc_place(c, p, SHB_IRON_BARS, 0, x + 2, y, z);
}

static void door_iron(struct sc_ctx *c, const struct piece *p, int x, int y, int z)
{
    sc_place(c, p, SHB_STONEBRICK, 0, x, y, z);
    sc_place(c, p, SHB_STONEBRICK, 0, x, y + 1, z);
    sc_place(c, p, SHB_STONEBRICK, 0, x, y + 2, z);
    sc_place(c, p, SHB_STONEBRICK, 0, x + 1, y + 2, z);
    sc_place(c, p, SHB_STONEBRICK, 0, x + 2, y + 2, z);
    sc_place(c, p, SHB_STONEBRICK, 0, x + 2, y + 1, z);
    sc_place(c, p, SHB_STONEBRICK, 0, x + 2, y, z);
    sc_place(c, p, SHB_IRON_DOOR, 0, x + 1, y, z);
    sc_place(c, p, SHB_IRON_DOOR, 8, x + 1, y + 1, z);
    sc_place(c, p, SHB_STONE_BUTTON, sc_orient_meta(SHB_STONE_BUTTON, 4, p->coord_base_mode),
             x + 2, y + 1, z + 1);
    sc_place(c, p, SHB_STONE_BUTTON, sc_orient_meta(SHB_STONE_BUTTON, 3, p->coord_base_mode),
             x + 2, y + 1, z - 1);
}

static void place_door(struct sc_ctx *c, const struct piece *p, int door, int x, int y, int z)
{
    switch (door)
    {
    case DOOR_WOOD: door_wood(c, p, x, y, z); break;
    case DOOR_GRATES: door_grates(c, p, x, y, z); break;
    case DOOR_IRON: door_iron(c, p, x, y, z); break;
    default: door_opening(c, p, x, y, z); break;
    }
}

/* ------------------------------------------------------------------ stones
 * StructureStrongholdPieces.Stones: stone brick variants on a face cell, air
 * inside; the float draw happens only for a face cell. */

static void stones_select(void *ud, int on_face, int *id, int *meta)
{
    struct sc_ctx *c = ud;

    if (on_face)
    {
        *id = SHB_STONEBRICK;
        float v = jr_float(c->rand);

        if (v < 0.2f) *meta = 2;
        else if (v < 0.5f) *meta = 1;
        else if (v < 0.55f)
        {
            *id = SHB_MONSTER_EGG;
            *meta = 2;
        }
        else *meta = 0;
    }
    else
    {
        *id = SHB_AIR;
        *meta = 0;
    }
}

static void fill_randomized(struct sc_ctx *c, const struct piece *p, int minX, int minY, int minZ,
                            int maxX, int maxY, int maxZ, int replace)
{
    sc_fill_randomized(c, p, minX, minY, minZ, maxX, maxY, maxZ, replace, stones_select, c);
}

/* ------------------------------------------------------------------ chests
 * generateStructureChestContents' argument draws: the table's book stack from
 * the piece's own Random, then the count, then the call's body. */

static void chest(struct sc_ctx *c, const struct piece *p, const char *table_name,
                  enum loot_count_kind count_kind, int tx, int ty, int tz)
{
    const struct loot_table *table = loot_table_by_name(table_name);
    struct loot_book book;

    loot_book_build(c->rand, &table->entries[table->n - 1], &book);
    int count = loot_count(c->rand, count_kind);
    sc_chest_contents(c, p, table, &book, tx, ty, tz, count);
    loot_book_free(&book);
}

/* ------------------------------------------------------------------ pieces */

/* Straight.addComponentParts. */
static int straight_parts(struct sc_ctx *c, struct piece *p, const struct sh_state *st)
{
    if (sc_is_liquid_in(c, p)) return 0;

    fill_randomized(c, p, 0, 0, 0, 4, 4, 6, 1);
    place_door(c, p, st->door, 1, 1, 0);
    place_door(c, p, DOOR_OPENING, 1, 1, 6);
    sc_random_place(c, p, 0.1f, 1, 2, 1, SHB_TORCH, 0);
    sc_random_place(c, p, 0.1f, 3, 2, 1, SHB_TORCH, 0);
    sc_random_place(c, p, 0.1f, 1, 2, 5, SHB_TORCH, 0);
    sc_random_place(c, p, 0.1f, 3, 2, 5, SHB_TORCH, 0);

    if (st->left) sc_fill_air(c, p, 0, 1, 2, 0, 3, 4);
    if (st->right) sc_fill_air(c, p, 4, 1, 2, 4, 3, 4);

    return 1;
}

/* ChestCorridor.addComponentParts. */
static int chestcorridor_parts(struct sc_ctx *c, struct piece *p, struct sh_state *st)
{
    if (sc_is_liquid_in(c, p)) return 0;

    fill_randomized(c, p, 0, 0, 0, 4, 4, 6, 1);
    place_door(c, p, st->door, 1, 1, 0);
    place_door(c, p, DOOR_OPENING, 1, 1, 6);
    sc_fill(c, p, 3, 1, 2, 3, 1, 4, SHB_STONEBRICK, SHB_STONEBRICK, 0);
    sc_place(c, p, SHB_STONE_SLAB, 5, 3, 1, 1);
    sc_place(c, p, SHB_STONE_SLAB, 5, 3, 1, 5);
    sc_place(c, p, SHB_STONE_SLAB, 5, 3, 2, 2);
    sc_place(c, p, SHB_STONE_SLAB, 5, 3, 2, 4);

    for (int v = 2; v <= 4; ++v) sc_place(c, p, SHB_STONE_SLAB, 5, 2, 1, v);

    if (!st->chest)
    {
        int y = sc_y_with_offset(p, 2);
        int x = sc_x_with_offset(p, 3, 3);
        int z = sc_z_with_offset(p, 3, 3);

        if (sc_in_step_box(c, x, y, z))
        {
            st->chest = 1;
            chest(c, p, "strongholdCorridor", LOOT_COUNT_2_2, 3, 2, 3);
        }
    }

    return 1;
}

/* Corridor.addComponentParts. */
static int corridor_parts(struct sc_ctx *c, struct piece *p, const struct sh_state *st)
{
    if (sc_is_liquid_in(c, p)) return 0;

    for (int v = 0; v < st->steps; ++v)
    {
        for (int x = 0; x <= 4; ++x) sc_place(c, p, SHB_STONEBRICK, 0, x, 0, v);

        for (int y = 1; y <= 3; ++y)
        {
            sc_place(c, p, SHB_STONEBRICK, 0, 0, y, v);
            sc_place(c, p, SHB_AIR, 0, 1, y, v);
            sc_place(c, p, SHB_AIR, 0, 2, y, v);
            sc_place(c, p, SHB_AIR, 0, 3, y, v);
            sc_place(c, p, SHB_STONEBRICK, 0, 4, y, v);
        }

        for (int x = 0; x <= 4; ++x) sc_place(c, p, SHB_STONEBRICK, 0, x, 4, v);
    }

    return 1;
}

/* Crossing.addComponentParts. */
static int crossing_parts(struct sc_ctx *c, struct piece *p, const struct sh_state *st)
{
    if (sc_is_liquid_in(c, p)) return 0;

    fill_randomized(c, p, 0, 0, 0, 9, 8, 10, 1);
    place_door(c, p, st->door, 4, 3, 0);

    if (st->cross_ll) sc_fill_air(c, p, 0, 3, 1, 0, 5, 3);
    if (st->cross_rl) sc_fill_air(c, p, 9, 3, 1, 9, 5, 3);
    if (st->cross_lh) sc_fill_air(c, p, 0, 5, 7, 0, 7, 9);
    if (st->cross_rh) sc_fill_air(c, p, 9, 5, 7, 9, 7, 9);

    sc_fill_air(c, p, 5, 1, 10, 7, 3, 10);
    fill_randomized(c, p, 1, 2, 1, 8, 2, 6, 0);
    fill_randomized(c, p, 4, 1, 5, 4, 4, 9, 0);
    fill_randomized(c, p, 8, 1, 5, 8, 4, 9, 0);
    fill_randomized(c, p, 1, 4, 7, 3, 4, 9, 0);
    fill_randomized(c, p, 1, 3, 5, 3, 3, 6, 0);
    sc_fill(c, p, 1, 3, 4, 3, 3, 4, SHB_STONE_SLAB, SHB_STONE_SLAB, 0);
    sc_fill(c, p, 1, 4, 6, 3, 4, 6, SHB_STONE_SLAB, SHB_STONE_SLAB, 0);
    fill_randomized(c, p, 5, 1, 7, 7, 1, 8, 0);
    sc_fill(c, p, 5, 1, 9, 7, 1, 9, SHB_STONE_SLAB, SHB_STONE_SLAB, 0);
    sc_fill(c, p, 5, 2, 7, 7, 2, 7, SHB_STONE_SLAB, SHB_STONE_SLAB, 0);
    sc_fill(c, p, 4, 5, 7, 4, 5, 9, SHB_STONE_SLAB, SHB_STONE_SLAB, 0);
    sc_fill(c, p, 8, 5, 7, 8, 5, 9, SHB_STONE_SLAB, SHB_STONE_SLAB, 0);
    sc_fill(c, p, 5, 5, 7, 7, 5, 9, SHB_DOUBLE_STONE_SLAB, SHB_DOUBLE_STONE_SLAB, 0);
    sc_place(c, p, SHB_TORCH, 0, 6, 5, 6);
    return 1;
}

/* LeftTurn.addComponentParts. */
static int leftturn_parts(struct sc_ctx *c, struct piece *p, const struct sh_state *st)
{
    if (sc_is_liquid_in(c, p)) return 0;

    fill_randomized(c, p, 0, 0, 0, 4, 4, 4, 1);
    place_door(c, p, st->door, 1, 1, 0);

    if (p->coord_base_mode != 2 && p->coord_base_mode != 3)
        sc_fill_air(c, p, 4, 1, 1, 4, 3, 3);
    else
        sc_fill_air(c, p, 0, 1, 1, 0, 3, 3);

    return 1;
}

/* RightTurn.addComponentParts: the carve is on the other side. */
static int rightturn_parts(struct sc_ctx *c, struct piece *p, const struct sh_state *st)
{
    if (sc_is_liquid_in(c, p)) return 0;

    fill_randomized(c, p, 0, 0, 0, 4, 4, 4, 1);
    place_door(c, p, st->door, 1, 1, 0);

    if (p->coord_base_mode != 2 && p->coord_base_mode != 3)
        sc_fill_air(c, p, 0, 1, 1, 0, 3, 3);
    else
        sc_fill_air(c, p, 4, 1, 1, 4, 3, 3);

    return 1;
}

/* Library.addComponentParts. */
static int library_parts(struct sc_ctx *c, struct piece *p, const struct sh_state *st)
{
    if (sc_is_liquid_in(c, p)) return 0;

    int top = st->tall ? 11 : 6;

    fill_randomized(c, p, 0, 0, 0, 13, top - 1, 14, 1);
    place_door(c, p, st->door, 4, 1, 0);
    sc_fill_random(c, p, 0.07f, 2, 1, 1, 11, 4, 13, SHB_WEB, SHB_WEB, 0);

    for (int v = 1; v <= 13; ++v)
    {
        if ((v - 1) % 4 == 0)
        {
            sc_fill(c, p, 1, 1, v, 1, 4, v, SHB_PLANKS, SHB_PLANKS, 0);
            sc_fill(c, p, 12, 1, v, 12, 4, v, SHB_PLANKS, SHB_PLANKS, 0);
            sc_place(c, p, SHB_TORCH, 0, 2, 3, v);
            sc_place(c, p, SHB_TORCH, 0, 11, 3, v);

            if (st->tall)
            {
                sc_fill(c, p, 1, 6, v, 1, 9, v, SHB_PLANKS, SHB_PLANKS, 0);
                sc_fill(c, p, 12, 6, v, 12, 9, v, SHB_PLANKS, SHB_PLANKS, 0);
            }
        }
        else
        {
            sc_fill(c, p, 1, 1, v, 1, 4, v, SHB_BOOKSHELF, SHB_BOOKSHELF, 0);
            sc_fill(c, p, 12, 1, v, 12, 4, v, SHB_BOOKSHELF, SHB_BOOKSHELF, 0);

            if (st->tall)
            {
                sc_fill(c, p, 1, 6, v, 1, 9, v, SHB_BOOKSHELF, SHB_BOOKSHELF, 0);
                sc_fill(c, p, 12, 6, v, 12, 9, v, SHB_BOOKSHELF, SHB_BOOKSHELF, 0);
            }
        }
    }

    for (int v = 3; v < 12; v += 2)
    {
        sc_fill(c, p, 3, 1, v, 4, 3, v, SHB_BOOKSHELF, SHB_BOOKSHELF, 0);
        sc_fill(c, p, 6, 1, v, 7, 3, v, SHB_BOOKSHELF, SHB_BOOKSHELF, 0);
        sc_fill(c, p, 9, 1, v, 10, 3, v, SHB_BOOKSHELF, SHB_BOOKSHELF, 0);
    }

    if (st->tall)
    {
        sc_fill(c, p, 1, 5, 1, 3, 5, 13, SHB_PLANKS, SHB_PLANKS, 0);
        sc_fill(c, p, 10, 5, 1, 12, 5, 13, SHB_PLANKS, SHB_PLANKS, 0);
        sc_fill(c, p, 4, 5, 1, 9, 5, 2, SHB_PLANKS, SHB_PLANKS, 0);
        sc_fill(c, p, 4, 5, 12, 9, 5, 13, SHB_PLANKS, SHB_PLANKS, 0);
        sc_place(c, p, SHB_PLANKS, 0, 9, 5, 11);
        sc_place(c, p, SHB_PLANKS, 0, 8, 5, 11);
        sc_place(c, p, SHB_PLANKS, 0, 9, 5, 10);
        sc_fill(c, p, 3, 6, 2, 3, 6, 12, SHB_FENCE, SHB_FENCE, 0);
        sc_fill(c, p, 10, 6, 2, 10, 6, 10, SHB_FENCE, SHB_FENCE, 0);
        sc_fill(c, p, 4, 6, 2, 9, 6, 2, SHB_FENCE, SHB_FENCE, 0);
        sc_fill(c, p, 4, 6, 12, 8, 6, 12, SHB_FENCE, SHB_FENCE, 0);
        sc_place(c, p, SHB_FENCE, 0, 9, 6, 11);
        sc_place(c, p, SHB_FENCE, 0, 8, 6, 11);
        sc_place(c, p, SHB_FENCE, 0, 9, 6, 10);
        int m = sc_orient_meta(SHB_LADDER, 3, p->coord_base_mode);

        for (int y = 1; y <= 7; ++y) sc_place(c, p, SHB_LADDER, m, 10, y, 13);

        /* the ring: var8 7, var9 7 */
        sc_place(c, p, SHB_FENCE, 0, 6, 9, 7);
        sc_place(c, p, SHB_FENCE, 0, 7, 9, 7);
        sc_place(c, p, SHB_FENCE, 0, 6, 8, 7);
        sc_place(c, p, SHB_FENCE, 0, 7, 8, 7);
        sc_place(c, p, SHB_FENCE, 0, 6, 7, 7);
        sc_place(c, p, SHB_FENCE, 0, 7, 7, 7);
        sc_place(c, p, SHB_FENCE, 0, 5, 7, 7);
        sc_place(c, p, SHB_FENCE, 0, 8, 7, 7);
        sc_place(c, p, SHB_FENCE, 0, 6, 7, 6);
        sc_place(c, p, SHB_FENCE, 0, 6, 7, 8);
        sc_place(c, p, SHB_FENCE, 0, 7, 7, 6);
        sc_place(c, p, SHB_FENCE, 0, 7, 7, 8);
        sc_place(c, p, SHB_TORCH, 0, 5, 8, 7);
        sc_place(c, p, SHB_TORCH, 0, 8, 8, 7);
        sc_place(c, p, SHB_TORCH, 0, 6, 8, 6);
        sc_place(c, p, SHB_TORCH, 0, 6, 8, 8);
        sc_place(c, p, SHB_TORCH, 0, 7, 8, 6);
        sc_place(c, p, SHB_TORCH, 0, 7, 8, 8);
    }

    chest(c, p, "strongholdLibrary", LOOT_COUNT_1_4, 3, 3, 5);

    if (st->tall)
    {
        sc_place(c, p, SHB_AIR, 0, 12, 9, 1);
        chest(c, p, "strongholdLibrary", LOOT_COUNT_1_4, 12, 8, 1);
    }

    return 1;
}

/* PortalRoom.addComponentParts. */
static int portalroom_parts(struct sc_ctx *c, struct piece *p, struct sh_state *st)
{
    fill_randomized(c, p, 0, 0, 0, 10, 7, 15, 0);
    place_door(c, p, DOOR_GRATES, 4, 1, 0);
    fill_randomized(c, p, 1, 6, 1, 1, 6, 14, 0);
    fill_randomized(c, p, 9, 6, 1, 9, 6, 14, 0);
    fill_randomized(c, p, 2, 6, 1, 8, 6, 2, 0);
    fill_randomized(c, p, 2, 6, 14, 8, 6, 14, 0);
    fill_randomized(c, p, 1, 1, 1, 2, 1, 4, 0);
    fill_randomized(c, p, 8, 1, 1, 9, 1, 4, 0);
    sc_fill(c, p, 1, 1, 1, 1, 1, 3, SHB_FLOWING_LAVA, SHB_FLOWING_LAVA, 0);
    sc_fill(c, p, 9, 1, 1, 9, 1, 3, SHB_FLOWING_LAVA, SHB_FLOWING_LAVA, 0);
    fill_randomized(c, p, 3, 1, 8, 7, 1, 12, 0);
    sc_fill(c, p, 4, 1, 9, 6, 1, 11, SHB_FLOWING_LAVA, SHB_FLOWING_LAVA, 0);

    for (int v = 3; v < 14; v += 2)
    {
        sc_fill(c, p, 0, 3, v, 0, 4, v, SHB_IRON_BARS, SHB_IRON_BARS, 0);
        sc_fill(c, p, 10, 3, v, 10, 4, v, SHB_IRON_BARS, SHB_IRON_BARS, 0);
    }

    for (int v = 2; v < 9; v += 2) sc_fill(c, p, v, 3, 15, v, 4, 15, SHB_IRON_BARS, SHB_IRON_BARS, 0);

    int m = sc_orient_meta(SHB_STONE_BRICK_STAIRS, 3, p->coord_base_mode);

    fill_randomized(c, p, 4, 1, 5, 6, 1, 7, 0);
    fill_randomized(c, p, 4, 2, 6, 6, 2, 7, 0);
    fill_randomized(c, p, 4, 3, 7, 6, 3, 7, 0);

    for (int x = 4; x <= 6; ++x)
    {
        sc_place(c, p, SHB_STONE_BRICK_STAIRS, m, x, 1, 4);
        sc_place(c, p, SHB_STONE_BRICK_STAIRS, m, x, 2, 5);
        sc_place(c, p, SHB_STONE_BRICK_STAIRS, m, x, 3, 6);
    }

    /* the frames' facing metas by coord base mode */
    int f0 = 2, f1 = 0, f2 = 3, f3 = 1;

    switch (p->coord_base_mode)
    {
    case 0: f0 = 0; f1 = 2; break;
    case 1: f0 = 1; f1 = 3; f2 = 0; f3 = 2; break;
    case 3: f0 = 3; f1 = 1; f2 = 0; f3 = 2; break;
    default: break;
    }

    sc_place(c, p, SHB_END_PORTAL_FRAME, f0 + (jr_float(c->rand) > 0.9f ? 4 : 0), 4, 3, 8);
    sc_place(c, p, SHB_END_PORTAL_FRAME, f0 + (jr_float(c->rand) > 0.9f ? 4 : 0), 5, 3, 8);
    sc_place(c, p, SHB_END_PORTAL_FRAME, f0 + (jr_float(c->rand) > 0.9f ? 4 : 0), 6, 3, 8);
    sc_place(c, p, SHB_END_PORTAL_FRAME, f1 + (jr_float(c->rand) > 0.9f ? 4 : 0), 4, 3, 12);
    sc_place(c, p, SHB_END_PORTAL_FRAME, f1 + (jr_float(c->rand) > 0.9f ? 4 : 0), 5, 3, 12);
    sc_place(c, p, SHB_END_PORTAL_FRAME, f1 + (jr_float(c->rand) > 0.9f ? 4 : 0), 6, 3, 12);
    sc_place(c, p, SHB_END_PORTAL_FRAME, f2 + (jr_float(c->rand) > 0.9f ? 4 : 0), 3, 3, 9);
    sc_place(c, p, SHB_END_PORTAL_FRAME, f2 + (jr_float(c->rand) > 0.9f ? 4 : 0), 3, 3, 10);
    sc_place(c, p, SHB_END_PORTAL_FRAME, f2 + (jr_float(c->rand) > 0.9f ? 4 : 0), 3, 3, 11);
    sc_place(c, p, SHB_END_PORTAL_FRAME, f3 + (jr_float(c->rand) > 0.9f ? 4 : 0), 7, 3, 9);
    sc_place(c, p, SHB_END_PORTAL_FRAME, f3 + (jr_float(c->rand) > 0.9f ? 4 : 0), 7, 3, 10);
    sc_place(c, p, SHB_END_PORTAL_FRAME, f3 + (jr_float(c->rand) > 0.9f ? 4 : 0), 7, 3, 11);

    if (!st->mob)
    {
        int y = sc_y_with_offset(p, 3);
        int x = sc_x_with_offset(p, 5, 6);
        int z = sc_z_with_offset(p, 5, 6);

        if (sc_in_step_box(c, x, y, z))
        {
            st->mob = 1;
            world_set_block(c->w, x, y, z, SHB_MOB_SPAWNER, 0, 2);
            struct tile_entity *te = world_tile_entity(c->w, x, y, z);

            if (te != NULL)
            {
                free(te->u.spawner.mob_id);
                te->u.spawner.mob_id = dup_text("Silverfish");
            }
        }
    }

    return 1;
}

/* Prison.addComponentParts. */
static int prison_parts(struct sc_ctx *c, struct piece *p, const struct sh_state *st)
{
    if (sc_is_liquid_in(c, p)) return 0;

    fill_randomized(c, p, 0, 0, 0, 8, 4, 10, 1);
    place_door(c, p, st->door, 1, 1, 0);
    sc_fill_air(c, p, 1, 1, 10, 3, 3, 10);
    fill_randomized(c, p, 4, 1, 1, 4, 3, 1, 0);
    fill_randomized(c, p, 4, 1, 3, 4, 3, 3, 0);
    fill_randomized(c, p, 4, 1, 7, 4, 3, 7, 0);
    fill_randomized(c, p, 4, 1, 9, 4, 3, 9, 0);
    sc_fill(c, p, 4, 1, 4, 4, 3, 6, SHB_IRON_BARS, SHB_IRON_BARS, 0);
    sc_fill(c, p, 5, 1, 5, 7, 3, 5, SHB_IRON_BARS, SHB_IRON_BARS, 0);
    sc_place(c, p, SHB_IRON_BARS, 0, 4, 3, 2);
    sc_place(c, p, SHB_IRON_BARS, 0, 4, 3, 8);

    int m = sc_orient_meta(SHB_IRON_DOOR, 3, p->coord_base_mode);

    sc_place(c, p, SHB_IRON_DOOR, m, 4, 1, 2);
    sc_place(c, p, SHB_IRON_DOOR, m + 8, 4, 2, 2);
    sc_place(c, p, SHB_IRON_DOOR, m, 4, 1, 8);
    sc_place(c, p, SHB_IRON_DOOR, m + 8, 4, 2, 8);
    return 1;
}

/* RoomCrossing.addComponentParts. */
static int roomcrossing_parts(struct sc_ctx *c, struct piece *p, const struct sh_state *st)
{
    if (sc_is_liquid_in(c, p)) return 0;

    fill_randomized(c, p, 0, 0, 0, 10, 6, 10, 1);
    place_door(c, p, st->door, 4, 1, 0);
    sc_fill_air(c, p, 4, 1, 10, 6, 3, 10);
    sc_fill_air(c, p, 0, 1, 4, 0, 3, 6);
    sc_fill_air(c, p, 10, 1, 4, 10, 3, 6);

    switch (st->room_type)
    {
    case 0:
        sc_place(c, p, SHB_STONEBRICK, 0, 5, 1, 5);
        sc_place(c, p, SHB_STONEBRICK, 0, 5, 2, 5);
        sc_place(c, p, SHB_STONEBRICK, 0, 5, 3, 5);
        sc_place(c, p, SHB_TORCH, 0, 4, 3, 5);
        sc_place(c, p, SHB_TORCH, 0, 6, 3, 5);
        sc_place(c, p, SHB_TORCH, 0, 5, 3, 4);
        sc_place(c, p, SHB_TORCH, 0, 5, 3, 6);
        sc_place(c, p, SHB_STONE_SLAB, 0, 4, 1, 4);
        sc_place(c, p, SHB_STONE_SLAB, 0, 4, 1, 5);
        sc_place(c, p, SHB_STONE_SLAB, 0, 4, 1, 6);
        sc_place(c, p, SHB_STONE_SLAB, 0, 6, 1, 4);
        sc_place(c, p, SHB_STONE_SLAB, 0, 6, 1, 5);
        sc_place(c, p, SHB_STONE_SLAB, 0, 6, 1, 6);
        sc_place(c, p, SHB_STONE_SLAB, 0, 5, 1, 4);
        sc_place(c, p, SHB_STONE_SLAB, 0, 5, 1, 6);
        break;

    case 1:
        for (int v = 0; v < 5; ++v)
        {
            sc_place(c, p, SHB_STONEBRICK, 0, 3, 1, 3 + v);
            sc_place(c, p, SHB_STONEBRICK, 0, 7, 1, 3 + v);
            sc_place(c, p, SHB_STONEBRICK, 0, 3 + v, 1, 3);
            sc_place(c, p, SHB_STONEBRICK, 0, 3 + v, 1, 7);
        }

        sc_place(c, p, SHB_STONEBRICK, 0, 5, 1, 5);
        sc_place(c, p, SHB_STONEBRICK, 0, 5, 2, 5);
        sc_place(c, p, SHB_STONEBRICK, 0, 5, 3, 5);
        sc_place(c, p, SHB_FLOWING_WATER, 0, 5, 4, 5);
        break;

    case 2:
    {
        for (int v = 1; v <= 9; ++v)
        {
            sc_place(c, p, SHB_COBBLESTONE, 0, 1, 3, v);
            sc_place(c, p, SHB_COBBLESTONE, 0, 9, 3, v);
        }

        for (int v = 1; v <= 9; ++v)
        {
            sc_place(c, p, SHB_COBBLESTONE, 0, v, 3, 1);
            sc_place(c, p, SHB_COBBLESTONE, 0, v, 3, 9);
        }

        sc_place(c, p, SHB_COBBLESTONE, 0, 5, 1, 4);
        sc_place(c, p, SHB_COBBLESTONE, 0, 5, 1, 6);
        sc_place(c, p, SHB_COBBLESTONE, 0, 5, 3, 4);
        sc_place(c, p, SHB_COBBLESTONE, 0, 5, 3, 6);
        sc_place(c, p, SHB_COBBLESTONE, 0, 4, 1, 5);
        sc_place(c, p, SHB_COBBLESTONE, 0, 6, 1, 5);
        sc_place(c, p, SHB_COBBLESTONE, 0, 4, 3, 5);
        sc_place(c, p, SHB_COBBLESTONE, 0, 6, 3, 5);

        for (int v = 1; v <= 3; ++v)
        {
            sc_place(c, p, SHB_COBBLESTONE, 0, 4, v, 4);
            sc_place(c, p, SHB_COBBLESTONE, 0, 6, v, 4);
            sc_place(c, p, SHB_COBBLESTONE, 0, 4, v, 6);
            sc_place(c, p, SHB_COBBLESTONE, 0, 6, v, 6);
        }

        sc_place(c, p, SHB_TORCH, 0, 5, 3, 5);

        for (int v = 2; v <= 8; ++v)
        {
            sc_place(c, p, SHB_PLANKS, 0, 2, 3, v);
            sc_place(c, p, SHB_PLANKS, 0, 3, 3, v);

            if (v <= 3 || v >= 7)
            {
                sc_place(c, p, SHB_PLANKS, 0, 4, 3, v);
                sc_place(c, p, SHB_PLANKS, 0, 5, 3, v);
                sc_place(c, p, SHB_PLANKS, 0, 6, 3, v);
            }

            sc_place(c, p, SHB_PLANKS, 0, 7, 3, v);
            sc_place(c, p, SHB_PLANKS, 0, 8, 3, v);
        }

        int m = sc_orient_meta(SHB_LADDER, 4, p->coord_base_mode);

        sc_place(c, p, SHB_LADDER, m, 9, 1, 3);
        sc_place(c, p, SHB_LADDER, m, 9, 2, 3);
        sc_place(c, p, SHB_LADDER, m, 9, 3, 3);
        chest(c, p, "strongholdCrossing", LOOT_COUNT_1_4, 3, 4, 8);
        break;
    }

    default:
        break;
    }

    return 1;
}

/* Stairs.addComponentParts (Stairs2 inherits it). */
static int stairs_parts(struct sc_ctx *c, struct piece *p, const struct sh_state *st)
{
    if (sc_is_liquid_in(c, p)) return 0;

    fill_randomized(c, p, 0, 0, 0, 4, 10, 4, 1);
    place_door(c, p, st->door, 1, 7, 0);
    place_door(c, p, DOOR_OPENING, 1, 1, 4);
    sc_place(c, p, SHB_STONEBRICK, 0, 2, 6, 1);
    sc_place(c, p, SHB_STONEBRICK, 0, 1, 5, 1);
    sc_place(c, p, SHB_STONE_SLAB, 0, 1, 6, 1);
    sc_place(c, p, SHB_STONEBRICK, 0, 1, 5, 2);
    sc_place(c, p, SHB_STONEBRICK, 0, 1, 4, 3);
    sc_place(c, p, SHB_STONE_SLAB, 0, 1, 5, 3);
    sc_place(c, p, SHB_STONEBRICK, 0, 2, 4, 3);
    sc_place(c, p, SHB_STONEBRICK, 0, 3, 3, 3);
    sc_place(c, p, SHB_STONE_SLAB, 0, 3, 4, 3);
    sc_place(c, p, SHB_STONEBRICK, 0, 3, 3, 2);
    sc_place(c, p, SHB_STONEBRICK, 0, 3, 2, 1);
    sc_place(c, p, SHB_STONE_SLAB, 0, 3, 3, 1);
    sc_place(c, p, SHB_STONEBRICK, 0, 2, 2, 1);
    sc_place(c, p, SHB_STONEBRICK, 0, 1, 1, 1);
    sc_place(c, p, SHB_STONE_SLAB, 0, 1, 2, 1);
    sc_place(c, p, SHB_STONEBRICK, 0, 1, 1, 2);
    sc_place(c, p, SHB_STONE_SLAB, 0, 1, 1, 3);
    return 1;
}

/* StairsStraight.addComponentParts. */
static int stairsstraight_parts(struct sc_ctx *c, struct piece *p, const struct sh_state *st)
{
    if (sc_is_liquid_in(c, p)) return 0;

    fill_randomized(c, p, 0, 0, 0, 4, 10, 7, 1);
    place_door(c, p, st->door, 1, 7, 0);
    place_door(c, p, DOOR_OPENING, 1, 1, 7);
    int m = sc_orient_meta(SHB_STONE_STAIRS, 2, p->coord_base_mode);

    for (int v = 0; v < 6; ++v)
    {
        sc_place(c, p, SHB_STONE_STAIRS, m, 1, 6 - v, 1 + v);
        sc_place(c, p, SHB_STONE_STAIRS, m, 2, 6 - v, 1 + v);
        sc_place(c, p, SHB_STONE_STAIRS, m, 3, 6 - v, 1 + v);

        if (v < 5)
        {
            sc_place(c, p, SHB_STONEBRICK, 0, 1, 5 - v, 1 + v);
            sc_place(c, p, SHB_STONEBRICK, 0, 2, 5 - v, 1 + v);
            sc_place(c, p, SHB_STONEBRICK, 0, 3, 5 - v, 1 + v);
        }
    }

    return 1;
}

int stronghold_blocks(struct sc_ctx *c, struct piece *p)
{
    struct sh_state *st = (struct sh_state *)stronghold_state(p);

    switch (st->kind)
    {
    case SHS_STRAIGHT: return straight_parts(c, p, st);
    case SHS_PRISON: return prison_parts(c, p, st);
    case SHS_LEFTTURN: return leftturn_parts(c, p, st);
    case SHS_RIGHTTURN: return rightturn_parts(c, p, st);
    case SHS_ROOMCROSSING: return roomcrossing_parts(c, p, st);
    case SHS_STAIRSSTRAIGHT: return stairsstraight_parts(c, p, st);
    case SHS_STAIRS: return stairs_parts(c, p, st);
    case SHS_CROSSING: return crossing_parts(c, p, st);
    case SHS_CHESTCORRIDOR: return chestcorridor_parts(c, p, st);
    case SHS_LIBRARY: return library_parts(c, p, st);
    case SHS_PORTALROOM: return portalroom_parts(c, p, st);
    case SHS_CORRIDOR: return corridor_parts(c, p, st);
    default: return 1;
    }
}
