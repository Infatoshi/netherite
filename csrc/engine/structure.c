#include "structure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fortress.h"
#include "mineshaft.h"
#include "temple.h"
#include "village.h"
#include "stronghold.h"

/* Every ported structure type. A new type adds one line here and one file
 * beside mineshaft.c; see the seam note in structure.h. */
const struct structure_type *const structure_types[] = {
    &structure_mineshaft,
    &structure_temple,
    &structure_village,
    &structure_stronghold,
    &structure_fortress,
    NULL
};

/* ---- StructureBoundingBox ---- */

struct bbox bbox_make(int minX, int minY, int minZ, int maxX, int maxY, int maxZ)
{
    struct bbox b;
    b.minX = minX;
    b.minY = minY;
    b.minZ = minZ;
    b.maxX = maxX;
    b.maxY = maxY;
    b.maxZ = maxZ;
    return b;
}

struct bbox bbox_new_empty(void)
{
    return bbox_make(0x7fffffff, 0x7fffffff, 0x7fffffff, (int)0x80000000, (int)0x80000000, (int)0x80000000);
}

void bbox_expand_to(struct bbox *b, const struct bbox *o)
{
    if (o->minX < b->minX) b->minX = o->minX;
    if (o->minY < b->minY) b->minY = o->minY;
    if (o->minZ < b->minZ) b->minZ = o->minZ;
    if (o->maxX > b->maxX) b->maxX = o->maxX;
    if (o->maxY > b->maxY) b->maxY = o->maxY;
    if (o->maxZ > b->maxZ) b->maxZ = o->maxZ;
}

void bbox_offset(struct bbox *b, int dx, int dy, int dz)
{
    b->minX += dx;
    b->minY += dy;
    b->minZ += dz;
    b->maxX += dx;
    b->maxY += dy;
    b->maxZ += dz;
}

int bbox_intersects(const struct bbox *a, const struct bbox *b)
{
    return a->maxX >= b->minX && a->minX <= b->maxX
        && a->maxZ >= b->minZ && a->minZ <= b->maxZ
        && a->maxY >= b->minY && a->minY <= b->maxY;
}

int bbox_xsize(const struct bbox *b) { return b->maxX - b->minX + 1; }
int bbox_ysize(const struct bbox *b) { return b->maxY - b->minY + 1; }
int bbox_zsize(const struct bbox *b) { return b->maxZ - b->minZ + 1; }

struct bbox bbox_component_to_add(int x, int y, int z, int offX, int offY, int offZ,
                                  int sizeX, int sizeY, int sizeZ, int mode)
{
    switch (mode)
    {
        case 0:
            return bbox_make(x + offX, y + offY, z + offZ,
                             x + sizeX - 1 + offX, y + sizeY - 1 + offY, z + sizeZ - 1 + offZ);
        case 1:
            return bbox_make(x - sizeZ + 1 + offZ, y + offY, z + offX,
                             x + offZ, y + sizeY - 1 + offY, z + sizeX - 1 + offX);
        case 2:
            return bbox_make(x + offX, y + offY, z - sizeZ + 1 + offZ,
                             x + sizeX - 1 + offX, y + sizeY - 1 + offY, z + offZ);
        case 3:
            return bbox_make(x + offZ, y + offY, z + offX,
                             x + sizeZ - 1 + offZ, y + sizeY - 1 + offY, z + sizeX - 1 + offX);
        default:
            return bbox_make(x + offX, y + offY, z + offZ,
                             x + sizeX - 1 + offX, y + sizeY - 1 + offY, z + sizeZ - 1 + offZ);
    }
}

/* ---- starts ---- */

void start_init(struct start *s, const char *id, int cx, int cz)
{
    s->id = id;
    s->chunk_x = cx;
    s->chunk_z = cz;
    s->bb = bbox_make(0, 0, 0, 0, 0, 0);
    s->pieces = NULL;
    s->n = 0;
    s->cap = 0;
}

void start_add(struct start *s, struct piece *p)
{
    if (s->n == s->cap)
    {
        s->cap = s->cap ? 2 * s->cap : 16;
        s->pieces = realloc(s->pieces, (size_t)s->cap * sizeof *s->pieces);
        if (!s->pieces) abort();
    }
    s->pieces[s->n++] = p;
}

