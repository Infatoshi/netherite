/* StructureStrongholdPieces and MapGenStronghold. Every Random draw is in
 * Java's order: a constructor draws before its buildComponent, a call that
 * draws twice from one Random is split into statements, and the weighted pick
 * walks the piece list exactly as getNextComponent does.
 *
 * Each piece's own state (its door, its kind's flags) lives in a struct that the
 * piece owns, so a start's tree carries everything it needs with it; piece_nbt
 * renders it through stronghold_piece_nbt.
 *
 * The ring positions come from the world seed: canSpawnStructureAtCoords builds
 * its Random with Det.newRandom() and immediately calls setSeed(worldSeed), so
 * it is plain new Random(worldSeed); the three positions are drawn from it and
 * refined through WorldChunkManager.findBiomePosition (the biome layers).
 *
 * The weighted list and its instance counters are process state, as in Java:
 * prepareStructurePieces resets them, the walk runs one type at a time, and a
 * start's retry loop restarts them on every attempt (Java calls
 * prepareStructurePieces inside each attempt's Stairs2 construction).
 */
#include "env.h"
#include "stronghold.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "biomes.h"
#include "layers.h"
#include "world.h"

/* MapGenStronghold defaults. The world.conf "distance"/"count"/"spread"
 * switches are not read here: the oracle only ever builds the defaults. */
#define SH_DISTANCE 32.0
#define SH_SPREAD   3

/* getNextValidComponent's depth bound is 50 and every piece a start accepts is
 * appended to Stairs2.field_75026_c, so the pending list needs no fixed cap. */
#define SH_PENDING_INIT 16

static int abs_int(int v) { return v < 0 ? -v : v; }

/* Math.PI; C11 without _GNU_SOURCE has no M_PI. */
#define SH_PI 3.141592653589793

/* ------------------------------------------------------------------- ring */

#define sh_coords (nw_env->stronghold.coords)
#define sh_coords_ready (nw_env->stronghold.coords_ready)
#define sh_layers (nw_env->stronghold.layers)
#define sh_layers_ready (nw_env->stronghold.layers_ready)
#define sh_layers_seed (nw_env->stronghold.layers_seed)

/* WorldChunkManager.func_150795_a asks for the same biome box from every ring
 * position that lands in it; the generator is a pure function of the seed and
 * the position, so caching the last box is equivalent and cheap. */
#define sh_box (nw_env->stronghold.box)
#define sh_box_x (nw_env->stronghold.box_x)
#define sh_box_z (nw_env->stronghold.box_z)
#define sh_box_w (nw_env->stronghold.box_w)
#define sh_box_h (nw_env->stronghold.box_h)
#define sh_box_valid (nw_env->stronghold.box_valid)

/* Det.mix and Det.newRandom are not needed: MapGenStronghold draws one Random
 * from them and then calls setSeed(worldSeed) on it, which replaces the whole
 * 48-bit state, so the ring is exactly new Random(worldSeed). */
static void stronghold_ring_random(int64_t seed, jrand *out)
{
    jr_seed(out, seed);
}

static int *gen_box(int x, int z, int w, int h)
{
    if (sh_box_valid && sh_box_x == x && sh_box_z == z && sh_box_w == w && sh_box_h == h)
        return sh_box;
    free(sh_box);
    sh_box = malloc(sizeof(int) * (size_t)w * (size_t)h);
    if (!sh_box) abort();
    layer_ints(sh_layers.gen, x, z, w, h, sh_box);
    sh_box_x = x;
    sh_box_z = z;
    sh_box_w = w;
    sh_box_h = h;
    sh_box_valid = 1;
    return sh_box;
}

/* WorldChunkManager.func_150795_a: the running pick count is the bound, and it
 * only advances when a replacement happens, so the same box consumes the same
 * Randoms in Java and here. */
static int find_biome_position(int px, int pz, int radius, jrand *rand, int *ox, int *oz)
{
    int x0 = (px - radius) >> 2;
    int z0 = (pz - radius) >> 2;
    int x1 = (px + radius) >> 2;
    int z1 = (pz + radius) >> 2;
    int w = x1 - x0 + 1;
    int h = z1 - z0 + 1;
    int *v = gen_box(x0, z0, w, h);
    int found = 0, picks = 0;   /* var13 == null and var14: picks, not suitable cells */

    for (int i = 0; i < w * h; ++i)
    {
        int id = v[i] & 255;
        if (BIOMES[id].exists && BIOMES[id].root > 0.0f)
        {
            /* var13 == null short-circuits the first draw; var14 counts the
             * replacements, not the suitable cells. */
            if (!found || jr_int_n(rand, picks + 1) == 0)
            {
                *ox = (x0 + i % w) << 2;
                *oz = (z0 + i / w) << 2;
                found = 1;
                ++picks;
            }
        }
    }
    return found;
}

