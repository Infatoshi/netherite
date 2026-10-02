/* StructureMineshaftPieces and StructureMineshaftStart. Every Random draw is in
 * Java's order: a constructor draws before its buildComponent, and each call
 * that draws twice is split into statements, since C leaves argument order
 * unspecified. */
#include "mineshaft.h"

#include <stdio.h>
#include <stdlib.h>

/* MapGenMineshaft.field_82673_e, world.conf's "chance" default. */
#define MINESHAFT_CHANCE 0.004

static int abs_int(int v)
{
    return v < 0 ? -v : v;
}

static struct piece *piece_new(const char *id, enum piece_kind kind, int type)
{
    struct piece *p = calloc(1, sizeof *p);
    if (!p) abort();
    p->id = id;
    p->kind = kind;
    p->component_type = type;
    p->coord_base_mode = -1;   /* StructureComponent(int) */
    return p;
}

static void room_add_entrance(struct piece *p, struct bbox b)
{
    if (p->u.room.n == p->u.room.cap)
    {
        p->u.room.cap = p->u.room.cap ? 2 * p->u.room.cap : 4;
        p->u.room.v = realloc(p->u.room.v, (size_t)p->u.room.cap * sizeof *p->u.room.v);
        if (!p->u.room.v) abort();
    }
    p->u.room.v[p->u.room.n++] = b;
}

/* ---- the pieces ---- */

static struct piece *corridor_new(int type, jrand *rand, struct bbox bb, int mode)
{
    struct piece *p = piece_new("MSCorridor", PIECE_CORRIDOR, type);
    p->coord_base_mode = mode;
    p->bb = bb;
    p->u.corridor.has_rails = jr_int_n(rand, 3) == 0;
    p->u.corridor.has_spiders = !p->u.corridor.has_rails && jr_int_n(rand, 23) == 0;
    p->u.corridor.spawner_placed = 0;
    p->u.corridor.section_count = (mode != 2 && mode != 0) ? bbox_xsize(&bb) / 5 : bbox_zsize(&bb) / 5;
    return p;
}

static struct piece *crossing_new(int type, struct bbox bb, int mode)
{
    struct piece *p = piece_new("MSCrossing", PIECE_CROSSING, type);
    /* Cross(int, Random, StructureBoundingBox, int) sets corridorDirection but
     * leaves coordBaseMode at StructureComponent(int)'s -1. */
    p->u.crossing.corridor_direction = mode;
    p->bb = bb;
    p->u.crossing.multiple_floors = bbox_ysize(&bb) > 3;
    return p;
}

static struct piece *stairs_new(int type, struct bbox bb, int mode)
{
    struct piece *p = piece_new("MSStairs", PIECE_STAIRS, type);
    p->coord_base_mode = mode;
    p->bb = bb;
    return p;
}

static struct piece *room_new(int type, jrand *rand, int x, int z)
{
    struct piece *p = piece_new("MSRoom", PIECE_ROOM, type);
    int sizeX = jr_int_n(rand, 6);
    int sizeY = jr_int_n(rand, 6);
    int sizeZ = jr_int_n(rand, 6);
    p->bb = bbox_make(x, 50, z, x + 7 + sizeX, 54 + sizeY, z + 7 + sizeZ);
    return p;
}

/* ---- placement tests ---- */

static int corridor_find(struct start *s, jrand *rand, int x, int y, int z, int mode, struct bbox *out)
{
    struct bbox bb = bbox_make(x, y, z, x, y + 2, z);
    int tries;

    for (tries = jr_int_n(rand, 3) + 2; tries > 0; --tries)
    {
        int length = tries * 5;

        switch (mode)
        {
            case 0: bb.maxX = x + 2; bb.maxZ = z + (length - 1); break;
            case 1: bb.minX = x - (length - 1); bb.maxZ = z + 2; break;
            case 2: bb.maxX = x + 2; bb.minZ = z - (length - 1); break;
            case 3: bb.maxX = x + (length - 1); bb.maxZ = z + 2; break;
            default: break;
        }

        if (start_find_intersecting(s, &bb) == NULL) break;
    }

    if (tries <= 0) return 0;
    *out = bb;
    return 1;
}

static int crossing_find(struct start *s, jrand *rand, int x, int y, int z, int mode, struct bbox *out)
{
    struct bbox bb = bbox_make(x, y, z, x, y + 2, z);
    if (jr_int_n(rand, 4) == 0) bb.maxY += 4;

    switch (mode)
    {
        case 0: bb.minX = x - 1; bb.maxX = x + 3; bb.maxZ = z + 4; break;
        case 1: bb.minX = x - 4; bb.minZ = z - 1; bb.maxZ = z + 3; break;
        case 2: bb.minX = x - 1; bb.maxX = x + 3; bb.minZ = z - 4; break;
        case 3: bb.maxX = x + 4; bb.minZ = z - 1; bb.maxZ = z + 3; break;
        default: break;
    }

    if (start_find_intersecting(s, &bb) != NULL) return 0;
    *out = bb;
    return 1;
}