void start_update_bb(struct start *s)
{
    s->bb = bbox_new_empty();
    for (int i = 0; i < s->n; ++i) bbox_expand_to(&s->bb, &s->pieces[i]->bb);
}

/* markAvailableHeight: lift the whole start so its floor sits at 63 - k. */
void start_mark_available_height(struct start *s, jrand *rand, int k)
{
    int span = 63 - k;
    int height = bbox_ysize(&s->bb) + 1;
    if (height < span) height += jr_int_n(rand, span - height);
    int dy = height - s->bb.maxY;
    bbox_offset(&s->bb, 0, dy, 0);
    for (int i = 0; i < s->n; ++i) bbox_offset(&s->pieces[i]->bb, 0, dy, 0);
}

/* setRandomHeight: settle the whole start's floor between lo and hi instead of
 * at a fixed level. */
void start_set_random_height(struct start *s, jrand *rand, int lo, int hi)
{
    int span = hi - lo + 1 - bbox_ysize(&s->bb);
    int y = span > 1 ? lo + jr_int_n(rand, span) : lo;
    int dy = y - s->bb.minY;
    bbox_offset(&s->bb, 0, dy, 0);
    for (int i = 0; i < s->n; ++i) bbox_offset(&s->pieces[i]->bb, 0, dy, 0);
}

struct piece *start_find_intersecting(const struct start *s, const struct bbox *b)
{
    for (int i = 0; i < s->n; ++i)
        if (bbox_intersects(&s->pieces[i]->bb, b)) return s->pieces[i];
    return NULL;
}

void start_free(struct start *s)
{
    for (int i = 0; i < s->n; ++i)
    {
        if (s->pieces[i]->kind == PIECE_ROOM) free(s->pieces[i]->u.room.v);
        if (s->pieces[i]->kind == PIECE_VILLAGE)
        {
            free(s->pieces[i]->u.village.weights);
            free(s->pieces[i]->u.village.pending_house.v);
            free(s->pieces[i]->u.village.pending_road.v);
        }
        free(s->pieces[i]->owned);
        if (s->pieces[i]->kind == PIECE_FORTRESS) free(s->pieces[i]->u.fortress.pending.v);
        free(s->pieces[i]);
    }
    free(s->pieces);
    s->pieces = NULL;
    s->n = s->cap = 0;
}

static nbt *bbox_nbt(const struct bbox *b)
{
    int v[6];
    v[0] = b->minX;
    v[1] = b->minY;
    v[2] = b->minZ;
    v[3] = b->maxX;
    v[4] = b->maxY;
    v[5] = b->maxZ;
    return nbt_new_int_array(v, 6);
}