static void stronghold_begin(int64_t seed)
{
    jrand rand;

    /* canSpawnStructureAtCoords: Random var3 = Det.newRandom(); var3.setSeed(seed). */
    stronghold_ring_random(seed, &rand);

    double angle = jr_double(&rand) * SH_PI * 2.0;
    int spread = SH_SPREAD;
    int dist = 1;

    /* The ring and the biome scan are cached process-wide, as Java caches the
     * generator's structureCoords and the WorldChunkManager's layer stack. A
     * walk of a second seed in the same process has to rebuild both: layers_init
     * memsets, so re-initializing is enough, and the box cache is keyed by
     * position only. */
    if (!sh_layers_ready || sh_layers_seed != seed)
    {
        layers_init(&sh_layers, seed);
        sh_layers_seed = seed;
        sh_box_valid = 0;
        sh_layers_ready = 1;
    }

    for (int i = 0; i < SH_COUNT; ++i)
    {
        double r = (1.25 * (double)dist + jr_double(&rand)) * SH_DISTANCE * (double)dist;
        int cx = (int)lround(cos(angle) * r);
        int cz = (int)lround(sin(angle) * r);
        int bx, bz;

        if (find_biome_position((cx << 4) + 8, (cz << 4) + 8, 112, &rand, &bx, &bz))
        {
            cx = bx >> 4;
            cz = bz >> 4;
        }

        sh_coords[i].cx = cx;
        sh_coords[i].cz = cz;

        angle += (SH_PI * 2.0) * (double)dist / (double)spread;

        if (i == spread)
        {
            dist += 2 + jr_int_n(&rand, 5);
            spread += 1 + jr_int_n(&rand, 2);
        }
    }
    sh_coords_ready = 1;
}

void stronghold_note_chunk(struct world *w, int cx, int cz)
{
    if (!sh_coords_ready || sh_layers_seed != w->seed) stronghold_begin(w->seed);
    for (int i = 0; i < SH_COUNT; ++i)
        if (abs(sh_coords[i].cx - cx) <= 8 && abs(sh_coords[i].cz - cz) <= 8)
            w->stronghold_seen_mask |= (uint8_t)(1u << i);
}

void stronghold_note_start(struct world *w, int cx, int cz, int linked)
{
    if (!sh_coords_ready || sh_layers_seed != w->seed) stronghold_begin(w->seed);
    for (int i = 0; i < SH_COUNT; ++i)
        if (sh_coords[i].cx == cx && sh_coords[i].cz == cz)
        {
            w->stronghold_seen_mask |= (uint8_t)(1u << i);
            if (linked) w->stronghold_unlinked_mask &= (uint8_t)~(1u << i);
            else w->stronghold_unlinked_mask |= (uint8_t)(1u << i);
        }
}

