/* MapGenScatteredFeature and its three components. Every Random draw is in
 * Java's order. The spawn test does not use the seam's per-candidate Random at
 * all: vanilla draws from the world's own Random, reseeded through
 * World.setRandomSeed(v, w, 14357617), so this file keeps its own jrand and
 * seeds it the way setRandomSeed does. The biome test reads the world's
 * WorldChunkManager at the chunk centre (x * 16 + 8, z * 16 + 8); the walk
 * hands the world seed to begin(), and the biome comes from the full-resolution
 * (voronoi) layer stack, which is what getBiomeGenAt serves.
 *
 * The three components are single pieces with no children of their own, so
 * buildComponent is the base class's empty body. */
#include "temple.h"
#include "env.h"

#include <stdlib.h>

#include "biomes.h"
#include "jrand.h"
#include "layers.h"

/* MapGenScatteredFeature's fields, world.conf's "distance" defaults. */
#define TEMPLE_MAX_DISTANCE 32
#define TEMPLE_MIN_DISTANCE 8

/* canSpawnStructureAtCoords's salt. */
#define TEMPLE_SALT 14357617

/* The biome ids of biomelist, as BiomeGenBase.func_150568_d(biomeID) resolves
 * them: desert, desertHills, jungle, jungleHills, swampland. The comparison in
 * Java is on the biome object, so the object id (161 aliases 160) is what
 * matters here. */
static const int temple_biomes[] = {
    /* Desert */ 2,
    /* DesertHills */ 17,
    /* Jungle */ 21,
    /* JungleHills */ 22,
    /* Swampland */ 6,
};

/* Set once per walk by begin(). The spawn test is a pure function of the world
 * seed and the candidate chunk, so one struct of walk state is enough. */
#define temple_world (nw_env->temple)

void temple_begin(int64_t seed)
{
    temple_world.seed = seed;
    layers_init(&temple_world.layers, seed);
}

/* World.setRandomSeed(x, y, salt): seed = x * 341873128712 + y * 132897987541
 * + world seed + salt, into the world's Random. Java computes the three terms
 * in long arithmetic and adds them left to right, all wrapping. */
static void set_random_seed(jrand *r, int x, int y, int salt)
{
    uint64_t v = (uint64_t)(int64_t)x * 341873128712ULL
               + (uint64_t)(int64_t)y * 132897987541ULL
               + (uint64_t)temple_world.seed
               + (uint64_t)(int64_t)salt;
    jr_seed(r, (int64_t)v);
}

/* The world's biome at a block position, through
 * WorldChunkManager.getBiomeGenAt -> BiomeCache -> biomeIndexLayer (the
 * voronoi layer). Returns the object id, matching the registry's aliasing. */
static int biome_gen_at(int x, int z)
{
    int id;
    biomes_full(&temple_world.layers, x, z, 1, 1, &id);
    return id;
}

static int biome_listed(int id)
{
    for (size_t i = 0; i < sizeof temple_biomes / sizeof temple_biomes[0]; ++i)
        if (temple_biomes[i] == id) return 1;
    return 0;
}

/* canSpawnStructureAtCoords. The two -1 adjustments on negative chunks, the
 * integer division of the chunk by the spacing, the two nextInt draws on the
 * world Random, and the biome test at the chunk centre. */
static int temple_can_spawn(jrand *rand, int cx, int cz)
{
    int want_x = cx;
    int want_z = cz;

    if (cx < 0) cx -= TEMPLE_MAX_DISTANCE - 1;
    if (cz < 0) cz -= TEMPLE_MAX_DISTANCE - 1;

    int gx = cx / TEMPLE_MAX_DISTANCE;
    int gz = cz / TEMPLE_MAX_DISTANCE;

    /* this.worldObj.setRandomSeed(gx, gz, 14357617) returns World.rand itself, so
     * the reseed and the two draws below land in the world's stream when the
     * caller has one (populate.h) */
    jrand local;
    jrand *world_rand = populate_world_rand != NULL ? populate_world_rand : &local;
    set_random_seed(world_rand, gx, gz, TEMPLE_SALT);

    gx *= TEMPLE_MAX_DISTANCE;
    gz *= TEMPLE_MAX_DISTANCE;
    gx += jr_int_n(world_rand, TEMPLE_MAX_DISTANCE - TEMPLE_MIN_DISTANCE);
    gz += jr_int_n(world_rand, TEMPLE_MAX_DISTANCE - TEMPLE_MIN_DISTANCE);

    if (want_x != gx || want_z != gz) return 0;
    return biome_listed(biome_gen_at(want_x * 16 + 8, want_z * 16 + 8));
}

/* ComponentScatteredFeaturePieces.Feature(Random, x, y, z, sizeX, sizeY,
 * sizeZ): coordBaseMode = nextInt(4), and the box is sizeX by sizeZ for the
 * two modes where the piece is not rotated, sizeZ by sizeX otherwise. */
static struct bbox feature_box(jrand *rand, int x, int y, int z,
                               int size_x, int size_y, int size_z, int *mode)
{
    *mode = jr_int_n(rand, 4);
    switch (*mode)
    {
        case 0:
        case 2:
            return bbox_make(x, y, z, x + size_x - 1, y + size_y - 1, z + size_z - 1);
        default:
            return bbox_make(x, y, z, x + size_z - 1, y + size_y - 1, z + size_x - 1);
    }
}

/* One temple component. Its `component_type` is StructureComponent(0)'s 0. */
static struct piece *temple_piece_new(enum temple_kind kind, const char *id, jrand *rand,
                                      int x, int y, int z, int size_x, int size_y, int size_z)
{
    struct piece *p = calloc(1, sizeof *p);
    if (!p) abort();
    p->id = id;
    p->kind = PIECE_TEMPLE;
    p->component_type = 0;
    p->bb = feature_box(rand, x, y, z, size_x, size_y, size_z, &p->coord_base_mode);
    p->u.temple.kind = kind;
    p->u.temple.size_x = size_x;
    p->u.temple.size_y = size_y;
    p->u.temple.size_z = size_z;
    p->u.temple.hpos = -1;   /* Feature.field_74936_d */
    return p;
}

/* MapGenScatteredFeature.Start: pick the component by the biome at the chunk
 * centre and add it, then updateBoundingBox. No markAvailableHeight: the
 * temple start's height is settled later, during population, by
 * Feature.func_74935_a. */
static void temple_make_start(jrand *rand, int cx, int cz, struct start *s)
{
    int x = cx * 16;
    int z = cz * 16;
    int biome = biome_gen_at(cx * 16 + 8, cz * 16 + 8);
    struct piece *p;

    if (biome == 21 || biome == 22)          /* BiomeGenBase.jungle, jungleHills */
        p = temple_piece_new(TEMPLE_JUNGLE_PYRAMID, "TeJP", rand, x, 64, z, 12, 10, 15);
    else if (biome == 6)                     /* BiomeGenBase.swampland */
        p = temple_piece_new(TEMPLE_SWAMP_HUT, "TeSH", rand, x, 64, z, 7, 5, 9);
    else
        p = temple_piece_new(TEMPLE_DESERT_PYRAMID, "TeDP", rand, x, 64, z, 21, 15, 21);

    start_add(s, p);
    start_update_bb(s);
}

const struct structure_type structure_temple = {
    "Temple",
    temple_begin,
    temple_can_spawn,
    temple_make_start
};