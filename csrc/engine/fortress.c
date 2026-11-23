/* MapGenNetherBridge and StructureNetherBridgePieces. Every Random draw is in
 * Java's order: the spawn test re-seeds the generator's Random from the
 * candidate's 16x16 chunk region and the world seed (so the walk's per-candidate
 * stream is discarded), and the start's construction draws the start piece's
 * coordBaseMode, then the weighted piece picker, then setRandomHeight. The
 * pieces are boxes and flags only; the blocks (nether brick, the blaze spawner,
 * the nether wart) are placed later, during population.
 *
 * The picker's state is per start: the two PieceWeight lists, the entry used
 * last, and the pending queue field_74967_d, exactly as Java keeps them on the
 * Start piece. Java's static PieceWeight table is shared, but Start's
 * constructor resets every counter, so a copy per start is equivalent. */
#include "fortress.h"
#include "env.h"

#include <stdlib.h>
#include <math.h>

#include "jrand.h"

/* MapGenNetherBridge.canSpawnStructureAtCoords seeds its own Random from the
 * region and the world seed, so the spawn test needs the world seed: the walk
 * hands it over once, in begin(). */
#define fortress_seed (neth_env->fortress.seed)

static void fortress_make_start(jrand *rand, int cx, int cz, struct start *s);

void fortress_begin(int64_t seed)
{
    fortress_seed = seed;
}

/* canSpawnStructureAtCoords: a region holds one candidate chunk when nextInt(3) is zero. Re-seed for each region, as
 * Java does for each tested chunk; leave rand after the exact draws so getStructureStart can continue from it. */
static int fortress_region_candidate(jrand *rand, int64_t seed, int rx, int rz, int *cx, int *cz)
{
    jr_seed(rand, (int64_t)(int32_t)((uint32_t)rx ^ ((uint32_t)rz << 4)) ^ seed);
    jr_int(rand);
    if (jr_int_n(rand, 3) != 0) return 0;
    *cx = rx * 16 + 4 + jr_int_n(rand, 8);
    *cz = rz * 16 + 4 + jr_int_n(rand, 8);
    return 1;
}

static int fortress_can_spawn(jrand *rand, int cx, int cz)
{
    int rx = cx >> 4, rz = cz >> 4;
    jr_seed(rand, (int64_t)(int32_t)((uint32_t)rx ^ ((uint32_t)rz << 4)) ^ fortress_seed);
    jr_int(rand);
    if (jr_int_n(rand, 3) != 0) return 0;
    int want_x = rx * 16 + 4 + jr_int_n(rand, 8);
    if (cx != want_x) return 0;
    int want_z = rz * 16 + 4 + jr_int_n(rand, 8);
    return cz == want_z;
}

/* A privileged locator for the Nether search: the nearest start chunk without loading or generating one. The same
 * region draw as the vanilla spawn test above means a loaded fortress map and this guess name the same candidate. */
int fortress_locate_start(int64_t seed, int bx, int bz, int radius, int *chunk_x, int *chunk_z)
{
    if (chunk_x == NULL || chunk_z == NULL || radius < 0 || radius > 32) return 0;
    int rx0 = bx >> 8, rz0 = bz >> 8, found = 0, best_x = 0, best_z = 0;
    int64_t best_d2 = 0;
    for (int rx = rx0 - radius; rx <= rx0 + radius; ++rx)
        for (int rz = rz0 - radius; rz <= rz0 + radius; ++rz)
        {
            jrand rand;
            int cx, cz;
            if (!fortress_region_candidate(&rand, seed, rx, rz, &cx, &cz)) continue;
            int64_t dx = (int64_t)cx * 16 + 8 - bx, dz = (int64_t)cz * 16 + 8 - bz;
            int64_t d2 = dx * dx + dz * dz;
            if (!found || d2 < best_d2) found = 1, best_d2 = d2, best_x = cx, best_z = cz;
        }
    if (found) *chunk_x = best_x, *chunk_z = best_z;
    return found;
}

/* A start's centre can be far from its blaze room. Build only the piece boxes, as Java would, and choose the nearest
 * throne centre. Distance is a cheap travel proxy; the tunnel planner checks actual cells as they become loaded. */