static void stronghold_nearest_impl(const struct world *w, int64_t seed,
                                    int x, int y, int z, int *tx, int *ty, int *tz)
{
    if (!sh_coords_ready || sh_layers_seed != seed) stronghold_begin(seed);
    double best = 1.7976931348623157e308;
    for (int i = 0; i < SH_COUNT; ++i)
    {
        int bx = sh_coords[i].cx * 16 + 8;
        int bz = sh_coords[i].cz * 16 + 8;
        int dx = bx - x, dy = 64 - y, dz = bz - z;
        double d = (double)(dx * dx + dy * dy + dz * dz);
        if (d < best) { best = d; *tx = bx; *ty = 64; *tz = bz; }
    }

    /* A raw chunk generation visits the surrounding eight candidate chunks.
     * MapGenStructure.func_151545_a also generates the query chunk. Its
     * structureMap then takes precedence over getCoordList's ring fallback. */
    int found_start = 0;
    for (int i = 0; i < SH_COUNT; ++i)
    {
        int cx = sh_coords[i].cx, cz = sh_coords[i].cz;
        int generated = abs(cx - (x >> 4)) <= 8 && abs(cz - (z >> 4)) <= 8;
        if (w != NULL && (w->stronghold_seen_mask & (1u << i))) generated = 1;
        if (w != NULL && !generated)
            for (size_t j = 0; j < w->lon; ++j)
            {
                int lx = (int)(uint32_t)w->load_order[j];
                int lz = (int)(w->load_order[j] >> 32);
                if (abs(cx - lx) <= 8 && abs(cz - lz) <= 8)
                { generated = 1; break; }
            }
        if (!generated) continue;

        jrand rand;
        jr_seed(&rand, seed);
        int64_t mul_x = jr_long(&rand), mul_z = jr_long(&rand);
        int64_t kx = (int64_t)((uint64_t)(int64_t)cx * (uint64_t)mul_x);
        int64_t kz = (int64_t)((uint64_t)(int64_t)cz * (uint64_t)mul_z);
        jr_seed(&rand, kx ^ kz ^ seed);
        jr_int(&rand); /* MapGenStructure.func_151538_a before can_spawn */
        struct start s;
        start_init(&s, "Stronghold", cx, cz);
        structure_stronghold.make_start(&rand, cx, cz, &s);
        if (s.n > 0)
        {
            const struct bbox *b = &s.pieces[0]->bb;
            /* Stairs2.func_151553_a points to its portal room, not to the
             * staircase bounding box inherited from StructureComponent, when
             * it links one: a start read back from the save does not */
            if (w == NULL || !(w->stronghold_unlinked_mask & (1u << i)))
                for (int j = 0; j < s.n; ++j)
                    if (stronghold_state(s.pieces[j])->kind == SHS_PORTALROOM)
                    { b = &s.pieces[j]->bb; break; }
            int px = b->minX + (b->maxX - b->minX + 1) / 2;
            int py = b->minY + (b->maxY - b->minY + 1) / 2;
            int pz = b->minZ + (b->maxZ - b->minZ + 1) / 2;
            int dx = px - x, dy = py - y, dz = pz - z;
            double d = (double)(dx * dx + dy * dy + dz * dz);
            if (!found_start || d < best)
            { found_start = 1; best = d; *tx = px; *ty = py; *tz = pz; }
        }
        start_free(&s);
    }
}

void stronghold_nearest(int64_t seed, int x, int y, int z, int *tx, int *ty, int *tz)
{
    stronghold_nearest_impl(NULL, seed, x, y, z, tx, ty, tz);
}

void stronghold_nearest_loaded(const struct world *w, int x, int y, int z,
                               int *tx, int *ty, int *tz)
{
    stronghold_nearest_impl(w, w->seed, x, y, z, tx, ty, tz);
}

static int stronghold_can_spawn(jrand *rand, int cx, int cz)
{
    for (int i = 0; i < SH_COUNT; ++i)
        if (cx == sh_coords[i].cx && cz == sh_coords[i].cz) return 1;
    return 0;
}

/* ----------------------------------------------------- kinds and state --- */

static const char *const sh_door_name[4] = {"OPENING", "WOOD_DOOR", "GRATES", "IRON_DOOR"};

const struct sh_state *stronghold_state(const struct piece *p)
{
    return (struct sh_state *)p->owned;
}

static struct sh_state *sh_of(const struct piece *p);

/* The two flags a stronghold piece's generation sets once: ChestCorridor's
 * hasMadeChest, PortalRoom's hasSpawner (structures.json Stronghold.flags). */
void stronghold_piece_set_flags(struct piece *p, int chest, int mob)
{
    struct sh_state *st = sh_of(p);

    if (st->kind == SHS_CHESTCORRIDOR) st->chest = chest;
    else if (st->kind == SHS_PORTALROOM) st->mob = mob;
}

static struct sh_state *sh_of(const struct piece *p)
{
    return (struct sh_state *)p->owned;
}

/* pieceWeightArray, in order. `type_gt` is the extra guard Library (4) and
 * PortalRoom (5) add to canSpawnMoreStructuresOfType. */
static const struct {
    int kind, weight, limit, type_gt;
} sh_weights[SH_WEIGHT_N] = {
    {SHS_STRAIGHT, 40, 0, -1},
    {SHS_PRISON, 5, 5, -1},
    {SHS_LEFTTURN, 20, 0, -1},
    {SHS_RIGHTTURN, 20, 0, -1},
    {SHS_ROOMCROSSING, 10, 6, -1},
    {SHS_STAIRSSTRAIGHT, 5, 5, -1},
    {SHS_STAIRS, 5, 5, -1},
    {SHS_CROSSING, 5, 4, -1},
    {SHS_CHESTCORRIDOR, 5, 4, -1},
    {SHS_LIBRARY, 10, 2, 4},
    {SHS_PORTALROOM, 20, 1, 5},
};
/* each weight's instancesSpawned, the walk's own count */
#define sh_spawned (nw_env->stronghold.spawned)

#define sh_total_weight (nw_env->stronghold.total_weight)
#define sh_active (nw_env->stronghold.active)
#define sh_active_n (nw_env->stronghold.active_n)
/* strongComponentType, -1 for none */
#define sh_strong_component (nw_env->stronghold.strong_component)
/* the start's strongholdPieceWeight, -1 for none */
#define sh_current_weight (nw_env->stronghold.current_weight)

