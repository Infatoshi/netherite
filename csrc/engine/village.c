/* MapGenVillage and StructureVillagePieces: the villages' start (a well), the
 * pending road and house queues, every piece the weighted list can build, and
 * the "Valid" flag. Every Random draw is in Java's order, and each Java prefix
 * that draws is its own statement here, since C leaves argument order
 * unspecified.
 *
 * The layout is a pure function of the world seed, the candidate chunk and the
 * world's biomes: MapGenVillage.canSpawnStructureAtCoords draws its two
 * nextInts from the world's own Random reseeded by
 * World.setRandomSeed(v, w, 10387312), tests areBiomesViable over
 * villageSpawnBiomes, and every candidate piece is tested again with
 * areBiomesViable at its own centre. So this file keeps its own jrand (seeded
 * the way setRandomSeed does) and its own layer stack, filled by
 * village_begin().
 *
 * Blocks, villagers and the population pass are a later lane: this is only
 * which chunk a village starts in and its tree of wells, roads and houses. */
#include "village.h"
#include "env.h"

#include <stdlib.h>

#include "jrand.h"
#include "layers.h"

/* MapGenVillage's fields (no-argument constructor): spacing 32, separation 8,
 * terrainType 0 for the default (non-flat) world. */
#define VILLAGE_MAX_DISTANCE 32
#define VILLAGE_MIN_DISTANCE 8
#define VILLAGE_TERRAIN_TYPE 0

/* canSpawnStructureAtCoords's salt. */
#define VILLAGE_SALT 10387312

/* getNextVillageStructureComponent's distance bound, and
 * getNextComponentVillagePath's depth bound. */
#define VILLAGE_HOUSE_RANGE 112
#define VILLAGE_PATH_DEPTH 3

/* villageSpawnBiomes: plains, desert, savanna. Java compares biome objects, so
 * the object id is what matters: field_150588_X is the Savanna object, id 35
 * (BiomeGenSavanna's slot 129 is Sunflower Plains, a different object). */
static const int village_biomes[] = {
    /* Plains */ 1,
    /* Desert */ 2,
    /* Savanna */ 35,
};

/* Set once per walk by village_begin(). The spawn test and every piece test are
 * pure functions of the world seed and the candidate, so one struct of walk
 * state is enough (nw_env->village). in_desert is
 * StructureVillagePieces.Start.inDesert for the start being built: the biome at
 * the well centre. Only pieces built through the Start read it; the well itself
 * was handed a null Start, so its own Desert stays false. */
#define village_world (nw_env->village)

void village_begin(int64_t seed)
{
    village_world.seed = seed;
    layers_init(&village_world.layers, seed);
}

/* World.setRandomSeed(x, y, salt): seed = x * 341873128712 + y * 132897987541
 * + world seed + salt, all in wrapping long arithmetic. */
static void set_random_seed(jrand *r, int x, int y, int salt)
{
    uint64_t v = (uint64_t)(int64_t)x * 341873128712ULL
               + (uint64_t)(int64_t)y * 132897987541ULL
               + (uint64_t)village_world.seed
               + (uint64_t)(int64_t)salt;
    jr_seed(r, (int64_t)v);
}

/* The world's full-resolution (voronoi) biome at a block position:
 * WorldChunkManager.getBiomeGenAt -> BiomeCache -> biomeIndexLayer. */
static int biome_gen_at(int x, int z)
{
    int id;
    biomes_full(&village_world.layers, x, z, 1, 1, &id);
    return id;
}

static int biome_listed(int id)
{
    for (size_t i = 0; i < sizeof village_biomes / sizeof village_biomes[0]; ++i)
        if (village_biomes[i] == id) return 1;
    return 0;
}

/* WorldChunkManager.areBiomesViable(x, z, radius, allowed): the quarter-scale
 * genBiomes over the box (x - radius) >> 2 .. (x + radius) >> 2, every cell
 * tested against the allowed list. */
static int are_biomes_viable(int x, int z, int radius)
{
    int var5 = (x - radius) >> 2;
    int var6 = (z - radius) >> 2;
    int var7 = (x + radius) >> 2;
    int var8 = (z + radius) >> 2;
    int w = var7 - var5 + 1;
    int h = var8 - var6 + 1;
    int *v = malloc(sizeof(int) * (size_t)w * (size_t)h);
    if (!v) abort();
    biomes_gen(&village_world.layers, var5, var6, w, h, v);
    int ok = 1;
    for (int i = 0; i < w * h; ++i)
        if (!biome_listed(v[i])) { ok = 0; break; }
    free(v);
    return ok;
}