int fortress_locate_throne(int64_t seed, int bx, int bz, int range, int target[2], int start_chunk[2])
{
    if (target == NULL || start_chunk == NULL || range < 0 || range > 2048) return 0;
    int rx0 = (bx - range) >> 8, rx1 = (bx + range) >> 8;
    int rz0 = (bz - range) >> 8, rz1 = (bz + range) >> 8;
    int found = 0, best_x = 0, best_z = 0, best_cx = 0, best_cz = 0;
    int64_t best_d2 = 0;
    for (int rx = rx0; rx <= rx1; ++rx)
        for (int rz = rz0; rz <= rz1; ++rz)
        {
            jrand rand;
            int cx, cz;
            if (!fortress_region_candidate(&rand, seed, rx, rz, &cx, &cz)) continue;
            int64_t sx = (int64_t)cx * 16 + 8 - bx, sz = (int64_t)cz * 16 + 8 - bz;
            if (sx * sx + sz * sz > (int64_t)range * range) continue;
            struct start s;
            start_init(&s, "Fortress", cx, cz);
            fortress_make_start(&rand, cx, cz, &s);
            for (int i = 0; i < s.n; ++i)
            {
                const struct piece *p = s.pieces[i];
                if (p->u.fortress.kind != F_THRONE) continue;
                int x = (p->bb.minX + p->bb.maxX) / 2, z = (p->bb.minZ + p->bb.maxZ) / 2;
                int64_t dx = (int64_t)x - bx, dz = (int64_t)z - bz, d2 = dx * dx + dz * dz;
                if (!found || d2 < best_d2)
                    found = 1, best_d2 = d2, best_x = x, best_z = z, best_cx = cx, best_cz = cz;
            }
            start_free(&s);
        }
    if (!found) return 0;
    target[0] = best_x, target[1] = best_z;
    start_chunk[0] = best_cx, start_chunk[1] = best_cz;
    return 1;
}

int fortress_locate_entry(int64_t seed, int bx, int by, int bz, int range, int target[3], int *kind)
{
    if (target == NULL || kind == NULL || range < 0 || range > 2048) return 0;
    int found = 0, best[3] = {0}, best_kind = 0;
    int64_t best_cost = 0;
    for (int rx = (bx - range) >> 8; rx <= (bx + range) >> 8; ++rx)
        for (int rz = (bz - range) >> 8; rz <= (bz + range) >> 8; ++rz)
        {
            jrand rand;
            int cx, cz;
            if (!fortress_region_candidate(&rand, seed, rx, rz, &cx, &cz)) continue;
            int64_t sx = (int64_t)cx * 16 + 8 - bx, sz = (int64_t)cz * 16 + 8 - bz;
            if (sx * sx + sz * sz > (int64_t)range * range) continue;
            struct start s;
            start_init(&s, "Fortress", cx, cz);
            fortress_make_start(&rand, cx, cz, &s);
            for (int i = 0; i < s.n; ++i)
            {
                const struct piece *p = s.pieces[i];
                int k = p->u.fortress.kind;
                if (k != F_CORRIDOR && k != F_CORRIDOR2 && k != F_CORRIDOR5 && k != F_CROSSING &&
                    k != F_CROSSING2)
                    continue;
                int x = (p->bb.minX + p->bb.maxX) / 2, y = p->bb.minY + 2, z = (p->bb.minZ + p->bb.maxZ) / 2;
                int64_t dx = (int64_t)x - bx, dy = (int64_t)y - by, dz = (int64_t)z - bz;
                int64_t cost = dx * dx + 4 * dy * dy + dz * dz;
                if (!found || cost < best_cost)
                {
                    found = 1, best_cost = cost, best[0] = x, best[1] = y, best[2] = z, best_kind = k;
                }
            }
            start_free(&s);
        }
    if (!found) return 0;
    for (int i = 0; i < 3; ++i) target[i] = best[i];
    *kind = best_kind;
    return 1;
}

/* ---- the pieces ---- */