/* Stairs2 (the start) and its pending children: getNextValidComponent's
 * p_75196_0_.field_75026_c. The start's own bounding box and the portal room it
 * found are the other two fields the walk reads. */
#define sh_start_bb (nw_env->stronghold.start_bb)
#define sh_portal_found (nw_env->stronghold.portal_found)
#define sh_pending (nw_env->stronghold.pending)
#define sh_pending_n (nw_env->stronghold.pending_n)
#define sh_pending_cap (nw_env->stronghold.pending_cap)

static void sh_pending_add(struct piece *p)
{
    if (sh_pending_n == sh_pending_cap)
    {
        sh_pending_cap = sh_pending_cap ? 2 * sh_pending_cap : SH_PENDING_INIT;
        sh_pending = realloc(sh_pending, (size_t)sh_pending_cap * sizeof *sh_pending);
        if (!sh_pending) abort();
    }
    sh_pending[sh_pending_n++] = p;
}

static void prepare_structure_pieces(void)
{
    for (int i = 0; i < SH_WEIGHT_N; ++i)
    {
        sh_spawned[i] = 0;
        sh_active[i] = i;
    }
    sh_active_n = SH_WEIGHT_N;
    sh_strong_component = -1;
}

/* ------------------------------------------------------------- pieces --- */

static struct piece *mk_empty(const char *id)
{
    struct piece *p = calloc(1, sizeof *p);
    struct sh_state *st = calloc(1, sizeof *st);
    if (!p || !st) abort();
    p->id = id;
    p->kind = PIECE_STRONGHOLD;
    p->owned = st;
    p->component_type = 0;
    return p;
}

/* Stronghold.getRandomDoor */
static int get_random_door(jrand *rand)
{
    int v = jr_int_n(rand, 5);
    if (v == 2) return DOOR_WOOD;
    if (v == 3) return DOOR_GRATES;
    if (v == 4) return DOOR_IRON;
    return DOOR_OPENING;
}

static int can_go_deeper(const struct bbox *b)
{
    return b->minY > 10;
}

static struct piece *finish(struct piece *p, int kind, int gd, struct bbox bb, int mode, int door)
{
    struct sh_state *st = sh_of(p);
    st->kind = kind;
    st->door = door;
    p->coord_base_mode = mode;
    p->component_type = gd;
    p->bb = bb;
    return p;
}

/* ---- constructors, each drawing exactly what its Java constructor draws ---- */

static struct piece *mk_straight(jrand *rand, int gd, struct bbox bb, int mode)
{
    int door = get_random_door(rand);
    int left = jr_int_n(rand, 2) == 0;
    int right = jr_int_n(rand, 2) == 0;
    struct piece *p = finish(mk_empty("SHS"), SHS_STRAIGHT, gd, bb, mode, door);
    sh_of(p)->left = left;
    sh_of(p)->right = right;
    return p;
}

static struct piece *mk_prison(jrand *rand, int gd, struct bbox bb, int mode)
{
    int door = get_random_door(rand);
    return finish(mk_empty("SHPH"), SHS_PRISON, gd, bb, mode, door);
}

static struct piece *mk_leftturn(jrand *rand, int gd, struct bbox bb, int mode)
{
    int door = get_random_door(rand);
    return finish(mk_empty("SHLT"), SHS_LEFTTURN, gd, bb, mode, door);
}

static struct piece *mk_roomcrossing(jrand *rand, int gd, struct bbox bb, int mode)
{
    int door = get_random_door(rand);
    int room_type = jr_int_n(rand, 5);
    struct piece *p = finish(mk_empty("SHRC"), SHS_ROOMCROSSING, gd, bb, mode, door);
    sh_of(p)->room_type = room_type;
    return p;
}

static struct piece *mk_stairsstraight(jrand *rand, int gd, struct bbox bb, int mode)
{
    int door = get_random_door(rand);
    return finish(mk_empty("SHSSD"), SHS_STAIRSSTRAIGHT, gd, bb, mode, door);
}

/* Stairs(int, Random, StructureBoundingBox, int): Source is false. */
static struct piece *mk_stairs(jrand *rand, int gd, struct bbox bb, int mode)
{
    int door = get_random_door(rand);
    struct piece *p = finish(mk_empty("SHSD"), SHS_STAIRS, gd, bb, mode, door);
    sh_of(p)->source = 0;
    return p;
}

/* Stairs2: Stairs(int, Random, int, int) then a fixed bounding box at y 64..74.
 * Source is true and the door is OPENING. */
