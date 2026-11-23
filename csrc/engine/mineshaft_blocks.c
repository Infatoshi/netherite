/* StructureMineshaftPieces' addComponentParts, the four kinds' block halves:
 * the room (dirt floor, entrance carves, the dome), the corridor (rails, webs,
 * torches, fence posts, the spawner and the chest cart), the crossing and the
 * stairs. Every Random draw is in Java's order; see structure_blocks.c for the
 * helpers and the env the writes run in. */
#include "mineshaft.h"

#include "blocks.h"

#include <stdlib.h>
#include <string.h>

static int imin(int a, int b) { return a < b ? a : b; }

static char *dup_text(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);

    if (p) memcpy(p, s, n);
    return p;
}

/* the material a block id carries */
static int material_is_air(int id)
{
    return strcmp(MATERIALS[BLOCKS[id & 4095].material].name, "air") == 0;
}

/* Corridor.generateStructureChestContents: the corridor's own chest check, a
 * rail under the cart and the cart itself. Java evaluates the call's
 * arguments before its body, so the table's book stack and the count draw
 * happen even when the position then fails the box or air check and nothing
 * is placed. When the check passes the order is Java's: the rail metadata
 * bool, the rail block, the cart's constructor draws, then the slots. */
static int corridor_cart(struct sc_ctx *c, struct piece *p, int tx, int ty, int tz)
{
    int x = sc_x_with_offset(p, tx, tz), y = sc_y_with_offset(p, ty), z = sc_z_with_offset(p, tx, tz);

    const struct loot_table *table = loot_table_by_name("mineshaftCorridor");
    struct loot_book book;

    loot_book_build(c->rand, &table->entries[table->n - 1], &book);
    int count = 3 + jr_int_n(c->rand, 4);

    if (!sc_in_step_box(c, x, y, z) || !material_is_air(world_get_block(c->w, x, y, z)))
    {
        loot_book_free(&book);
        return 0;
    }

    int rail_meta = jr_bool(c->rand) ? 1 : 0;
    sc_place(c, p, SCB_RAIL, sc_orient_meta(SCB_RAIL, rail_meta, p->coord_base_mode), tx, ty, tz);

    struct loot_stack slots[CART_SLOTS];

    memset(slots, 0, sizeof slots);
    loot_generate_contents(c->rand, table, &book, slots, CART_SLOTS, count);
    sc_cart_new(c, (double)((float)x + 0.5f), (double)((float)y + 0.5f), (double)((float)z + 0.5f), slots);
    loot_book_free(&book);
    return 1;
}