static int stairs_find(struct start *s, jrand *rand, int x, int y, int z, int mode, struct bbox *out)
{
    struct bbox bb = bbox_make(x, y - 5, z, x, y + 2, z);

    switch (mode)
    {
        case 0: bb.maxX = x + 2; bb.maxZ = z + 8; break;
        case 1: bb.minX = x - 8; bb.maxZ = z + 2; break;
        case 2: bb.maxX = x + 2; bb.minZ = z - 8; break;
        case 3: bb.maxX = x + 8; bb.maxZ = z + 2; break;
        default: break;
    }

    if (start_find_intersecting(s, &bb) != NULL) return 0;
    *out = bb;
    return 1;
}

static struct piece *random_component(struct start *s, jrand *rand, int x, int y, int z, int mode, int type)
{
    int pick = jr_int_n(rand, 100);
    struct bbox bb;

    if (pick >= 80)
    {
        if (!crossing_find(s, rand, x, y, z, mode, &bb)) return NULL;
        return crossing_new(type, bb, mode);
    }
    else if (pick >= 70)
    {
        if (!stairs_find(s, rand, x, y, z, mode, &bb)) return NULL;
        return stairs_new(type, bb, mode);
    }

    if (!corridor_find(s, rand, x, y, z, mode, &bb)) return NULL;
    return corridor_new(type, rand, bb, mode);
}

static struct piece *next_piece(struct piece *parent, struct start *s, jrand *rand,
                                int x, int y, int z, int mode, int depth)
{
    if (depth > 8) return NULL;
    if (abs_int(x - parent->bb.minX) > 80 || abs_int(z - parent->bb.minZ) > 80) return NULL;

    struct piece *p = random_component(s, rand, x, y, z, mode, depth + 1);
    if (p)
    {
        start_add(s, p);
        piece_build_component(p, parent, s, rand);
    }
    return p;
}

/* ---- buildComponent ---- */

static void corridor_build(struct piece *p, struct piece *parent, struct start *s, jrand *rand)
{
    int type = p->component_type;
    int pick = jr_int_n(rand, 4);
    int y;

    switch (p->coord_base_mode)
    {
        case 0:
            if (pick <= 1)
            {
                y = p->bb.minY - 1 + jr_int_n(rand, 3);
                next_piece(parent, s, rand, p->bb.minX, y, p->bb.maxZ + 1, 0, type);
            }
            else if (pick == 2)
            {
                y = p->bb.minY - 1 + jr_int_n(rand, 3);
                next_piece(parent, s, rand, p->bb.minX - 1, y, p->bb.maxZ - 3, 1, type);
            }
            else
            {
                y = p->bb.minY - 1 + jr_int_n(rand, 3);
                next_piece(parent, s, rand, p->bb.maxX + 1, y, p->bb.maxZ - 3, 3, type);
            }
            break;
        case 1:
            if (pick <= 1)
            {
                y = p->bb.minY - 1 + jr_int_n(rand, 3);
                next_piece(parent, s, rand, p->bb.minX - 1, y, p->bb.minZ, 1, type);
            }
            else if (pick == 2)
            {
                y = p->bb.minY - 1 + jr_int_n(rand, 3);
                next_piece(parent, s, rand, p->bb.minX, y, p->bb.minZ - 1, 2, type);
            }
            else
            {
                y = p->bb.minY - 1 + jr_int_n(rand, 3);
                next_piece(parent, s, rand, p->bb.minX, y, p->bb.maxZ + 1, 0, type);
            }
            break;
        case 2:
            if (pick <= 1)
            {
                y = p->bb.minY - 1 + jr_int_n(rand, 3);
                next_piece(parent, s, rand, p->bb.minX, y, p->bb.minZ - 1, 2, type);
            }
            else if (pick == 2)
            {
                y = p->bb.minY - 1 + jr_int_n(rand, 3);
                next_piece(parent, s, rand, p->bb.minX - 1, y, p->bb.minZ, 1, type);
            }
            else
            {
                y = p->bb.minY - 1 + jr_int_n(rand, 3);
                next_piece(parent, s, rand, p->bb.maxX + 1, y, p->bb.minZ, 3, type);
            }
            break;
        case 3:
            if (pick <= 1)
            {
                y = p->bb.minY - 1 + jr_int_n(rand, 3);
                next_piece(parent, s, rand, p->bb.maxX + 1, y, p->bb.minZ, 3, type);
            }
            else if (pick == 2)
            {
                y = p->bb.minY - 1 + jr_int_n(rand, 3);
                next_piece(parent, s, rand, p->bb.maxX - 3, y, p->bb.minZ - 1, 2, type);
            }
            else
            {
                y = p->bb.minY - 1 + jr_int_n(rand, 3);
                next_piece(parent, s, rand, p->bb.maxX - 3, y, p->bb.maxZ + 1, 0, type);
            }
            break;
        default: break;
    }

    if (type < 8)
    {
        int i, roll;

        if (p->coord_base_mode != 2 && p->coord_base_mode != 0)
        {
            for (i = p->bb.minX + 3; i + 3 <= p->bb.maxX; i += 5)
            {
                roll = jr_int_n(rand, 5);

                if (roll == 0) next_piece(parent, s, rand, i, p->bb.minY, p->bb.minZ - 1, 2, type + 1);
                else if (roll == 1) next_piece(parent, s, rand, i, p->bb.minY, p->bb.maxZ + 1, 0, type + 1);
            }
        }
        else
        {
            for (i = p->bb.minZ + 3; i + 3 <= p->bb.maxZ; i += 5)
            {
                roll = jr_int_n(rand, 5);

                if (roll == 0) next_piece(parent, s, rand, p->bb.minX - 1, p->bb.minY, i, 1, type + 1);
                else if (roll == 1) next_piece(parent, s, rand, p->bb.maxX + 1, p->bb.minY, i, 3, type + 1);
            }
        }
    }
}