nbt *piece_nbt(const struct piece *p)
{
    nbt *c = nbt_new_compound();
    nbt_put(c, "id", nbt_new_string(p->id));
    nbt_put(c, "BB", bbox_nbt(&p->bb));
    nbt_put(c, "O", nbt_new_int(p->coord_base_mode));
    nbt_put(c, "GD", nbt_new_int(p->component_type));
    switch (p->kind)
    {
        case PIECE_CORRIDOR:
            nbt_put(c, "hr", nbt_new_byte(p->u.corridor.has_rails));
            nbt_put(c, "sc", nbt_new_byte(p->u.corridor.has_spiders));
            nbt_put(c, "hps", nbt_new_byte(p->u.corridor.spawner_placed));
            nbt_put(c, "Num", nbt_new_int(p->u.corridor.section_count));
            break;
        case PIECE_CROSSING:
            nbt_put(c, "tf", nbt_new_byte(p->u.crossing.multiple_floors));
            nbt_put(c, "D", nbt_new_int(p->u.crossing.corridor_direction));
            break;
        case PIECE_ROOM:
        {
            nbt *list = nbt_new_list();
            for (int i = 0; i < p->u.room.n; ++i) nbt_list_add(list, bbox_nbt(&p->u.room.v[i]));
            nbt_put(c, "Entrances", list);
            break;
        }
        case PIECE_STAIRS:
            break;
        case PIECE_FORTRESS:
        {
            /* Piece.func_143012_a is the base class's empty body; only the two
             * corridors, the dead end and the throne add a key. */
            switch (p->u.fortress.kind)
            {
                case F_CORRIDOR:
                case F_CORRIDOR2:
                    nbt_put(c, "Chest", nbt_new_byte(p->u.fortress.chest));
                    break;
                case F_END:
                    nbt_put(c, "Seed", nbt_new_int(p->u.fortress.fill_seed));
                    break;
                case F_THRONE:
                    nbt_put(c, "Mob", nbt_new_byte(p->u.fortress.has_spawner));
                    break;
                default:
                    break;
            }
            break;
        }
        case PIECE_TEMPLE:
        {
            /* ComponentScatteredFeaturePieces.Feature.func_143012_a, then the
             * subclass's own keys. */
            nbt_put(c, "Width", nbt_new_int(p->u.temple.size_x));
            nbt_put(c, "Height", nbt_new_int(p->u.temple.size_y));
            nbt_put(c, "Depth", nbt_new_int(p->u.temple.size_z));
            nbt_put(c, "HPos", nbt_new_int(p->u.temple.hpos));
            switch (p->u.temple.kind)
            {
                case TEMPLE_DESERT_PYRAMID:
                    for (int i = 0; i < 4; ++i)
                    {
                        char key[20];
                        snprintf(key, sizeof key, "hasPlacedChest%d", i);
                        nbt_put(c, key, nbt_new_byte(p->u.temple.v.desert.has_placed_chest[i]));
                    }
                    break;
                case TEMPLE_JUNGLE_PYRAMID:
                    nbt_put(c, "placedMainChest", nbt_new_byte(p->u.temple.v.jungle.placed_main_chest));
                    nbt_put(c, "placedHiddenChest", nbt_new_byte(p->u.temple.v.jungle.placed_hidden_chest));
                    nbt_put(c, "placedTrap1", nbt_new_byte(p->u.temple.v.jungle.placed_trap1));
                    nbt_put(c, "placedTrap2", nbt_new_byte(p->u.temple.v.jungle.placed_trap2));
                    break;
                case TEMPLE_SWAMP_HUT:
                    nbt_put(c, "Witch", nbt_new_byte(p->u.temple.v.swamp.has_witch));
                    break;
            }
            break;
        }
        case PIECE_VILLAGE:
        {
            /* StructureVillagePieces.Village.func_143012_a, then the subclass's
             * own keys. HPos and VCount start -1 and 0. Desert is
             * Village.field_143014_b: Start.inDesert for every piece built
             * through the Start, but false for the well itself, which was
             * constructed with a null Start. */
            nbt_put(c, "HPos", nbt_new_int(p->u.village.hpos));
            nbt_put(c, "VCount", nbt_new_int(p->u.village.vcount));
            nbt_put(c, "Desert", nbt_new_byte(p->u.village.desert));
            switch (p->u.village.kind)
            {
                case V_ROAD:
                    nbt_put(c, "Length", nbt_new_int(p->u.village.length));
                    break;
                case V_HOUSE4:
                    nbt_put(c, "Terrace", nbt_new_byte(p->u.village.terrace));
                    break;
                case V_WOODHUT:
                    nbt_put(c, "T", nbt_new_int(p->u.village.table));
                    nbt_put(c, "C", nbt_new_byte(p->u.village.tall_house));
                    break;
                case V_HOUSE2:
                    nbt_put(c, "Chest", nbt_new_byte(p->u.village.chest));
                    break;
                case V_FIELD1:
                    nbt_put(c, "CA", nbt_new_int(p->u.village.ca));
                    nbt_put(c, "CB", nbt_new_int(p->u.village.cb));
                    nbt_put(c, "CC", nbt_new_int(p->u.village.cc));
                    nbt_put(c, "CD", nbt_new_int(p->u.village.cd));
                    break;
                case V_FIELD2:
                    nbt_put(c, "CA", nbt_new_int(p->u.village.ca));
                    nbt_put(c, "CB", nbt_new_int(p->u.village.cb));
                    break;
                default:
                    break;
            }
            break;
        }
        case PIECE_STRONGHOLD:
            stronghold_piece_nbt(c, p);
            break;
    }
    return c;
}