/* A village piece: the shared structure header plus the Village fields, which
 * live in the piece's union (structure.h). */
static struct piece *vill_new(const char *id, enum village_kind kind, int desert)
{
    struct piece *p = calloc(1, sizeof *p);
    if (!p) abort();
    p->id = id;
    p->kind = PIECE_VILLAGE;
    p->component_type = 0;
    p->coord_base_mode = -1;                 /* StructureComponent(int) */
    p->u.village.kind = kind;
    p->u.village.hpos = -1;                  /* field_143015_k */
    p->u.village.desert = desert;
    p->u.village.last_weight = NULL;
    return p;
}

static int abs_int(int v)
{
    return v < 0 ? -v : v;
}

/* Village.canVillageGoDeeper: a box that exists and sits above the sea-level
 * guard. The Java signature takes the box alone (a null box is false); every
 * call site here already has a box from bbox_component_to_add. */
static int can_village_go_deeper(const struct bbox *b)
{
    return b->minY > 10;
}

/* Village.getAverageGroundLevel and addComponentParts are population-time:
 * HPos, VCount, the chest and the placed blocks all need a world to read, and
 * the layout pass never calls them. The first three keys still come out of the
 * tree here, with the values the constructors left. */

/* ---- the weighted piece list ---- */

/* MathHelper.getRandomIntegerInRange: [lo, hi] inclusive. */
static int rand_range(jrand *rand, int lo, int hi)
{
    if (lo >= hi) return lo;
    return jr_int_n(rand, hi - lo + 1) + lo;
}

static void weights_add(struct piece *p, enum village_kind class_kind, int weight, int limit)
{
    struct village_weight *slot;
    int n = p->u.village.nweights;
    if (n == 0 || (n & (n - 1)) == 0)
    {
        p->u.village.weights = realloc(p->u.village.weights,
                                       (size_t)(n ? 2 * n : 9) * sizeof *p->u.village.weights);
        if (!p->u.village.weights) abort();
    }
    slot = &p->u.village.weights[n];
    slot->class_kind = class_kind;
    slot->weight = weight;
    slot->spawned = 0;
    slot->limit = limit;
    p->u.village.nweights = n + 1;
}

/* getStructureVillageWeightedPieceList: nine weights, in Java's order, each
 * limit drawn from the piece Random. A weight whose limit came out 0 is
 * removed (Java iterates and removes while iterating; removing the current
 * element is equivalent to dropping it). */
static void weighted_piece_list(struct piece *start, jrand *rand, int terrain_type)
{
    int t = terrain_type;

    weights_add(start, V_HOUSE4, 4, rand_range(rand, 2 + t, 4 + t * 2));
    weights_add(start, V_CHURCH, 20, rand_range(rand, 0 + t, 1 + t));
    weights_add(start, V_HOUSE1, 20, rand_range(rand, 0 + t, 2 + t));
    weights_add(start, V_WOODHUT, 3, rand_range(rand, 2 + t, 5 + t * 3));
    weights_add(start, V_HALL, 15, rand_range(rand, 0 + t, 2 + t));
    weights_add(start, V_FIELD1, 3, rand_range(rand, 1 + t, 4 + t));
    weights_add(start, V_FIELD2, 3, rand_range(rand, 2 + t, 4 + t * 2));
    weights_add(start, V_HOUSE2, 15, rand_range(rand, 0, 1 + t));
    weights_add(start, V_HOUSE3, 8, rand_range(rand, 0 + t, 3 + t * 2));

    struct village_weight *w = start->u.village.weights;
    int n = 0;
    for (int i = 0; i < start->u.village.nweights; ++i)
        if (w[i].limit != 0) w[n++] = w[i];
    start->u.village.nweights = n;
}

/* func_75079_a: the sum of the weights still under their limit, or -1. */
static int weight_sum(struct piece *start)
{
    int sum = 0;
    int any = 0;
    for (int i = 0; i < start->u.village.nweights; ++i)
    {
        struct village_weight *w = &start->u.village.weights[i];
        if (w->limit > 0 && w->spawned < w->limit) any = 1;
        sum += w->weight;
    }
    return any ? sum : -1;
}