static void crossing_build(struct piece *p, struct piece *parent, struct start *s, jrand *rand)
{
    int type = p->component_type;

    switch (p->u.crossing.corridor_direction)
    {
        case 0:
            next_piece(parent, s, rand, p->bb.minX + 1, p->bb.minY, p->bb.maxZ + 1, 0, type);
            next_piece(parent, s, rand, p->bb.minX - 1, p->bb.minY, p->bb.minZ + 1, 1, type);
            next_piece(parent, s, rand, p->bb.maxX + 1, p->bb.minY, p->bb.minZ + 1, 3, type);
            break;
        case 1:
            next_piece(parent, s, rand, p->bb.minX + 1, p->bb.minY, p->bb.minZ - 1, 2, type);
            next_piece(parent, s, rand, p->bb.minX + 1, p->bb.minY, p->bb.maxZ + 1, 0, type);
            next_piece(parent, s, rand, p->bb.minX - 1, p->bb.minY, p->bb.minZ + 1, 1, type);
            break;
        case 2:
            next_piece(parent, s, rand, p->bb.minX + 1, p->bb.minY, p->bb.minZ - 1, 2, type);
            next_piece(parent, s, rand, p->bb.minX - 1, p->bb.minY, p->bb.minZ + 1, 1, type);
            next_piece(parent, s, rand, p->bb.maxX + 1, p->bb.minY, p->bb.minZ + 1, 3, type);
            break;
        case 3:
            next_piece(parent, s, rand, p->bb.minX + 1, p->bb.minY, p->bb.minZ - 1, 2, type);
            next_piece(parent, s, rand, p->bb.minX + 1, p->bb.minY, p->bb.maxZ + 1, 0, type);
            next_piece(parent, s, rand, p->bb.maxX + 1, p->bb.minY, p->bb.minZ + 1, 3, type);
            break;
        default: break;
    }

    if (p->u.crossing.multiple_floors)
    {
        if (jr_bool(rand)) next_piece(parent, s, rand, p->bb.minX + 1, p->bb.minY + 4, p->bb.minZ - 1, 2, type);
        if (jr_bool(rand)) next_piece(parent, s, rand, p->bb.minX - 1, p->bb.minY + 4, p->bb.minZ + 1, 1, type);
        if (jr_bool(rand)) next_piece(parent, s, rand, p->bb.maxX + 1, p->bb.minY + 4, p->bb.minZ + 1, 3, type);
        if (jr_bool(rand)) next_piece(parent, s, rand, p->bb.minX + 1, p->bb.minY + 4, p->bb.maxZ + 1, 0, type);
    }
}