/* Corridor.addComponentParts. */
static int corridor_parts(struct sc_ctx *c, struct piece *p)
{
    if (sc_is_liquid_in(c, p)) return 0;

    int var8 = p->u.corridor.section_count * 5 - 1;

    sc_fill(c, p, 0, 0, 0, 2, 1, var8, SCB_AIR, SCB_AIR, 0);
    sc_fill_random(c, p, 0.8f, 0, 2, 0, 2, 2, var8, SCB_AIR, SCB_AIR, 0);

    if (p->u.corridor.has_spiders)
        sc_fill_random(c, p, 0.6f, 0, 0, 0, 2, 1, var8, SCB_WEB, SCB_AIR, 0);

    for (int var9 = 0; var9 < p->u.corridor.section_count; ++var9)
    {
        int var10 = 2 + var9 * 5;

        sc_fill(c, p, 0, 0, var10, 0, 1, var10, SCB_FENCE, SCB_AIR, 0);
        sc_fill(c, p, 2, 0, var10, 2, 1, var10, SCB_FENCE, SCB_AIR, 0);

        if (jr_int_n(c->rand, 4) == 0)
        {
            sc_fill(c, p, 0, 2, var10, 0, 2, var10, SCB_PLANKS, SCB_AIR, 0);
            sc_fill(c, p, 2, 2, var10, 2, 2, var10, SCB_PLANKS, SCB_AIR, 0);
        }
        else
        {
            sc_fill(c, p, 0, 2, var10, 2, 2, var10, SCB_PLANKS, SCB_AIR, 0);
        }

        sc_random_place(c, p, 0.1f, 0, 2, var10 - 1, SCB_WEB, 0);
        sc_random_place(c, p, 0.1f, 2, 2, var10 - 1, SCB_WEB, 0);
        sc_random_place(c, p, 0.1f, 0, 2, var10 + 1, SCB_WEB, 0);
        sc_random_place(c, p, 0.1f, 2, 2, var10 + 1, SCB_WEB, 0);
        sc_random_place(c, p, 0.05f, 0, 2, var10 - 2, SCB_WEB, 0);
        sc_random_place(c, p, 0.05f, 2, 2, var10 - 2, SCB_WEB, 0);
        sc_random_place(c, p, 0.05f, 0, 2, var10 + 2, SCB_WEB, 0);
        sc_random_place(c, p, 0.05f, 2, 2, var10 + 2, SCB_WEB, 0);
        sc_random_place(c, p, 0.05f, 1, 2, var10 - 1, SCB_TORCH, 0);
        sc_random_place(c, p, 0.05f, 1, 2, var10 + 1, SCB_TORCH, 0);

        if (jr_int_n(c->rand, 100) == 0) corridor_cart(c, p, 2, 0, var10 - 1);
        if (jr_int_n(c->rand, 100) == 0) corridor_cart(c, p, 0, 0, var10 + 1);

        if (p->u.corridor.has_spiders && !p->u.corridor.spawner_placed)
        {
            int var11 = sc_y_with_offset(p, 0);
            int var12 = var10 - 1 + jr_int_n(c->rand, 3);
            int var13 = sc_x_with_offset(p, 1, var12);
            var12 = sc_z_with_offset(p, 1, var12);

            if (sc_in_step_box(c, var13, var11, var12))
            {
                p->u.corridor.spawner_placed = 1;
                world_set_block(c->w, var13, var11, var12, SCB_MOB_SPAWNER, 0, 2);
                struct tile_entity *te = world_tile_entity(c->w, var13, var11, var12);

                if (te != NULL)
                {
                    free(te->u.spawner.mob_id);
                    te->u.spawner.mob_id = dup_text("CaveSpider");
                }
            }
        }
    }

    for (int var9 = 0; var9 <= 2; ++var9)
        for (int var10 = 0; var10 <= var8; ++var10)
            if (sc_block_at(c, p, var9, -1, var10) == SCB_AIR)
                sc_place(c, p, SCB_PLANKS, 0, var9, -1, var10);

    if (p->u.corridor.has_rails)
    {
        for (int var9 = 0; var9 <= var8; ++var9)
        {
            int below = sc_block_at(c, p, 1, -1, var9);

            if (below != SCB_AIR && BLOCKS[below & 4095].opaque_cube)
                sc_random_place(c, p, 0.7f, 1, 0, var9, SCB_RAIL,
                                sc_orient_meta(SCB_RAIL, 0, p->coord_base_mode));
        }
    }

    return 1;
}

/* Cross.addComponentParts: absolute coordinates, the piece's coordBaseMode is
 * -1. */