/* getComponentToAddBoundingBox's origin offset and size box, one entry per
 * piece (the start has no shape: it is not built through createValidComponent). */
struct shape { int ox, oy, oz, size_x, size_y, size_z; };

static const struct shape shapes[] = {
    [F_STRAIGHT]     = {-1, -3, 0,  5, 10, 19},
    [F_CROSSING3]    = {-8, -3, 0, 19, 10, 19},
    [F_CROSSING]     = {-2,  0, 0,  7,  9,  7},
    [F_STAIRS]       = {-2,  0, 0,  7, 11,  7},
    [F_THRONE]       = {-2,  0, 0,  7,  8,  9},
    [F_ENTRANCE]     = {-5, -3, 0, 13, 14, 13},
    [F_CORRIDOR]     = {-1,  0, 0,  5,  7,  5},
    [F_CORRIDOR2]    = {-1,  0, 0,  5,  7,  5},
    [F_CORRIDOR3]    = {-1, -7, 0,  5, 14, 10},
    [F_CORRIDOR4]    = {-3,  0, 0,  9,  7,  9},
    [F_CORRIDOR5]    = {-1,  0, 0,  5,  7,  5},
    [F_CROSSING2]    = {-1,  0, 0,  5,  7,  5},
    [F_NETHER_STALK] = {-5, -3, 0, 13, 14, 13},
    [F_END]          = {-1, -3, 0,  5, 10,  8},
};

/* func_143049_a's registry names, the "id" of each piece's NBT. */
static const char *const ids[] = {
    [F_START]        = "NeStart",
    [F_STRAIGHT]     = "NeBS",
    [F_CORRIDOR]     = "NeSCLT",
    [F_CORRIDOR2]    = "NeSCRT",
    [F_CORRIDOR3]    = "NeCCS",
    [F_CORRIDOR4]    = "NeCTB",
    [F_CORRIDOR5]    = "NeSC",
    [F_CROSSING]     = "NeRC",
    [F_CROSSING2]    = "NeSCSC",
    [F_CROSSING3]    = "NeBCr",
    [F_END]          = "NeBEF",
    [F_ENTRANCE]     = "NeCE",
    [F_NETHER_STALK] = "NeCSR",
    [F_STAIRS]       = "NeSR",
    [F_THRONE]       = "NeMT",
};

/* One createValidComponent: place the box, refuse it when it sits too low or
 * runs into a piece already placed, then build the piece. The two corridors
 * and the dead end draw in their constructors, after the box test. `type` is
 * the component type the caller passes on, Java's recursion depth. */
static struct piece *fortress_create(enum fortress_kind kind, struct start *s, jrand *rand,
                                     int x, int y, int z, int mode, int type)
{
    const struct shape *sh = &shapes[kind];
    struct bbox bb = bbox_component_to_add(x, y, z, sh->ox, sh->oy, sh->oz,
                                           sh->size_x, sh->size_y, sh->size_z, mode);

    if (bb.minY <= 10 || start_find_intersecting(s, &bb) != NULL) return NULL;

    struct piece *p = calloc(1, sizeof *p);
    if (!p) abort();
    p->id = ids[kind];
    p->kind = PIECE_FORTRESS;
    p->bb = bb;
    p->coord_base_mode = mode;
    p->component_type = type;
    p->u.fortress.kind = kind;

    if (kind == F_CORRIDOR || kind == F_CORRIDOR2) p->u.fortress.chest = jr_int_n(rand, 3) == 0;
    if (kind == F_END) p->u.fortress.fill_seed = jr_int(rand);
    return p;
}

/* End.func_74971_a. It is built after the picker gives up, or when the new
 * piece would lie outside the start's 112 block reach; when it does not fit,
 * it is dropped, its nextInt() already spent. */
static struct piece *fortress_end(struct start *s, jrand *rand, int x, int y, int z, int mode, int type)
{
    return fortress_create(F_END, s, rand, x, y, z, mode, type);
}

/* ---- the weighted picker ---- */