static int can_spawn_more(const struct village_weight *w)
{
    return w->limit == 0 || w->spawned < w->limit;
}

/* ---- piece vendors ---- */

static void set_bb(struct piece *p, struct bbox b, int mode)
{
    p->bb = b;
    p->coord_base_mode = mode;
}

/* House4Garden.func_74912_a: 5x6x5, no canVillageGoDeeper test. */
static int house4_find(struct start *s, jrand *rand, int x, int y, int z, int mode, struct bbox *out)
{
    *out = bbox_component_to_add(x, y, z, 0, 0, 0, 5, 6, 5, mode);
    return start_find_intersecting(s, out) == NULL;
}

static struct piece *house4_new(jrand *rand, struct bbox bb, int mode, int desert)
{
    struct piece *p = vill_new("ViSH", V_HOUSE4, desert);
    set_bb(p, bb, mode);
    p->u.village.terrace = jr_bool(rand);
    return p;
}

/* Church.func_74919_a: 5x12x9. */
static int church_find(struct start *s, int x, int y, int z, int mode, struct bbox *out)
{
    *out = bbox_component_to_add(x, y, z, 0, 0, 0, 5, 12, 9, mode);
    return can_village_go_deeper(out) && start_find_intersecting(s, out) == NULL;
}

static struct piece *church_new(struct bbox bb, int mode, int desert)
{
    struct piece *p = vill_new("ViST", V_CHURCH, desert);
    set_bb(p, bb, mode);
    return p;
}

/* House1.func_74898_a: 9x9x6. */
static int house1_find(struct start *s, int x, int y, int z, int mode, struct bbox *out)
{
    *out = bbox_component_to_add(x, y, z, 0, 0, 0, 9, 9, 6, mode);
    return can_village_go_deeper(out) && start_find_intersecting(s, out) == NULL;
}

static struct piece *house1_new(struct bbox bb, int mode, int desert)
{
    struct piece *p = vill_new("ViBH", V_HOUSE1, desert);
    set_bb(p, bb, mode);
    return p;
}

/* WoodHut.func_74908_a: 4x6x5. The constructor draws two more: isTallHouse,
 * then tablePosition. */
static int woodhut_find(struct start *s, int x, int y, int z, int mode, struct bbox *out)
{
    *out = bbox_component_to_add(x, y, z, 0, 0, 0, 4, 6, 5, mode);
    return can_village_go_deeper(out) && start_find_intersecting(s, out) == NULL;
}

static struct piece *woodhut_new(jrand *rand, struct bbox bb, int mode, int desert)
{
    struct piece *p = vill_new("ViSmH", V_WOODHUT, desert);
    set_bb(p, bb, mode);
    p->u.village.tall_house = jr_bool(rand);
    p->u.village.table = jr_int_n(rand, 3);
    return p;
}

/* Hall.func_74906_a: 9x7x11. */
static int hall_find(struct start *s, int x, int y, int z, int mode, struct bbox *out)
{
    *out = bbox_component_to_add(x, y, z, 0, 0, 0, 9, 7, 11, mode);
    return can_village_go_deeper(out) && start_find_intersecting(s, out) == NULL;
}

static struct piece *hall_new(struct bbox bb, int mode, int desert)
{
    struct piece *p = vill_new("ViPH", V_HALL, desert);
    set_bb(p, bb, mode);
    return p;
}

/* The Start's field_74932_i / field_74930_j queues: Java List.add, taking the
 * element out with remove(int). */
static void village_pending_add(struct piece_list *q, struct piece *p)
{
    if (q->n == q->cap)
    {
        q->cap = q->cap ? 2 * q->cap : 8;
        q->v = realloc(q->v, (size_t)q->cap * sizeof *q->v);
        if (!q->v) abort();
    }
    q->v[q->n++] = p;
}

/* func_151559_a / func_151560_a: carrots on 0, potatoes on 1, wheat otherwise;
 * read back as the block registry id (carrots 141, potatoes 142, wheat 59). */