static struct piece *mk_stairs2(jrand *rand, int x, int z)
{
    int mode = jr_int_n(rand, 4);
    struct bbox bb = bbox_make(x, 64, z, x + 5 - 1, 74, z + 5 - 1);
    struct piece *p = finish(mk_empty("SHStart"), SHS_STAIRS, 0, bb, mode, DOOR_OPENING);
    sh_of(p)->source = 1;
    return p;
}

static struct piece *mk_crossing(jrand *rand, int gd, struct bbox bb, int mode)
{
    int door = get_random_door(rand);
    int ll = jr_bool(rand);
    int lh = jr_bool(rand);
    int rl = jr_bool(rand);
    int rh = jr_int_n(rand, 3) > 0;
    struct piece *p = finish(mk_empty("SH5C"), SHS_CROSSING, gd, bb, mode, door);
    sh_of(p)->cross_ll = ll;
    sh_of(p)->cross_lh = lh;
    sh_of(p)->cross_rl = rl;
    sh_of(p)->cross_rh = rh;
    return p;
}

static struct piece *mk_chestcorridor(jrand *rand, int gd, struct bbox bb, int mode)
{
    int door = get_random_door(rand);
    return finish(mk_empty("SHCC"), SHS_CHESTCORRIDOR, gd, bb, mode, door);
}

static struct piece *mk_library(jrand *rand, int gd, struct bbox bb, int mode)
{
    int door = get_random_door(rand);
    struct piece *p = finish(mk_empty("SHLi"), SHS_LIBRARY, gd, bb, mode, door);
    sh_of(p)->tall = bbox_ysize(&bb) > 6;
    return p;
}

static struct piece *mk_portalroom(int gd, struct bbox bb, int mode)
{
    return finish(mk_empty("SHPR"), SHS_PORTALROOM, gd, bb, mode, DOOR_OPENING);
}

static struct piece *mk_corridor(jrand *rand, int gd, struct bbox bb, int mode)
{
    struct piece *p = finish(mk_empty("SHFC"), SHS_CORRIDOR, gd, bb, mode, DOOR_OPENING);
    sh_of(p)->steps = (mode != 2 && mode != 0) ? bbox_xsize(&bb) : bbox_zsize(&bb);
    return p;
}

/* ---- findValidPlacement ---- */

static struct piece *sh_find(struct start *s, const struct bbox *b)
{
    return start_find_intersecting(s, b);
}

static struct piece *find_straight(struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    struct bbox bb = bbox_component_to_add(x, y, z, -1, -1, 0, 5, 5, 7, mode);
    if (!can_go_deeper(&bb) || sh_find(s, &bb) != NULL) return NULL;
    return mk_straight(rand, gd, bb, mode);
}

static struct piece *find_prison(struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    struct bbox bb = bbox_component_to_add(x, y, z, -1, -1, 0, 9, 5, 11, mode);
    if (!can_go_deeper(&bb) || sh_find(s, &bb) != NULL) return NULL;
    return mk_prison(rand, gd, bb, mode);
}

static struct piece *find_leftturn(struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    struct bbox bb = bbox_component_to_add(x, y, z, -1, -1, 0, 5, 5, 5, mode);
    if (!can_go_deeper(&bb) || sh_find(s, &bb) != NULL) return NULL;
    return mk_leftturn(rand, gd, bb, mode);
}

static struct piece *find_rightturn(struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    /* RightTurn does not override findValidPlacement, so the method is
     * LeftTurn's, whose body is `new LeftTurn(...)`: the RightTurn weight
     * yields a LeftTurn object (id "SHLT", LeftTurn's buildComponent) placed
     * exactly as a LeftTurn would be. */
    return find_leftturn(s, rand, x, y, z, mode, gd);
}

static struct piece *find_roomcrossing(struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    struct bbox bb = bbox_component_to_add(x, y, z, -4, -1, 0, 11, 7, 11, mode);
    if (!can_go_deeper(&bb) || sh_find(s, &bb) != NULL) return NULL;
    return mk_roomcrossing(rand, gd, bb, mode);
}

static struct piece *find_stairsstraight(struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    struct bbox bb = bbox_component_to_add(x, y, z, -1, -7, 0, 5, 11, 8, mode);
    if (!can_go_deeper(&bb) || sh_find(s, &bb) != NULL) return NULL;
    return mk_stairsstraight(rand, gd, bb, mode);
}

static struct piece *find_stairs(struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    struct bbox bb = bbox_component_to_add(x, y, z, -1, -7, 0, 5, 11, 5, mode);
    if (!can_go_deeper(&bb) || sh_find(s, &bb) != NULL) return NULL;
    return mk_stairs(rand, gd, bb, mode);
}