/* primaryComponents: the pieces off a crossing or a straight. */
static const struct fortress_weight primary_weights[6] = {
    {F_STRAIGHT, 30, 0, 0, 1}, {F_CROSSING3, 10, 0, 4, 0}, {F_CROSSING, 10, 0, 4, 0},
    {F_STAIRS, 10, 0, 3, 0}, {F_THRONE, 5, 0, 2, 0}, {F_ENTRANCE, 5, 0, 1, 0}
};

/* secondaryComponents: the pieces off a corridor or an entrance. */
static const struct fortress_weight secondary_weights[7] = {
    {F_CORRIDOR5, 25, 0, 0, 1}, {F_CROSSING2, 15, 0, 5, 0}, {F_CORRIDOR2, 5, 0, 10, 0},
    {F_CORRIDOR, 5, 0, 10, 0}, {F_CORRIDOR3, 10, 0, 3, 1}, {F_CORRIDOR4, 7, 0, 2, 0},
    {F_NETHER_STALK, 5, 0, 2, 0}
};

static void pending_add(struct piece_list *q, struct piece *p)
{
    if (q->n == q->cap)
    {
        q->cap = q->cap ? 2 * q->cap : 16;
        q->v = realloc(q->v, (size_t)q->cap * sizeof *q->v);
        if (!q->v) abort();
    }
    q->v[q->n++] = p;
}

/* getTotalWeight: the sum of the list's weights while any entry still has
 * room, -1 once every entry with a limit is exhausted. */
static int weight_total(struct fortress_weight *const *list, int n)
{
    int sum = 0, any = 0;
    for (int i = 0; i < n; ++i)
    {
        if (list[i]->limit > 0 && list[i]->spawned < list[i]->limit) any = 1;
        sum += list[i]->weight;
    }
    return any ? sum : -1;
}

/* func_78822_a / func_78823_a: an entry with no limit never runs out. */
static int weight_has_room(const struct fortress_weight *w)
{
    return w->limit == 0 || w->spawned < w->limit;
}

/* getNextComponent(start, weights, components, rand, ...): at most five picks
 * by weight; an entry whose counter is full, or the entry used last when it
 * may not repeat, ends the pick and the next round draws again; an entry whose
 * piece does not fit lets the current pick continue with the remaining ones.
 * When a piece is built its counter rises, it becomes the entry used last and,
 * once full, leaves the list. The fallback is a dead end. */
static struct piece *get_next_component(struct piece *start_piece, struct start *s, jrand *rand,
                                        int x, int y, int z, int mode, int depth, int secondary)
{
    struct fortress_weight **list = secondary ? start_piece->u.fortress.secondary
                                              : start_piece->u.fortress.primary;
    int *n = secondary ? &start_piece->u.fortress.nsecondary : &start_piece->u.fortress.nprimary;
    int total = weight_total(list, *n);
    int go = total > 0 && depth <= 30;
    int tries = 0;

    while (tries < 5 && go)
    {
        ++tries;
        int roll = jr_int_n(rand, total);

        for (int i = 0; i < *n; ++i)
        {
            struct fortress_weight *w = list[i];
            roll -= w->weight;
            if (roll >= 0) continue;
            if (!weight_has_room(w) || (w == start_piece->u.fortress.last && !w->repeat)) break;

            struct piece *p = fortress_create(w->kind, s, rand, x, y, z, mode, depth);

            if (p != NULL)
            {
                ++w->spawned;
                start_piece->u.fortress.last = w;
                if (!weight_has_room(w))
                {
                    for (int j = i; j + 1 < *n; ++j) list[j] = list[j + 1];
                    --*n;
                }
                return p;
            }
        }
    }
    return fortress_end(s, rand, x, y, z, mode, depth);
}

/* getNextComponent(start, components, rand, ...), the bounds wrapper: a new
 * piece more than 112 blocks from the start's own box is replaced by a dead
 * end, which is dropped rather than added. Inside the box the piece (or the
 * dead end the picker fell back to) joins the start and the pending queue. */