static int crossing_parts(struct sc_ctx *c, struct piece *p)
{
    if (sc_is_liquid_in(c, p)) return 0;

    if (p->u.crossing.multiple_floors)
    {
        sc_fill(c, p, p->bb.minX + 1, p->bb.minY, p->bb.minZ, p->bb.maxX - 1, p->bb.minY + 3 - 1, p->bb.maxZ,
                SCB_AIR, SCB_AIR, 0);
        sc_fill(c, p, p->bb.minX, p->bb.minY, p->bb.minZ + 1, p->bb.maxX, p->bb.minY + 3 - 1, p->bb.maxZ - 1,
                SCB_AIR, SCB_AIR, 0);
        sc_fill(c, p, p->bb.minX + 1, p->bb.maxY - 2, p->bb.minZ, p->bb.maxX - 1, p->bb.maxY, p->bb.maxZ,
                SCB_AIR, SCB_AIR, 0);
        sc_fill(c, p, p->bb.minX, p->bb.maxY - 2, p->bb.minZ + 1, p->bb.maxX, p->bb.maxY, p->bb.maxZ - 1,
                SCB_AIR, SCB_AIR, 0);
        sc_fill(c, p, p->bb.minX + 1, p->bb.minY + 3, p->bb.minZ + 1, p->bb.maxX - 1, p->bb.minY + 3, p->bb.maxZ - 1,
                SCB_AIR, SCB_AIR, 0);
    }
    else
    {
        sc_fill(c, p, p->bb.minX + 1, p->bb.minY, p->bb.minZ, p->bb.maxX - 1, p->bb.maxY, p->bb.maxZ,
                SCB_AIR, SCB_AIR, 0);
        sc_fill(c, p, p->bb.minX, p->bb.minY, p->bb.minZ + 1, p->bb.maxX, p->bb.maxY, p->bb.maxZ - 1,
                SCB_AIR, SCB_AIR, 0);
    }

    sc_fill(c, p, p->bb.minX + 1, p->bb.minY, p->bb.minZ + 1, p->bb.minX + 1, p->bb.maxY, p->bb.minZ + 1,
            SCB_PLANKS, SCB_AIR, 0);
    sc_fill(c, p, p->bb.minX + 1, p->bb.minY, p->bb.maxZ - 1, p->bb.minX + 1, p->bb.maxY, p->bb.maxZ - 1,
            SCB_PLANKS, SCB_AIR, 0);
    sc_fill(c, p, p->bb.maxX - 1, p->bb.minY, p->bb.minZ + 1, p->bb.maxX - 1, p->bb.maxY, p->bb.minZ + 1,
            SCB_PLANKS, SCB_AIR, 0);
    sc_fill(c, p, p->bb.maxX - 1, p->bb.minY, p->bb.maxZ - 1, p->bb.maxX - 1, p->bb.maxY, p->bb.maxZ - 1,
            SCB_PLANKS, SCB_AIR, 0);

    for (int x = p->bb.minX; x <= p->bb.maxX; ++x)
        for (int z = p->bb.minZ; z <= p->bb.maxZ; ++z)
            if (sc_block_at(c, p, x, p->bb.minY - 1, z) == SCB_AIR)
                sc_place(c, p, SCB_PLANKS, 0, x, p->bb.minY - 1, z);

    return 1;
}

/* Room.addComponentParts: absolute coordinates; roomsLinkedToTheRoom are the
 * entrance carves. */
static int room_parts(struct sc_ctx *c, struct piece *p)
{
    if (sc_is_liquid_in(c, p)) return 0;

    sc_fill(c, p, p->bb.minX, p->bb.minY, p->bb.minZ, p->bb.maxX, p->bb.minY, p->bb.maxZ, SCB_DIRT, SCB_AIR, 1);
    sc_fill(c, p, p->bb.minX, p->bb.minY + 1, p->bb.minZ, p->bb.maxX, imin(p->bb.minY + 3, p->bb.maxY), p->bb.maxZ,
            SCB_AIR, SCB_AIR, 0);

    for (int i = 0; i < p->u.room.n; ++i)
    {
        const struct bbox *b = &p->u.room.v[i];

        sc_fill(c, p, b->minX, b->maxY - 2, b->minZ, b->maxX, b->maxY, b->maxZ, SCB_AIR, SCB_AIR, 0);
    }

    sc_fill_dome(c, p, p->bb.minX, p->bb.minY + 4, p->bb.minZ, p->bb.maxX, p->bb.maxY, p->bb.maxZ, SCB_AIR, 0);
    return 1;
}

/* Stairs.addComponentParts. */
static int stairs_parts(struct sc_ctx *c, struct piece *p)
{
    if (sc_is_liquid_in(c, p)) return 0;

    sc_fill(c, p, 0, 5, 0, 2, 7, 1, SCB_AIR, SCB_AIR, 0);
    sc_fill(c, p, 0, 0, 7, 2, 2, 8, SCB_AIR, SCB_AIR, 0);

    for (int var4 = 0; var4 < 5; ++var4)
    {
        int minY = 5 - var4 - (var4 < 4 ? 1 : 0);

        sc_fill(c, p, 0, minY, 2 + var4, 2, 7 - var4, 2 + var4, SCB_AIR, SCB_AIR, 0);
    }

    return 1;
}

int mineshaft_blocks(struct sc_ctx *c, struct piece *p)
{
    switch (p->kind)
    {
    case PIECE_CORRIDOR: return corridor_parts(c, p);
    case PIECE_CROSSING: return crossing_parts(c, p);
    case PIECE_ROOM: return room_parts(c, p);
    case PIECE_STAIRS: return stairs_parts(c, p);
    default: return 1;
    }
}