static int crop_pick(jrand *rand)
{
    switch (jr_int_n(rand, 5))
    {
        case 0: return 141;
        case 1: return 142;
        default: return 59;
    }
}

/* Field1.func_74900_a: 13x4x9. The constructor draws four crops. */
static int field1_find(struct start *s, int x, int y, int z, int mode, struct bbox *out)
{
    *out = bbox_component_to_add(x, y, z, 0, 0, 0, 13, 4, 9, mode);
    return can_village_go_deeper(out) && start_find_intersecting(s, out) == NULL;
}

static struct piece *field1_new(jrand *rand, struct bbox bb, int mode, int desert)
{
    struct piece *p = vill_new("ViDF", V_FIELD1, desert);
    set_bb(p, bb, mode);
    p->u.village.ca = crop_pick(rand);
    p->u.village.cb = crop_pick(rand);
    p->u.village.cc = crop_pick(rand);
    p->u.village.cd = crop_pick(rand);
    return p;
}

/* Field2.func_74902_a: 7x4x9, two crops. */
static int field2_find(struct start *s, int x, int y, int z, int mode, struct bbox *out)
{
    *out = bbox_component_to_add(x, y, z, 0, 0, 0, 7, 4, 9, mode);
    return can_village_go_deeper(out) && start_find_intersecting(s, out) == NULL;
}

static struct piece *field2_new(jrand *rand, struct bbox bb, int mode, int desert)
{
    struct piece *p = vill_new("ViF", V_FIELD2, desert);
    set_bb(p, bb, mode);
    p->u.village.ca = crop_pick(rand);
    p->u.village.cb = crop_pick(rand);
    return p;
}

/* House2.func_74915_a: 10x6x7. */
static int house2_find(struct start *s, int x, int y, int z, int mode, struct bbox *out)
{
    *out = bbox_component_to_add(x, y, z, 0, 0, 0, 10, 6, 7, mode);
    return can_village_go_deeper(out) && start_find_intersecting(s, out) == NULL;
}

static struct piece *house2_new(struct bbox bb, int mode, int desert)
{
    struct piece *p = vill_new("ViS", V_HOUSE2, desert);
    set_bb(p, bb, mode);
    p->u.village.chest = 0;
    return p;
}

/* House3.func_74921_a: 9x7x12. */
static int house3_find(struct start *s, int x, int y, int z, int mode, struct bbox *out)
{
    *out = bbox_component_to_add(x, y, z, 0, 0, 0, 9, 7, 12, mode);
    return can_village_go_deeper(out) && start_find_intersecting(s, out) == NULL;
}

static struct piece *house3_new(struct bbox bb, int mode, int desert)
{
    struct piece *p = vill_new("ViTRH", V_HOUSE3, desert);
    set_bb(p, bb, mode);
    return p;
}

/* Torch.func_74904_a: 3x4x2, no canVillageGoDeeper test. */
static int torch_find(struct start *s, int x, int y, int z, int mode, struct bbox *out)
{
    *out = bbox_component_to_add(x, y, z, 0, 0, 0, 3, 4, 2, mode);
    return start_find_intersecting(s, out) == NULL;
}

static struct piece *torch_new(struct bbox bb, int mode, int desert)
{
    struct piece *p = vill_new("ViL", V_TORCH, desert);
    set_bb(p, bb, mode);
    return p;
}

/* Path.func_74933_a: try a road of 3 by length for length = 7 * [3,5] down to
 * 7 in steps of 7, the first that fits. Path's constructor then sets
 * averageGroundLevel to max(xsize, zsize). */
static int path_find(struct start *s, jrand *rand, int x, int y, int z, int mode, struct bbox *out)
{
    for (int length = 7 * rand_range(rand, 3, 5); length >= 7; length -= 7)
    {
        struct bbox bb = bbox_component_to_add(x, y, z, 0, 0, 0, 3, 3, length, mode);
        if (start_find_intersecting(s, &bb) == NULL)
        {
            *out = bb;
            return 1;
        }
    }
    return 0;
}

static struct piece *path_new(struct bbox bb, int mode, int desert)
{
    struct piece *p = vill_new("ViSR", V_ROAD, desert);
    set_bb(p, bb, mode);
    int xz = bbox_xsize(&bb);
    int zz = bbox_zsize(&bb);
    p->u.village.length = xz > zz ? xz : zz;
    return p;
}