static void room_build(struct piece *p, struct piece *parent, struct start *s, jrand *rand)
{
    int type = p->component_type;
    int span = bbox_ysize(&p->bb) - 3 - 1;   /* getYSize() - 3 - 1 */
    if (span <= 0) span = 1;

    int i;
    for (i = 0; i < bbox_xsize(&p->bb); i += 4)
    {
        i += jr_int_n(rand, bbox_xsize(&p->bb));
        if (i + 3 > bbox_xsize(&p->bb)) break;
        int y = p->bb.minY + jr_int_n(rand, span) + 1;
        struct piece *c = next_piece(parent, s, rand, p->bb.minX + i, y, p->bb.minZ - 1, 2, type);
        if (c) room_add_entrance(p, bbox_make(c->bb.minX, c->bb.minY, p->bb.minZ, c->bb.maxX, c->bb.maxY, p->bb.minZ + 1));
    }

    for (i = 0; i < bbox_xsize(&p->bb); i += 4)
    {
        i += jr_int_n(rand, bbox_xsize(&p->bb));
        if (i + 3 > bbox_xsize(&p->bb)) break;
        int y = p->bb.minY + jr_int_n(rand, span) + 1;
        struct piece *c = next_piece(parent, s, rand, p->bb.minX + i, y, p->bb.maxZ + 1, 0, type);
        if (c) room_add_entrance(p, bbox_make(c->bb.minX, c->bb.minY, p->bb.maxZ - 1, c->bb.maxX, c->bb.maxY, p->bb.maxZ));
    }

    for (i = 0; i < bbox_zsize(&p->bb); i += 4)
    {
        i += jr_int_n(rand, bbox_zsize(&p->bb));
        if (i + 3 > bbox_zsize(&p->bb)) break;
        int y = p->bb.minY + jr_int_n(rand, span) + 1;
        struct piece *c = next_piece(parent, s, rand, p->bb.minX - 1, y, p->bb.minZ + i, 1, type);
        if (c) room_add_entrance(p, bbox_make(p->bb.minX, c->bb.minY, c->bb.minZ, p->bb.minX + 1, c->bb.maxY, c->bb.maxZ));
    }

    for (i = 0; i < bbox_zsize(&p->bb); i += 4)
    {
        i += jr_int_n(rand, bbox_zsize(&p->bb));
        if (i + 3 > bbox_zsize(&p->bb)) break;
        int y = p->bb.minY + jr_int_n(rand, span) + 1;
        struct piece *c = next_piece(parent, s, rand, p->bb.maxX + 1, y, p->bb.minZ + i, 3, type);
        if (c) room_add_entrance(p, bbox_make(p->bb.maxX - 1, c->bb.minY, c->bb.minZ, p->bb.maxX, c->bb.maxY, c->bb.maxZ));
    }
}

static void stairs_build(struct piece *p, struct piece *parent, struct start *s, jrand *rand)
{
    int type = p->component_type;

    switch (p->coord_base_mode)
    {
        case 0: next_piece(parent, s, rand, p->bb.minX, p->bb.minY, p->bb.maxZ + 1, 0, type); break;
        case 1: next_piece(parent, s, rand, p->bb.minX - 1, p->bb.minY, p->bb.minZ, 1, type); break;
        case 2: next_piece(parent, s, rand, p->bb.minX, p->bb.minY, p->bb.minZ - 1, 2, type); break;
        case 3: next_piece(parent, s, rand, p->bb.maxX + 1, p->bb.minY, p->bb.minZ, 3, type); break;
        default: break;
    }
}

void mineshaft_build_component(struct piece *p, struct piece *parent, struct start *s, jrand *rand)
{
    switch (p->kind)
    {
        case PIECE_CORRIDOR: corridor_build(p, parent, s, rand); break;
        case PIECE_CROSSING: crossing_build(p, parent, s, rand); break;
        case PIECE_ROOM: room_build(p, parent, s, rand); break;
        case PIECE_STAIRS: stairs_build(p, parent, s, rand); break;
        case PIECE_STRONGHOLD: break;   /* the stronghold builds its own tree */
        default: break;
    }
}

/* ---- MapGenMineshaft ---- */

static int mineshaft_can_spawn(jrand *rand, int cx, int cz)
{
    if (!(jr_double(rand) < MINESHAFT_CHANCE)) return 0;
    int nearest = abs_int(cx) > abs_int(cz) ? abs_int(cx) : abs_int(cz);
    return jr_int_n(rand, 80) < nearest;
}

static void mineshaft_make_start(jrand *rand, int cx, int cz, struct start *s)
{
    struct piece *room = room_new(0, rand, (cx << 4) + 2, (cz << 4) + 2);
    start_add(s, room);
    piece_build_component(room, room, s, rand);
    start_update_bb(s);
    start_mark_available_height(s, rand, 10);
}

const struct structure_type structure_mineshaft = {
    "Mineshaft",
    NULL,
    mineshaft_can_spawn,
    mineshaft_make_start
};