nbt *start_nbt(const struct start *s)
{
    nbt *c = nbt_new_compound();
    nbt_put(c, "id", nbt_new_string(s->id));
    nbt_put(c, "ChunkX", nbt_new_int(s->chunk_x));
    nbt_put(c, "ChunkZ", nbt_new_int(s->chunk_z));
    nbt_put(c, "BB", bbox_nbt(&s->bb));
    nbt *children = nbt_new_list();
    for (int i = 0; i < s->n; ++i) nbt_list_add(children, piece_nbt(s->pieces[i]));
    nbt_put(c, "Children", children);
    /* MapGenVillage.Start.func_143022_a: the extra "Valid" flag, true when more
     * than two components are not roads. The well carries it (it is the piece
     * the start is built around); a start of another type has none. */
    if (s->n > 0 && s->pieces[0]->kind == PIECE_VILLAGE)
        nbt_put(c, "Valid", nbt_new_byte(s->pieces[0]->u.village.valid));
    return c;
}

void piece_build_component(struct piece *p, struct piece *parent, struct start *s, jrand *rand)
{
    /* One line per piece family: the shared dispatcher lives here so a new
     * type only exports its own body. */
    switch (p->kind)
    {
        case PIECE_CORRIDOR:
        case PIECE_CROSSING:
        case PIECE_ROOM:
        case PIECE_STAIRS:
            mineshaft_build_component(p, parent, s, rand);
            break;
        /* A temple component is one piece: StructureComponent.buildComponent's
         * default empty body, its subclasses never override it. */
        case PIECE_TEMPLE: break;
        case PIECE_VILLAGE: village_build_component(p, parent, s, rand); break;
        /* The stronghold builds its whole tree inside its start (stronghold.c). */
        case PIECE_STRONGHOLD: break;
        case PIECE_FORTRESS: fortress_build_component(p, parent, s, rand); break;
    }
}

const struct structure_type *structure_type_by_name(const char *name)
{
    for (int i = 0; structure_types[i]; ++i)
        if (!strcmp(structure_types[i]->name, name)) return structure_types[i];
    return NULL;
}

/* ---- the candidate walk ---- */

static int cmp_start(const void *pa, const void *pb)
{
    const struct start *a = *(const struct start *const *)pa;
    const struct start *b = *(const struct start *const *)pb;
    if (a->chunk_x != b->chunk_x) return a->chunk_x < b->chunk_x ? -1 : 1;
    if (a->chunk_z != b->chunk_z) return a->chunk_z < b->chunk_z ? -1 : 1;
    return 0;
}

static void start_list_add(struct start_list *l, struct start *s)
{
    if (l->n == l->cap)
    {
        l->cap = l->cap ? 2 * l->cap : 16;
        l->v = realloc(l->v, (size_t)l->cap * sizeof *l->v);
        if (!l->v) abort();
    }
    l->v[l->n++] = s;
}

void start_list_free(struct start_list *l)
{
    for (int i = 0; i < l->n; ++i) { start_free(l->v[i]); free(l->v[i]); }
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

void structure_walk(const struct structure_type *t, int64_t seed,
                    int x0, int z0, int x1, int z1, struct start_list *out)
{
    jrand rand;
    /* MapGenBase.func_151539_a: one Random for the type, seeded from the world
     * seed to draw the two placement longs; the seed is the same for every
     * offered chunk, so the two longs are constants of the walk. */
    jr_seed(&rand, seed);
    int64_t mulX = jr_long(&rand);
    int64_t mulZ = jr_long(&rand);
    if (t->begin) t->begin(seed);

    const int range = 8;   /* MapGenBase.range */

    for (int cx = x0 - range; cx <= x1 + range; ++cx)
        for (int cz = z0 - range; cz <= z1 + range; ++cz)
        {
            int64_t kx = (int64_t)((uint64_t)(int64_t)cx * (uint64_t)mulX);
            int64_t kz = (int64_t)((uint64_t)(int64_t)cz * (uint64_t)mulZ);
            jr_seed(&rand, kx ^ kz ^ seed);
            jr_int(&rand);                          /* MapGenStructure.func_151538_a */
            if (!t->can_spawn(&rand, cx, cz)) continue;
            struct start *s = malloc(sizeof *s);
            if (!s) abort();
            start_init(s, t->name, cx, cz);
            t->make_start(&rand, cx, cz, s);
            start_list_add(out, s);
        }

    qsort(out->v, (size_t)out->n, sizeof *out->v, cmp_start);
}