/* ---- the weighted pick ---- */

static struct piece *get_next_village_component(struct piece *start, struct start *s,
                                                jrand *rand, int x, int y, int z, int mode, int depth);

/* func_75083_a: dispatch on the weight's class. */
static struct piece *class_new(int class_kind, struct start *s,
                               jrand *rand, int x, int y, int z, int mode, int depth)
{
    struct bbox bb;
    int desert = village_world.in_desert;
    struct piece *p = NULL;

    switch (class_kind)
    {
        case V_HOUSE4:
            if (!house4_find(s, rand, x, y, z, mode, &bb)) return NULL;
            p = house4_new(rand, bb, mode, desert);
            break;
        case V_CHURCH:
            if (!church_find(s, x, y, z, mode, &bb)) return NULL;
            p = church_new(bb, mode, desert);
            break;
        case V_HOUSE1:
            if (!house1_find(s, x, y, z, mode, &bb)) return NULL;
            p = house1_new(bb, mode, desert);
            break;
        case V_WOODHUT:
            if (!woodhut_find(s, x, y, z, mode, &bb)) return NULL;
            p = woodhut_new(rand, bb, mode, desert);
            break;
        case V_HALL:
            if (!hall_find(s, x, y, z, mode, &bb)) return NULL;
            p = hall_new(bb, mode, desert);
            break;
        case V_FIELD1:
            if (!field1_find(s, x, y, z, mode, &bb)) return NULL;
            p = field1_new(rand, bb, mode, desert);
            break;
        case V_FIELD2:
            if (!field2_find(s, x, y, z, mode, &bb)) return NULL;
            p = field2_new(rand, bb, mode, desert);
            break;
        case V_HOUSE2:
            if (!house2_find(s, x, y, z, mode, &bb)) return NULL;
            p = house2_new(bb, mode, desert);
            break;
        case V_HOUSE3:
            if (!house3_find(s, x, y, z, mode, &bb)) return NULL;
            p = house3_new(bb, mode, desert);
            break;
        default:
            return NULL;
    }
    /* Each constructor is super(Start, depth): componentType is that depth. */
    p->component_type = depth;
    return p;
}

/* getNextVillageComponent: up to five weighted draws, then a torch as a
 * fallback. The Java list iteration breaks on the first weight whose running
 * total goes negative, and the outer loop then draws again. `last_weight` is
 * the PieceWeight reference the guard compares against; Java keeps it pointing
 * at a removed entry, which is why it is compared by identity here. */
static struct piece *get_next_village_component(struct piece *start, struct start *s,
                                                jrand *rand, int x, int y, int z, int mode, int depth)
{
    int total = weight_sum(start);
    if (total <= 0) return NULL;

    for (int tries = 0; tries < 5; ++tries)
    {
        int pick = jr_int_n(rand, total);
        for (int i = 0; i < start->u.village.nweights; ++i)
        {
            struct village_weight *w = &start->u.village.weights[i];
            pick -= w->weight;
            if (pick < 0)
            {
                /* canSpawnMoreVillagePiecesOfType(depth): a piece whose own
                 * limit is reached, or one that is the last success and the
                 * list still has alternatives, is skipped. */
                if (!can_spawn_more(w)) break;
                if (w == start->u.village.last_weight && start->u.village.nweights > 1) break;

                struct piece *p = class_new(w->class_kind, s, rand, x, y, z, mode, depth);
                if (p != NULL)
                {
                    struct village_weight *taken = w;
                    ++taken->spawned;
                    start->u.village.last_weight = taken;
                    /* canSpawnMoreVillagePieces: drop the weight when its last piece was
                     * just taken. Java's list.remove drops the object from the
                     * list but structVillagePieceWeight still points at it, so
                     * the guard above never matches it again. Keep the pointer
                     * on a copy that is never a list entry. */
                    if (!(taken->limit == 0 || taken->spawned < taken->limit))
                    {
                        int at = (int)(taken - start->u.village.weights);
                        struct village_weight removed = *taken;
                        for (int j = at; j + 1 < start->u.village.nweights; ++j)
                            start->u.village.weights[j] = start->u.village.weights[j + 1];
                        --start->u.village.nweights;
                        start->u.village.removed_weight = removed;
                        start->u.village.last_weight = &start->u.village.removed_weight;
                    }
                    return p;
                }
            }
        }
    }

    {
        struct bbox bb;
        if (torch_find(s, x, y, z, mode, &bb))
        {
            struct piece *t = torch_new(bb, mode, village_world.in_desert);
            t->component_type = depth;
            return t;
        }
    }
    return NULL;
}