static struct piece *find_crossing(struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    struct bbox bb = bbox_component_to_add(x, y, z, -4, -3, 0, 10, 9, 11, mode);
    if (!can_go_deeper(&bb) || sh_find(s, &bb) != NULL) return NULL;
    return mk_crossing(rand, gd, bb, mode);
}

static struct piece *find_chestcorridor(struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    struct bbox bb = bbox_component_to_add(x, y, z, -1, -1, 0, 5, 5, 7, mode);
    if (!can_go_deeper(&bb) || sh_find(s, &bb) != NULL) return NULL;
    return mk_chestcorridor(rand, gd, bb, mode);
}

static struct piece *find_library(struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    struct bbox bb = bbox_component_to_add(x, y, z, -4, -1, 0, 14, 11, 15, mode);
    if (!can_go_deeper(&bb) || sh_find(s, &bb) != NULL)
    {
        bb = bbox_component_to_add(x, y, z, -4, -1, 0, 14, 6, 15, mode);
        if (!can_go_deeper(&bb) || sh_find(s, &bb) != NULL) return NULL;
    }
    return mk_library(rand, gd, bb, mode);
}

static struct piece *find_portalroom(struct start *s, int x, int y, int z, int mode, int gd)
{
    struct bbox bb = bbox_component_to_add(x, y, z, -4, -1, 0, 11, 8, 16, mode);
    if (!can_go_deeper(&bb) || sh_find(s, &bb) != NULL) return NULL;
    return mk_portalroom(gd, bb, mode);
}

/* Corridor.func_74992_a: the longest corridor that still touches the piece the
 * caller ran into, and only when that piece's floor is at the same height. */
static int find_corridor_pre(struct start *s, int x, int y, int z, int mode, struct bbox *out)
{
    struct bbox bb = bbox_component_to_add(x, y, z, -1, -1, 0, 5, 5, 4, mode);
    struct piece *hit = sh_find(s, &bb);

    if (hit == NULL) return 0;

    if (hit->bb.minY == bb.minY)
    {
        for (int steps = 2; steps >= 0; --steps)
        {
            bb = bbox_component_to_add(x, y, z, -1, -1, 0, 5, 5, steps, mode);
            if (!bbox_intersects(&hit->bb, &bb))
            {
                *out = bbox_component_to_add(x, y, z, -1, -1, 0, 5, 5, steps + 1, mode);
                return 1;
            }
        }
    }
    return 0;
}

/* ---- the weighted walk ---- */

static struct piece *build_kind(int kind, struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    switch (kind)
    {
        case SHS_STRAIGHT: return find_straight(s, rand, x, y, z, mode, gd);
        case SHS_PRISON: return find_prison(s, rand, x, y, z, mode, gd);
        case SHS_LEFTTURN: return find_leftturn(s, rand, x, y, z, mode, gd);
        case SHS_RIGHTTURN: return find_rightturn(s, rand, x, y, z, mode, gd);
        case SHS_ROOMCROSSING: return find_roomcrossing(s, rand, x, y, z, mode, gd);
        case SHS_STAIRSSTRAIGHT: return find_stairsstraight(s, rand, x, y, z, mode, gd);
        case SHS_STAIRS: return find_stairs(s, rand, x, y, z, mode, gd);
        case SHS_CROSSING: return find_crossing(s, rand, x, y, z, mode, gd);
        case SHS_CHESTCORRIDOR: return find_chestcorridor(s, rand, x, y, z, mode, gd);
        case SHS_LIBRARY: return find_library(s, rand, x, y, z, mode, gd);
        case SHS_PORTALROOM: return find_portalroom(s, x, y, z, mode, gd);
    }
    return NULL;
}

/* PieceWeight.canSpawnMoreStructuresOfType(gd) */
static int can_spawn_type(int i, int gd)
{
    if (sh_weights[i].limit != 0 && sh_spawned[i] >= sh_weights[i].limit) return 0;
    return sh_weights[i].type_gt < 0 || gd > sh_weights[i].type_gt;
}

static int can_add_structure_pieces(void)
{
    int any = 0;
    sh_total_weight = 0;
    /* Java iterates structurePieceList: a weight removed at its instance limit
     * stops contributing to totalWeight. */
    for (int k = 0; k < sh_active_n; ++k)
    {
        int i = sh_active[k];
        if (sh_weights[i].limit > 0 && sh_spawned[i] < sh_weights[i].limit) any = 1;
        sh_total_weight += sh_weights[i].weight;
    }
    return any;
}