static void fortress_next(struct piece *p, struct piece *start_piece, struct start *s, jrand *rand,
                          int x, int y, int z, int mode, int secondary)
{
    if (abs(x - start_piece->bb.minX) <= 112 && abs(z - start_piece->bb.minZ) <= 112)
    {
        struct piece *c = get_next_component(start_piece, s, rand, x, y, z, mode,
                                             p->component_type + 1, secondary);
        if (c != NULL)
        {
            start_add(s, c);
            pending_add(&start_piece->u.fortress.pending, c);
        }
    }
    else
    {
        fortress_end(s, rand, x, y, z, mode, p->component_type);
    }
}

/* getNextComponentNormal: along the piece's facing. o4 shifts the same axis,
 * o5 the height. */
static void next_normal(struct piece *p, struct piece *start_piece, struct start *s, jrand *rand,
                        int o4, int o5, int secondary)
{
    switch (p->coord_base_mode)
    {
        case 0:
            fortress_next(p, start_piece, s, rand, p->bb.minX + o4, p->bb.minY + o5, p->bb.maxZ + 1,
                          p->coord_base_mode, secondary);
            break;
        case 1:
            fortress_next(p, start_piece, s, rand, p->bb.minX - 1, p->bb.minY + o5, p->bb.minZ + o4,
                          p->coord_base_mode, secondary);
            break;
        case 2:
            fortress_next(p, start_piece, s, rand, p->bb.minX + o4, p->bb.minY + o5, p->bb.minZ - 1,
                          p->coord_base_mode, secondary);
            break;
        case 3:
            fortress_next(p, start_piece, s, rand, p->bb.maxX + 1, p->bb.minY + o5, p->bb.minZ + o4,
                          p->coord_base_mode, secondary);
            break;
        default:
            break;
    }
}

/* getNextComponentX: across the piece, which turns the new piece to mode 1 or
 * 2. o4 is the height, o5 the same axis. */
static void next_x(struct piece *p, struct piece *start_piece, struct start *s, jrand *rand,
                   int o4, int o5, int secondary)
{
    switch (p->coord_base_mode)
    {
        case 0:
        case 2:
            fortress_next(p, start_piece, s, rand, p->bb.minX - 1, p->bb.minY + o4, p->bb.minZ + o5,
                          1, secondary);
            break;
        case 1:
        case 3:
            fortress_next(p, start_piece, s, rand, p->bb.minX + o5, p->bb.minY + o4, p->bb.minZ - 1,
                          2, secondary);
            break;
        default:
            break;
    }
}

/* getNextComponentZ: across the piece the other way, mode 3 or 0. */
static void next_z(struct piece *p, struct piece *start_piece, struct start *s, jrand *rand,
                   int o4, int o5, int secondary)
{
    switch (p->coord_base_mode)
    {
        case 0:
        case 2:
            fortress_next(p, start_piece, s, rand, p->bb.maxX + 1, p->bb.minY + o4, p->bb.minZ + o5,
                          3, secondary);
            break;
        case 1:
        case 3:
            fortress_next(p, start_piece, s, rand, p->bb.minX + o5, p->bb.minY + o4, p->bb.maxZ + 1,
                          0, secondary);
            break;
        default:
            break;
    }
}