/* getNextVillageStructureComponent: depth and distance guards, the biome test
 * at the piece's centre with radius size/2 + 4, then queue on field_74932_i. */
static struct piece *get_next_village_structure(struct piece *start, struct start *s,
                                                jrand *rand, int x, int y, int z, int mode, int depth)
{
    if (depth > 50) return NULL;
    if (!(abs_int(x - start->bb.minX) <= VILLAGE_HOUSE_RANGE
          && abs_int(z - start->bb.minZ) <= VILLAGE_HOUSE_RANGE)) return NULL;

    struct piece *p = get_next_village_component(start, s, rand, x, y, z, mode, depth + 1);
    if (p != NULL)
    {
        int cx = (p->bb.minX + p->bb.maxX) / 2;
        int cz = (p->bb.minZ + p->bb.maxZ) / 2;
        int dx = p->bb.maxX - p->bb.minX;
        int dz = p->bb.maxZ - p->bb.minZ;
        int span = dx > dz ? dx : dz;
        if (are_biomes_viable(cx, cz, span / 2 + 4))
        {
            start_add(s, p);
            /* field_74932_i holds pieces; push the piece the start just took. */
            if (s->n > 0) village_pending_add(&start->u.village.pending_house, s->pieces[s->n - 1]);
            return p;
        }
    }
    return NULL;
}

/* getNextComponentVillagePath: depth guard, distance guard, the road's own
 * find, the y > 10 test, the biome test, then queue on field_74930_j. */
static struct piece *get_next_village_path(struct piece *start, struct start *s,
                                           jrand *rand, int x, int y, int z, int mode, int depth)
{
    if (depth > VILLAGE_PATH_DEPTH + start->u.village.terrain_type) return NULL;
    if (!(abs_int(x - start->bb.minX) <= VILLAGE_HOUSE_RANGE
          && abs_int(z - start->bb.minZ) <= VILLAGE_HOUSE_RANGE)) return NULL;

    struct bbox bb;
    if (path_find(s, rand, x, y, z, mode, &bb) && bb.minY > 10)
    {
        int cx = (bb.minX + bb.maxX) / 2;
        int cz = (bb.minZ + bb.maxZ) / 2;
        int dx = bb.maxX - bb.minX;
        int dz = bb.maxZ - bb.minZ;
        int span = dx > dz ? dx : dz;
        if (are_biomes_viable(cx, cz, span / 2 + 4))
        {
            struct piece *p = path_new(bb, mode, village_world.in_desert);
            p->component_type = depth;
            start_add(s, p);
            village_pending_add(&start->u.village.pending_road, p);
            return p;
        }
    }
    return NULL;
}

/* Village.getNextComponentNN / getNextComponentPP: the four base-mode cases. */
static struct piece *next_nn(struct piece *p, struct piece *start, struct start *s, jrand *rand, int dy, int dz)
{
    struct bbox b = p->bb;
    switch (p->coord_base_mode)
    {
        case 0: return get_next_village_structure(start, s, rand, b.minX - 1, b.minY + dy, b.minZ + dz, 1, p->component_type);
        case 1: return get_next_village_structure(start, s, rand, b.minX + dz, b.minY + dy, b.minZ - 1, 2, p->component_type);
        case 2: return get_next_village_structure(start, s, rand, b.minX - 1, b.minY + dy, b.minZ + dz, 1, p->component_type);
        case 3: return get_next_village_structure(start, s, rand, b.minX + dz, b.minY + dy, b.minZ - 1, 2, p->component_type);
        default: return NULL;
    }
}