/* getNextComponent */
static struct piece *get_next_component(struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    int tries;

    if (!can_add_structure_pieces()) return NULL;

    if (sh_strong_component >= 0)
    {
        struct piece *p = build_kind(sh_strong_component, s, rand, x, y, z, mode, gd);
        sh_strong_component = -1;
        if (p != NULL) return p;
    }

    for (tries = 0; tries < 5; ++tries)
    {
        int roll = jr_int_n(rand, sh_total_weight);

        for (int k = 0; k < sh_active_n; ++k)
        {
            int i = sh_active[k];
            roll -= sh_weights[i].weight;

            if (roll < 0)
            {
                if (!can_spawn_type(i, gd) || i == sh_current_weight) break;

                struct piece *p = build_kind(sh_weights[i].kind, s, rand, x, y, z, mode, gd);
                if (p != NULL)
                {
                    ++sh_spawned[i];
                    sh_current_weight = i;
                    if (!(sh_weights[i].limit == 0 || sh_spawned[i] < sh_weights[i].limit))
                    {
                        for (int j = k; j + 1 < sh_active_n; ++j) sh_active[j] = sh_active[j + 1];
                        --sh_active_n;
                    }
                    return p;
                }
            }
        }
    }

    {
        struct bbox bb;
        if (find_corridor_pre(s, x, y, z, mode, &bb) && bb.minY > 1)
            return mk_corridor(rand, gd, bb, mode);
    }
    return NULL;
}

/* getNextValidComponent */
static struct piece *get_next_valid(struct start *s, jrand *rand, int x, int y, int z, int mode, int gd)
{
    struct piece *p;

    if (gd > 50) return NULL;
    if (abs_int(x - sh_start_bb.minX) > 112 || abs_int(z - sh_start_bb.minZ) > 112) return NULL;

    /* Java: getNextComponent(..., p_75196_7_ + 1). */
    ++gd;

    p = get_next_component(s, rand, x, y, z, mode, gd);
    if (p != NULL)
    {
        start_add(s, p);
        sh_pending_add(p);
    }
    return p;
}

/* Stronghold.getNextComponentNormal / X / Z, each dispatching on the mode. */
static void next_normal(struct start *s, struct piece *p, jrand *rand, int a, int b)
{
    switch (p->coord_base_mode)
    {
        case 0: get_next_valid(s, rand, p->bb.minX + a, p->bb.minY + b, p->bb.maxZ + 1, 0, p->component_type); break;
        case 1: get_next_valid(s, rand, p->bb.minX - 1, p->bb.minY + b, p->bb.minZ + a, 1, p->component_type); break;
        case 2: get_next_valid(s, rand, p->bb.minX + a, p->bb.minY + b, p->bb.minZ - 1, 2, p->component_type); break;
        case 3: get_next_valid(s, rand, p->bb.maxX + 1, p->bb.minY + b, p->bb.minZ + a, 3, p->component_type); break;
    }
}

static void next_x(struct start *s, struct piece *p, jrand *rand, int a, int b)
{
    switch (p->coord_base_mode)
    {
        case 0:
        case 2: get_next_valid(s, rand, p->bb.minX - 1, p->bb.minY + a, p->bb.minZ + b, 1, p->component_type); break;
        case 1:
        case 3: get_next_valid(s, rand, p->bb.minX + b, p->bb.minY + a, p->bb.minZ - 1, 2, p->component_type); break;
    }
}

static void next_z(struct start *s, struct piece *p, jrand *rand, int a, int b)
{
    switch (p->coord_base_mode)
    {
        case 0:
        case 2: get_next_valid(s, rand, p->bb.maxX + 1, p->bb.minY + a, p->bb.minZ + b, 3, p->component_type); break;
        case 1:
        case 3: get_next_valid(s, rand, p->bb.minX + b, p->bb.minY + a, p->bb.maxZ + 1, 0, p->component_type); break;
    }
}