void fortress_build_component(struct piece *p, struct piece *parent, struct start *s, jrand *rand)
{
    struct piece *start_piece = parent;

    switch (p->u.fortress.kind)
    {
        case F_START:      /* Start extends Crossing3 */
        case F_CROSSING3:
            next_normal(p, start_piece, s, rand, 8, 3, 0);
            next_x(p, start_piece, s, rand, 3, 8, 0);
            next_z(p, start_piece, s, rand, 3, 8, 0);
            break;
        case F_STRAIGHT:
            next_normal(p, start_piece, s, rand, 1, 3, 0);
            break;
        case F_CORRIDOR:
            next_x(p, start_piece, s, rand, 0, 1, 1);
            break;
        case F_CORRIDOR2:
            next_z(p, start_piece, s, rand, 0, 1, 1);
            break;
        case F_CORRIDOR3:
            next_normal(p, start_piece, s, rand, 1, 0, 1);
            break;
        case F_CORRIDOR4:
        {
            /* The two draws are arguments of the two calls, so they happen
             * between them, in this order. */
            int reach = (p->coord_base_mode == 1 || p->coord_base_mode == 2) ? 5 : 1;
            int go_x = jr_int_n(rand, 8) > 0;
            next_x(p, start_piece, s, rand, 0, reach, go_x);
            int go_z = jr_int_n(rand, 8) > 0;
            next_z(p, start_piece, s, rand, 0, reach, go_z);
            break;
        }
        case F_CORRIDOR5:
            next_normal(p, start_piece, s, rand, 1, 0, 1);
            break;
        case F_CROSSING:
            next_normal(p, start_piece, s, rand, 2, 0, 0);
            next_x(p, start_piece, s, rand, 0, 2, 0);
            next_z(p, start_piece, s, rand, 0, 2, 0);
            break;
        case F_CROSSING2:
            next_normal(p, start_piece, s, rand, 1, 0, 1);
            next_x(p, start_piece, s, rand, 0, 1, 1);
            next_z(p, start_piece, s, rand, 0, 1, 1);
            break;
        case F_END:
        case F_THRONE:
            break;   /* the base class's empty buildComponent */
        case F_ENTRANCE:
            next_normal(p, start_piece, s, rand, 5, 3, 1);
            break;
        case F_NETHER_STALK:
            next_normal(p, start_piece, s, rand, 5, 3, 1);
            next_normal(p, start_piece, s, rand, 5, 11, 1);
            break;
        case F_STAIRS:
            next_z(p, start_piece, s, rand, 6, 2, 0);
            break;
    }
}

/* MapGenNetherBridge.Start: the start piece, its own copies of the two
 * weighted lists, its buildComponent, the pending queue in a random order,
 * then setRandomHeight(world, rand, 48, 70). */
static void fortress_make_start(jrand *rand, int cx, int cz, struct start *s)
{
    struct piece *start_piece = calloc(1, sizeof *start_piece);
    if (!start_piece) abort();
    start_piece->id = ids[F_START];
    start_piece->kind = PIECE_FORTRESS;
    start_piece->component_type = 0;
    start_piece->u.fortress.kind = F_START;

    /* Crossing3(Random, x, z): coordBaseMode first, then the fixed 19x19 box
     * at y 64..73. */
    start_piece->coord_base_mode = jr_int_n(rand, 4);
    int x = (cx << 4) + 2;
    int z = (cz << 4) + 2;
    start_piece->bb = bbox_make(x, 64, z, x + 19 - 1, 73, z + 19 - 1);

    /* Start(): the static tables, their counters reset. */
    for (int i = 0; i < 6; ++i)
    {
        start_piece->u.fortress.store[i] = primary_weights[i];
        start_piece->u.fortress.store[i].spawned = 0;
        start_piece->u.fortress.primary[i] = &start_piece->u.fortress.store[i];
    }
    start_piece->u.fortress.nprimary = 6;
    for (int i = 0; i < 7; ++i)
    {
        start_piece->u.fortress.store[6 + i] = secondary_weights[i];
        start_piece->u.fortress.store[6 + i].spawned = 0;
        start_piece->u.fortress.secondary[i] = &start_piece->u.fortress.store[6 + i];
    }
    start_piece->u.fortress.nsecondary = 7;
    start_piece->u.fortress.last = NULL;

    start_add(s, start_piece);

    piece_build_component(start_piece, start_piece, s, rand);

    while (start_piece->u.fortress.pending.n > 0)
    {
        int i = jr_int_n(rand, start_piece->u.fortress.pending.n);
        struct piece *p = start_piece->u.fortress.pending.v[i];
        for (int j = i; j + 1 < start_piece->u.fortress.pending.n; ++j)
            start_piece->u.fortress.pending.v[j] = start_piece->u.fortress.pending.v[j + 1];
        --start_piece->u.fortress.pending.n;
        piece_build_component(p, start_piece, s, rand);
    }

    start_update_bb(s);
    start_set_random_height(s, rand, 48, 70);
}

const struct structure_type structure_fortress = {
    "Fortress",
    fortress_begin,
    fortress_can_spawn,
    fortress_make_start
};