static struct piece *next_pp(struct piece *p, struct piece *start, struct start *s, jrand *rand, int dy, int dz)
{
    struct bbox b = p->bb;
    switch (p->coord_base_mode)
    {
        case 0: return get_next_village_structure(start, s, rand, b.maxX + 1, b.minY + dy, b.minZ + dz, 3, p->component_type);
        case 1: return get_next_village_structure(start, s, rand, b.minX + dz, b.minY + dy, b.maxZ + 1, 0, p->component_type);
        case 2: return get_next_village_structure(start, s, rand, b.maxX + 1, b.minY + dy, b.minZ + dz, 3, p->component_type);
        case 3: return get_next_village_structure(start, s, rand, b.minX + dz, b.minY + dy, b.maxZ + 1, 0, p->component_type);
        default: return NULL;
    }
}

static int max_span(const struct bbox *b)
{
    int dx = bbox_xsize(b);
    int dz = bbox_zsize(b);
    return dx > dz ? dx : dz;
}

/* ---- buildComponent ---- */

/* Well.buildComponent: four paths out of the four sides. */
static void well_build(struct piece *p, struct piece *start, struct start *s, jrand *rand)
{
    get_next_village_path(start, s, rand, p->bb.minX - 1, p->bb.maxY - 4, p->bb.minZ + 1, 1, p->component_type);
    get_next_village_path(start, s, rand, p->bb.maxX + 1, p->bb.maxY - 4, p->bb.minZ + 1, 3, p->component_type);
    get_next_village_path(start, s, rand, p->bb.minX + 1, p->bb.maxY - 4, p->bb.minZ - 1, 2, p->component_type);
    get_next_village_path(start, s, rand, p->bb.minX + 1, p->bb.maxY - 4, p->bb.maxZ + 1, 0, p->component_type);
}

/* Path.buildComponent: houses down both sides, then possibly a road off each
 * end. Note the guarding boolean: a side that produced any house bumps the
 * cursor by that house's span and sets `placed`. */
static void path_build(struct piece *p, struct piece *start, struct start *s, jrand *rand)
{
    int placed = 0;
    int i;
    struct piece *c;

    for (i = jr_int_n(rand, 5); i < p->u.village.length - 8; i += 2 + jr_int_n(rand, 5))
    {
        c = next_nn(p, start, s, rand, 0, i);
        if (c != NULL)
        {
            i += max_span(&c->bb);
            placed = 1;
        }
    }

    for (i = jr_int_n(rand, 5); i < p->u.village.length - 8; i += 2 + jr_int_n(rand, 5))
    {
        c = next_pp(p, start, s, rand, 0, i);
        if (c != NULL)
        {
            i += max_span(&c->bb);
            placed = 1;
        }
    }

    if (placed && jr_int_n(rand, 3) > 0)
    {
        switch (p->coord_base_mode)
        {
            case 0: get_next_village_path(start, s, rand, p->bb.minX - 1, p->bb.minY, p->bb.maxZ - 2, 1, p->component_type); break;
            case 1: get_next_village_path(start, s, rand, p->bb.minX, p->bb.minY, p->bb.minZ - 1, 2, p->component_type); break;
            case 2: get_next_village_path(start, s, rand, p->bb.minX - 1, p->bb.minY, p->bb.minZ, 1, p->component_type); break;
            case 3: get_next_village_path(start, s, rand, p->bb.maxX - 2, p->bb.minY, p->bb.minZ - 1, 2, p->component_type); break;
            default: break;
        }
    }

    if (placed && jr_int_n(rand, 3) > 0)
    {
        switch (p->coord_base_mode)
        {
            case 0: get_next_village_path(start, s, rand, p->bb.maxX + 1, p->bb.minY, p->bb.maxZ - 2, 3, p->component_type); break;
            case 1: get_next_village_path(start, s, rand, p->bb.minX, p->bb.minY, p->bb.maxZ + 1, 0, p->component_type); break;
            case 2: get_next_village_path(start, s, rand, p->bb.maxX + 1, p->bb.minY, p->bb.minZ, 3, p->component_type); break;
            case 3: get_next_village_path(start, s, rand, p->bb.maxX - 2, p->bb.minY, p->bb.maxZ + 1, 0, p->component_type); break;
            default: break;
        }
    }
}

void village_build_component(struct piece *p, struct piece *parent, struct start *s, jrand *rand)
{
    if (p->u.village.kind == V_WELL) well_build(p, parent, s, rand);
    else if (p->u.village.kind == V_ROAD) path_build(p, parent, s, rand);
}

/* ---- MapGenVillage ---- */