static void build_component(struct start *s, struct piece *p, jrand *rand)
{
    struct sh_state *st = sh_of(p);
    int mode = p->coord_base_mode;

    switch (st->kind)
    {
        case SHS_STRAIGHT:
            next_normal(s, p, rand, 1, 1);
            if (st->left) next_x(s, p, rand, 1, 2);
            if (st->right) next_z(s, p, rand, 1, 2);
            break;

        case SHS_PRISON:
        case SHS_CHESTCORRIDOR:
        case SHS_STAIRSSTRAIGHT:
            next_normal(s, p, rand, 1, 1);
            break;

        case SHS_LEFTTURN:
            if (mode != 2 && mode != 3) next_z(s, p, rand, 1, 1);
            else next_x(s, p, rand, 1, 1);
            break;

        case SHS_RIGHTTURN:
            if (mode != 2 && mode != 3) next_x(s, p, rand, 1, 1);
            else next_z(s, p, rand, 1, 1);
            break;

        case SHS_ROOMCROSSING:
            next_normal(s, p, rand, 4, 1);
            next_x(s, p, rand, 1, 4);
            next_z(s, p, rand, 1, 4);
            break;

        case SHS_STAIRS:
            if (st->source) sh_strong_component = SHS_CROSSING;
            next_normal(s, p, rand, 1, 1);
            break;

        case SHS_CROSSING:
        {
            int a = 3, b = 5;
            if (mode == 1 || mode == 2) { a = 8 - a; b = 8 - b; }
            next_normal(s, p, rand, 5, 1);
            if (st->cross_ll) next_x(s, p, rand, a, 1);
            if (st->cross_lh) next_x(s, p, rand, b, 7);
            if (st->cross_rl) next_z(s, p, rand, a, 1);
            if (st->cross_rh) next_z(s, p, rand, b, 7);
            break;
        }

        case SHS_PORTALROOM:
            sh_portal_found = 1;
            break;

        case SHS_LIBRARY:
        case SHS_CORRIDOR:
            break;
    }
}

/* ---- MapGenStronghold.Start ---- */

static void stronghold_make_start(jrand *rand, int cx, int cz, struct start *s)
{
    struct piece *stairs;
    int attempts = 0;

    /* MapGenStronghold.getStructureStart's retry loop: prepareStructurePieces
     * and the whole tree are redone until a portal room is in the components. */
    for (;;)
    {
        if (++attempts > 2000)
        {
            fprintf(stderr, "stronghold (%d,%d): no portal room after %d attempts\n", cx, cz, attempts);
            abort();
        }
        prepare_structure_pieces();
        sh_current_weight = -1;
        sh_portal_found = 0;
        sh_pending_n = 0;

        stairs = mk_stairs2(rand, (cx << 4) + 2, (cz << 4) + 2);
        start_add(s, stairs);
        sh_start_bb = stairs->bb;
        build_component(s, stairs, rand);

        while (sh_pending_n > 0)
        {
            int pick_idx = jr_int_n(rand, sh_pending_n);
            struct piece *next = sh_pending[pick_idx];
            for (int i = pick_idx; i + 1 < sh_pending_n; ++i) sh_pending[i] = sh_pending[i + 1];
            --sh_pending_n;
            build_component(s, next, rand);
        }

        if (s->n > 0 && sh_portal_found) break;

        start_free(s);
        start_init(s, "Stronghold", cx, cz);
    }

    start_update_bb(s);
    start_mark_available_height(s, rand, 10);
}

/* ---- NBT ---- */

void stronghold_piece_nbt(nbt *c, const struct piece *p)
{
    const struct sh_state *st = sh_of(p);

    nbt_put(c, "EntryDoor", nbt_new_string(sh_door_name[st->door]));

    switch (st->kind)
    {
        case SHS_STRAIGHT:
            nbt_put(c, "Left", nbt_new_byte(st->left));
            nbt_put(c, "Right", nbt_new_byte(st->right));
            break;
        case SHS_CORRIDOR:
            nbt_put(c, "Steps", nbt_new_int(st->steps));
            break;
        case SHS_CROSSING:
            nbt_put(c, "leftLow", nbt_new_byte(st->cross_ll));
            nbt_put(c, "leftHigh", nbt_new_byte(st->cross_lh));
            nbt_put(c, "rightLow", nbt_new_byte(st->cross_rl));
            nbt_put(c, "rightHigh", nbt_new_byte(st->cross_rh));
            break;
        case SHS_CHESTCORRIDOR:
            nbt_put(c, "Chest", nbt_new_byte(st->chest));
            break;
        case SHS_ROOMCROSSING:
            nbt_put(c, "Type", nbt_new_int(st->room_type));
            break;
        case SHS_LIBRARY:
            nbt_put(c, "Tall", nbt_new_byte(st->tall));
            break;
        case SHS_STAIRS:
            nbt_put(c, "Source", nbt_new_byte(st->source));
            break;
        case SHS_PORTALROOM:
            nbt_put(c, "Mob", nbt_new_byte(st->mob));
            break;
        case SHS_PRISON:
        case SHS_LEFTTURN:
        case SHS_RIGHTTURN:
        case SHS_STAIRSSTRAIGHT:
            break;
    }
}

const struct structure_type structure_stronghold = {
    "Stronghold",
    stronghold_begin,
    stronghold_can_spawn,
    stronghold_make_start
};