static int village_can_spawn(jrand *rand, int cx, int cz)
{
    int want_x = cx;
    int want_z = cz;

    if (cx < 0) cx -= VILLAGE_MAX_DISTANCE - 1;
    if (cz < 0) cz -= VILLAGE_MAX_DISTANCE - 1;

    int gx = cx / VILLAGE_MAX_DISTANCE;
    int gz = cz / VILLAGE_MAX_DISTANCE;

    /* this.worldObj.setRandomSeed(gx, gz, 10387312) returns World.rand itself, so
     * the reseed and the two draws below land in the world's stream when the
     * caller has one (populate.h) */
    jrand local;
    jrand *world_rand = populate_world_rand != NULL ? populate_world_rand : &local;
    set_random_seed(world_rand, gx, gz, VILLAGE_SALT);

    gx *= VILLAGE_MAX_DISTANCE;
    gz *= VILLAGE_MAX_DISTANCE;
    gx += jr_int_n(world_rand, VILLAGE_MAX_DISTANCE - VILLAGE_MIN_DISTANCE);
    gz += jr_int_n(world_rand, VILLAGE_MAX_DISTANCE - VILLAGE_MIN_DISTANCE);

    if (want_x != gx || want_z != gz) return 0;
    return are_biomes_viable(want_x * 16 + 8, want_z * 16 + 8, 0);
}

/* MapGenVillage.Start: the weighted list, the well (which is the start's first
 * component), the well's own buildComponent, then the alternating pending
 * queues, then the "Valid" flag. */
static void village_make_start(jrand *rand, int cx, int cz, struct start *s)
{
    struct piece *start = vill_new("ViStart", V_WELL, 0);
    start->u.village.terrain_type = VILLAGE_TERRAIN_TYPE;

    weighted_piece_list(start, rand, VILLAGE_TERRAIN_TYPE);

    /* StructureVillagePieces.Start(WorldChunkManager, 0, rand, x, z, list,
     * terrainType): the Well constructor draws coordBaseMode first, then the
     * Start constructor reads the biome at the centre ((cx << 4) + 2). The
     * super call passes a null Start, so the well's own field_143014_b stays
     * false; only the pieces built from the Start read inDesert. */
    int mode = jr_int_n(rand, 4);
    int x = (cx << 4) + 2;
    int z = (cz << 4) + 2;
    int biome = biome_gen_at(x, z);
    start->u.village.desert = 0;
    village_world.in_desert = (biome == 2 || biome == 17);   /* desert, desertHills */
    start->bb = bbox_make(x, 64, z, x + 6 - 1, 78, z + 6 - 1);
    start->coord_base_mode = mode;
    start_add(s, start);

    /* var7.buildComponent(var7, this.components, rand). */
    piece_build_component(start, start, s, rand);

    /* Alternate: while either queue is non-empty, take a random element from
     * the roads first if any, otherwise from the houses. */
    while (start->u.village.pending_road.n > 0 || start->u.village.pending_house.n > 0)
    {
        struct piece *p;
        if (start->u.village.pending_road.n == 0)
        {
            int i = jr_int_n(rand, start->u.village.pending_house.n);
            p = start->u.village.pending_house.v[i];
            for (int j = i; j + 1 < start->u.village.pending_house.n; ++j)
                start->u.village.pending_house.v[j] = start->u.village.pending_house.v[j + 1];
            --start->u.village.pending_house.n;
        }
        else
        {
            int i = jr_int_n(rand, start->u.village.pending_road.n);
            p = start->u.village.pending_road.v[i];
            for (int j = i; j + 1 < start->u.village.pending_road.n; ++j)
                start->u.village.pending_road.v[j] = start->u.village.pending_road.v[j + 1];
            --start->u.village.pending_road.n;
        }
        piece_build_component(p, start, s, rand);
    }

    start_update_bb(s);

    /* hasMoreThanTwoComponents: count the components that are not roads. */
    int non_road = 0;
    for (int i = 0; i < s->n; ++i)
        if (!(s->pieces[i]->kind == PIECE_VILLAGE && s->pieces[i]->u.village.kind == V_ROAD)) ++non_road;
    start->u.village.valid = non_road > 2 ? 1 : 0;
}

const struct structure_type structure_village = {
    "Village",
    village_begin,
    village_can_spawn,
    village_make_start
};