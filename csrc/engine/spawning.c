/* SpawnerAnimals, bit for bit: findChunksForSpawning (the eligible chunk set
 * in its Java HashMap order, the per-type caps over the counted records, the
 * pack walk's Random draws, getCanSpawnHere per type with the light and block
 * rules) and performWorldGenSpawning. The spawned mobs are constructed records
 * the recording carries: class, position, the Det draws already spent,
 * onSpawnWithEgg's equipment. The mob lanes tick them; here they are never
 * ticked, so every box stands where it spawned, exactly as the oracle's runs
 * never tick their entities either.
 *
 * World.rand is the driver's stream (the servertick's, or the populate
 * driver's); the entity Randoms are det_rngs seeded from Det's per-role
 * seeder. Java evaluates operands left to right, so every draw that shares an
 * expression with another is its own statement here. */
#include "spawning.h"
#include "jmath.h"
#include "hostiles_spider.h"
#include "jorder.h"
#include "env.h"
#include "populate_nether.h"
#include "entity.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "blocks.h"
#include "enchant.h"
#include "populate.h"
#include "trace.h"

/* World.getBlock and getChunkFromChunkCoords ask ChunkProviderServer to load
 * a missing chunk. Natural spawning can cross the loaded edge even while the
 * player stands still; that load can populate terrain and reseed World.rand
 * (recreateStructures). The reads vanilla guards with chunkExists or
 * blockExists (getSavedLightValue, getBiomeGenForCoords, the local difficulty)
 * stay on world_chunk and never load. */
static void sp_ensure(struct spawner *s, int x, int z)
{
    if (s->in_tick && s->ensure_chunk && !world_chunk_near(s->w, x >> 4, z >> 4) && !world_chunk(s->w, x >> 4, z >> 4))
        s->ensure_chunk(s->ensure_ctx, x >> 4, z >> 4);
}

static struct chunk *sp_chunk(struct spawner *s, int cx, int cz)
{
    sp_ensure(s, cx * 16, cz * 16);
    return world_chunk(s->w, cx, cz);
}

/* the loaded chunk a read at (x, z) lands in after sp_ensure, NULL when there
 * is none (World.getBlock's own path then answers) */
static const struct chunk *sp_read_chunk(struct spawner *s, int x, int z)
{
    if (!(x >= -30000000 && x < 30000000 && z >= -30000000 && z < 30000000))
    {
        sp_ensure(s, x, z);
        return NULL;
    }

    const struct chunk *c = world_chunk_near(s->w, x >> 4, z >> 4);

    if (c != NULL) return c;
    sp_ensure(s, x, z);
    return world_chunk(s->w, x >> 4, z >> 4);
}

static int sp_block(struct spawner *s, int x, int y, int z)
{
    /* World.getBlock answers air above and below the world without a chunk */
    if (y < 0 || y >= 256) return 0;

    const struct chunk *c = sp_read_chunk(s, x, z);

    return c != NULL ? chunk_get_block(c, x & 15, y, z & 15) : world_get_block(s->w, x, y, z);
}

static int sp_meta(struct spawner *s, int x, int y, int z)
{
    if (y < 0 || y >= 256) return 0;

    const struct chunk *c = sp_read_chunk(s, x, z);

    return c != NULL ? chunk_get_meta(c, x & 15, y, z & 15) : world_get_meta(s->w, x, y, z);
}



/* Block ids the rules name. */
enum {
    SP_BEDROCK = 7,
    SP_GRASS = 2,
    SP_SLAB_STONE = 44, SP_SLAB_WOOD = 126,
    SP_HOPPER = 154,
    SP_SNOW_LAYER = 78,
};

/* --------------------------------------------------------------- the lists */

/* The spawn lists, BiomeGenBase's base lists after WorldConf.pruneSpawns took
 * the horses, wolves and ocelots out, plus the subclasses' clears: mushroom
 * island (and the shore, which shares its registry object) carries mooshroom
 * only, the oceans, rivers and stone beach no creatures, everything else the
 * base rows. */
static struct sp_list lists[CT_TYPES];
static struct sp_list mush_creature;
static struct sp_list jungle_creature;
static struct sp_list swamp_monster;
static struct sp_list empty_list;
static struct sp_list scattered_witch;

/* before main: the same for every environment */
__attribute__((constructor)) static void build_lists(void)
{
    static const int mk[7] = { SP_SPIDER, SP_ZOMBIE, SP_SKELETON, SP_CREEPER, SP_SLIME, SP_ENDERMAN, SP_WITCH };
    static const int mw[7] = { 100, 100, 100, 100, 100, 10, 5 };
    static const int mn[7] = { 4, 4, 4, 4, 4, 1, 1 };
    static const int mm[7] = { 4, 4, 4, 4, 4, 4, 1 };

    lists[CT_MONSTER].n = 7;

    for (int i = 0; i < 7; ++i)
    {
        lists[CT_MONSTER].kind[i] = mk[i];
        lists[CT_MONSTER].weight[i] = mw[i];
        lists[CT_MONSTER].min_group[i] = mn[i];
        lists[CT_MONSTER].max_group[i] = mm[i];
    }

    static const int ck[4] = { SP_SHEEP, SP_PIG, SP_CHICKEN, SP_COW };
    static const int cw[4] = { 12, 10, 10, 8 };

    lists[CT_CREATURE].n = 4;

    for (int i = 0; i < 4; ++i)
    {
        lists[CT_CREATURE].kind[i] = ck[i];
        lists[CT_CREATURE].weight[i] = cw[i];
        lists[CT_CREATURE].min_group[i] = 4;
        lists[CT_CREATURE].max_group[i] = 4;
    }

    lists[CT_AMBIENT].n = 1;
    lists[CT_AMBIENT].kind[0] = SP_BAT;
    lists[CT_AMBIENT].weight[0] = 10;
    lists[CT_AMBIENT].min_group[0] = 8;
    lists[CT_AMBIENT].max_group[0] = 8;

    lists[CT_WATER].n = 1;
    lists[CT_WATER].kind[0] = SP_SQUID;
    lists[CT_WATER].weight[0] = 10;
    lists[CT_WATER].min_group[0] = 4;
    lists[CT_WATER].max_group[0] = 4;

    mush_creature.n = 1;
    mush_creature.kind[0] = SP_MOOSHROOM;
    mush_creature.weight[0] = 8;
    mush_creature.min_group[0] = 4;
    mush_creature.max_group[0] = 8;

    /* BiomeGenJungle adds a second chicken row (the ocelot row the pruning
     * removed went with it); the mutated jungle ids copy the base's lists */
    jungle_creature.n = 5;

    for (int i = 0; i < 4; ++i)
    {
        jungle_creature.kind[i] = lists[CT_CREATURE].kind[i];
        jungle_creature.weight[i] = lists[CT_CREATURE].weight[i];
        jungle_creature.min_group[i] = 4;
        jungle_creature.max_group[i] = 4;
    }

    jungle_creature.kind[4] = SP_CHICKEN;
    jungle_creature.weight[4] = 10;
    jungle_creature.min_group[4] = 4;
    jungle_creature.max_group[4] = 4;

    /* BiomeGenSwamp adds a slime row after the base monsters (weight 1, one
     * slime); Swampland M copies the swamp's lists */
    swamp_monster = lists[CT_MONSTER];
    swamp_monster.kind[7] = SP_SLIME;
    swamp_monster.weight[7] = 1;
    swamp_monster.min_group[7] = 1;
    swamp_monster.max_group[7] = 1;
    swamp_monster.n = 8;

    empty_list.n = 0;

    /* MapGenScatteredFeature's constructor: the witch list every swamp hut
     * carries (EntityWitch, weight 1, one mob, one mob). */
    scattered_witch.n = 1;
    scattered_witch.kind[0] = SP_WITCH;
    scattered_witch.weight[0] = 1;
    scattered_witch.min_group[0] = 1;
    scattered_witch.max_group[0] = 1;

}

/* The witch list a swamp hut's ChunkProviderGenerate.getPossibleCreatures
 * hands the spawner: only monsters, only inside the hut's box. */
const struct sp_list *spawning_scattered_witch(void)
{
    return &scattered_witch;
}

/* The biomes whose spawnableCreatureList a subclass cleared: the oceans,
 * rivers and stone beach (BiomeGenBase), the two deserts, the two beaches,
 * the three mesas, the two snow biomes (the wolf rows the pruning would have
 * removed are gone with the whole list), hell, the end and the mushroom
 * island pair (which shares its registry object and carries mooshroom only),
 * and the mutations that copy a cleared list (BiomeGenMutated takes its
 * base's lists): Desert M, Ice Plains Spikes and the three mesa mutations. */
static int biome_has_creatures(int biome)
{
    switch (biome)
    {
    case 0:   /* ocean */
    case 2:   /* desert */
    case 7:   /* river */
    case 8:   /* hell */
    case 9:   /* the end */
    case 10:  /* frozen ocean */
    case 11:  /* frozen river */
    case 12:  /* ice plains */
    case 13:  /* ice mountains */
    case 14:  /* mushroom island */
    case 15:  /* mushroom island shore */
    case 16:  /* beach */
    case 17:  /* desert hills */
    case 24:  /* deep ocean */
    case 25:  /* stone beach */
    case 26:  /* cold beach */
    case 37:  /* mesa */
    case 38:  /* mesa plateau F */
    case 39:  /* mesa plateau */
    case 130: /* desert M */
    case 140: /* ice plains spikes */
    case 165: /* mesa (bryce) */
    case 166: /* mesa plateau F M */
    case 167: /* mesa plateau M */
        return 0;
    default:
        return 1;
    }
}

static int biome_is_mushroom(int biome)
{
    return biome == 14 || biome == 15;
}

/* BiomeGenHell clears every list and adds its three monsters; BiomeGenEnd
 * clears them and adds the enderman. */
static const struct sp_list hell_monster = {
    3, {SP_GHAST, SP_PIG_ZOMBIE, SP_MAGMA_CUBE}, {50, 100, 1}, {4, 4, 4}, {4, 4, 4}
};
static const struct sp_list end_monster = {
    1, {SP_ENDERMAN}, {10}, {4}, {4}
};

/* MapGenNetherBridge's spawnList, in its add order. */
static const struct sp_list fortress_monster = {
    4, {SP_BLAZE, SP_PIG_ZOMBIE, SP_SKELETON, SP_MAGMA_CUBE}, {10, 5, 10, 3}, {2, 4, 4, 4}, {3, 4, 4, 4}
};

const struct sp_list *spawning_hell_list(struct populate *p, int type, int x, int y, int z,
                                         int below, int *via)
{
    int f = type == CT_MONSTER ? populate_hell_fortress_at(p, x, y, z, below) : 0;

    if (via != NULL) *via = f;
    if (f != 0) return &fortress_monster;

    return spawning_list_for(8, type);
}

const struct sp_list *spawning_list_for(int biome, int type)
{

    if (biome == 8) return type == CT_MONSTER ? &hell_monster : &empty_list;
    if (biome == 9) return type == CT_MONSTER ? &end_monster : &empty_list;

    /* the swamp and Swampland M carry the extra slime row */
    if (type == CT_MONSTER && (biome == 6 || biome == 134)) return &swamp_monster;

    if (biome_is_mushroom(biome))
    {
        /* BiomeGenMushroomIsland clears the monster, creature and water
         * lists (the shore shares its registry object) and carries
         * mooshroom only; the cave (ambient bat) list is kept */
        if (type == CT_CREATURE) return &mush_creature;

        if (type == CT_MONSTER || type == CT_WATER) return &empty_list;

        return &lists[type];
    }

    if (type == CT_CREATURE)
    {
        /* the jungles carry the extra chicken row */
        if (biome == 21 || biome == 22 || biome == 23 || biome == 149 || biome == 151)
            return &jungle_creature;

        if (!biome_has_creatures(biome)) return &empty_list;
    }

    return &lists[type];
}

/* The EnumCreatureType a kind is an instanceof. */
static int type_of_kind(int kind)
{
    if (kind == SP_CRYSTAL || kind == SP_OTHER) return -1;
    if (kind == SP_BAT) return CT_AMBIENT;
    if (kind == SP_SQUID) return CT_WATER;
    if (kind == SP_SHEEP || kind == SP_PIG || kind == SP_CHICKEN || kind == SP_COW ||
        kind == SP_MOOSHROOM) return CT_CREATURE;
    return CT_MONSTER;
}

/* ------------------------------------------------------- light and blocks */

static float clamp_float(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Chunk.canBlockSeeTheSky on the chunk's own column. */
static int chunk_see_sky(const struct chunk *c, int lx, int y, int lz)
{
    return y >= c->height[lz << 4 | lx];
}

/* Chunk.getSavedLightValue. */
static int chunk_saved_light(const struct chunk *c, int type, int lx, int y, int lz)
{
    if (!((c->mask >> (y >> 4)) & 1))
        return chunk_see_sky(c, lx, y, lz) ? (type == LIGHT_SKY ? 15 : 0) : 0;

    int idx = lx << 12 | lz << 8 | y;

    return type == LIGHT_SKY ? (c->no_sky ? 0 : chunk_cell_sky(c, idx)) : chunk_cell_blocklight(c, idx);
}

/* World.getSavedLightValue: the default light value where the chunk is not
 * loaded. */
static int saved_light(struct spawner *s, int type, int x, int y, int z)
{
    if (y < 0) y = 0;
    if (y >= 256) y = 255;

    if (x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000)
        return type == LIGHT_SKY ? 15 : 0;

    /* chunkExists first: this read never loads */
    struct chunk *c = world_chunk(s->w, x >> 4, z >> 4);

    if (c == NULL) return type == LIGHT_SKY ? 15 : 0;

    return chunk_saved_light(c, type, x & 15, y, z & 15);
}

/* Chunk.getBlockLightValue(x, y, z, subtracted). A missing section answers
 * the sky default less the subtraction, but 0 under a provider with
 * hasNoSky (the Nether, the End). */
static int chunk_block_light(const struct chunk *c, int lx, int y, int lz, int subtracted)
{
    if (!((c->mask >> (y >> 4)) & 1)) return !c->no_sky && subtracted < 15 ? 15 - subtracted : 0;

    int idx = lx << 12 | lz << 8 | y;
    int v = c->no_sky ? 0 : chunk_cell_sky(c, idx);

    v -= subtracted;

    int b = chunk_cell_blocklight(c, idx);

    return b > v ? b : v;
}

/* The default empty chunk's Chunk.getBlockLightValue: every section missing. */
static int empty_chunk_light(int subtracted)
{
    return subtracted < 15 ? 15 - subtracted : 0;
}

/* Block.func_149710_n, Block.registerBlocks' loop: non-air blocks with a
 * render type of 10, or a slab, or grass, or a material that blocks grass, or
 * zero light opacity. */
static int use_neighbor(int id)
{
    const struct block_def *b = &BLOCKS[id & 4095];
    int m = b->material;

    if (m == 0) return 0;
    if (b->opacity == 0) return 1;
    if (b->render_type == 10) return 1;
    if (id == SP_GRASS) return 1;
    if (id == SP_SLAB_STONE || id == SP_SLAB_WOOD) return 1;
    if (m == 5 || m == 14 || m == 17 || m == 19 || m == 20 || m == 26 || m == 31) return 1;

    return 0;
}

/* World.getBlockLightValue_do with check false: the stored value. The check
 * reads its five neighbours through this, so it does not call itself. */
static int block_light_raw(struct spawner *s, int x, int y, int z)
{
    if (x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000) return 15;

    if (y < 0) return 0;
    if (y >= 256) y = 255;

    struct chunk *c = sp_chunk(s, x >> 4, z >> 4);

    if (c == NULL) return empty_chunk_light(s->skylight_subtracted);

    return chunk_block_light(c, x & 15, y, z & 15, s->skylight_subtracted);
}

/* World.getBlockLightValue_do(x, y, z, check). */
static int block_light_do(struct spawner *s, int x, int y, int z, int check)
{
    if (x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000) return 15;

    if (check && use_neighbor(sp_block(s, x, y, z)))
    {
        int v = block_light_raw(s, x, y + 1, z);
        int a;

        a = block_light_raw(s, x + 1, y, z);
        if (a > v) v = a;
        a = block_light_raw(s, x - 1, y, z);
        if (a > v) v = a;
        a = block_light_raw(s, x, y, z + 1);
        if (a > v) v = a;
        a = block_light_raw(s, x, y, z - 1);
        if (a > v) v = a;

        return v;
    }

    return block_light_raw(s, x, y, z);
}

/* World.getBlockLightValue. */
static int block_light_value(struct spawner *s, int x, int y, int z)
{
    return block_light_do(s, x, y, z, 1);
}

/* The tile spawner's getCanSpawnHere (serverreplay.c) reads the same light
 * queries at the world's current skylightSubtracted. */
int spawner_saved_light(struct spawner *s, int type, int x, int y, int z)
{
    return saved_light(s, type, x, y, z);
}

int spawner_block_light_value(struct spawner *s, int x, int y, int z)
{
    return block_light_do(s, x, y, z, 1);
}

/* WorldProvider.lightBrightnessTable: the Nether's generateLightBrightnessTable
 * lifts every entry by its 0.1F ambient. */
float spawner_brightness(int dim, int light)
{
    float f = 1.0F - (float)light / 15.0F;
    float ambient = dim == -1 ? 0.1F : 0.0F;

    return (1.0F - f) / (f * 3.0F + 1.0F) * (1.0F - ambient) + ambient;
}

/* World.getFullBlockLightValue. */
static int full_block_light(struct spawner *s, int x, int y, int z)
{
    if (y < 0) return 0;
    if (y >= 256) y = 255;

    struct chunk *c = sp_chunk(s, x >> 4, z >> 4);

    if (c == NULL) return empty_chunk_light(0);

    return chunk_block_light(c, x & 15, y, z & 15, 0);
}

/* -------------------------------------------------------------- the queries */

/* World.checkNoEntityCollision(box): every entity whose box meets it and that
 * prevents spawning. */
/* Whether World.getEntitiesWithinAABBExcludingEntity(null, b) reaches the
 * entity through its chunk list: the chunks from (min - 2) / 16 to
 * (max + 2) / 16, the sections likewise (clamped to the column). */
static int box_reached(const struct sp_box *o, const struct aabb *b)
{
    if (!o->has_home) return 1;
    int cx0 = mh_floor((b->min_x - 2.0) / 16.0), cx1 = mh_floor((b->max_x + 2.0) / 16.0);
    int cz0 = mh_floor((b->min_z - 2.0) / 16.0), cz1 = mh_floor((b->max_z + 2.0) / 16.0);
    int cy0 = mh_floor((b->min_y - 2.0) / 16.0), cy1 = mh_floor((b->max_y + 2.0) / 16.0);
    if (cy0 < 0) cy0 = 0;
    if (cy1 > 15) cy1 = 15;
    int hy = o->hcy < 0 ? 0 : o->hcy > 15 ? 15 : o->hcy;
    return o->hcx >= cx0 && o->hcx <= cx1 && o->hcz >= cz0 && o->hcz <= cz1 && hy >= cy0 && hy <= cy1;
}

static int no_entity_collision(struct spawner *s, const struct aabb *b)
{
    for (int i = 0; i < s->nboxes; ++i)
    {
        const struct sp_box *o = &s->boxes[i];

        if (!o->prevent || !box_reached(o, b)) continue;

        if (o->max_x > b->min_x && o->min_x < b->max_x &&
            o->max_y > b->min_y && o->min_y < b->max_y &&
            o->max_z > b->min_z && o->min_z < b->max_z) return 0;
    }

    if (s->blocked != NULL && s->blocked(s->blocked_ctx, b)) return 0;

    return 1;
}

/* World.getCollidingBoundingBoxes(entity, box).isEmpty(): the block scan, then
 * the entity boxes the 0.25-expanded selection picked, tested against the
 * unexpanded box. */
static int colliding_empty(struct spawner *s, const struct aabb *b)
{
    if (!world_colliding_boxes_empty(s->w, *b)) return 0;

    double ex0 = b->min_x - 0.25, ey0 = b->min_y - 0.25, ez0 = b->min_z - 0.25;
    double ex1 = b->max_x + 0.25, ey1 = b->max_y + 0.25, ez1 = b->max_z + 0.25;

    for (int i = 0; i < s->nboxes; ++i)
    {
        const struct sp_box *o = &s->boxes[i];

        if (!box_reached(o, b)) continue;
        if (o->max_x <= ex0 || o->min_x >= ex1) continue;
        if (o->max_y <= ey0 || o->min_y >= ey1) continue;
        if (o->max_z <= ez0 || o->min_z >= ez1) continue;

        if (o->max_x > b->min_x && o->min_x < b->max_x &&
            o->max_y > b->min_y && o->min_y < b->max_y &&
            o->max_z > b->min_z && o->min_z < b->max_z) return 0;
    }

    return 1;
}

/* World.isAnyLiquid(box). */
static int any_liquid(struct spawner *s, const struct aabb *b)
{
    int x0 = mh_floor(b->min_x), x1 = mh_floor(b->max_x + 1.0);
    int y0 = mh_floor(b->min_y), y1 = mh_floor(b->max_y + 1.0);
    int z0 = mh_floor(b->min_z), z1 = mh_floor(b->max_z + 1.0);

    /* the three decrements on a negative minimum: one more column read (and
     * possibly one more chunk loaded) on the negative side */
    if (b->min_x < 0.0) --x0;
    if (b->min_y < 0.0) --y0;
    if (b->min_z < 0.0) --z0;

    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
            {
                int id = sp_block(s, x, y, z) & 4095;

                if (MATERIALS[BLOCKS[id].material].is_liquid) return 1;
            }

    return 0;
}

/* World.doesBlockHaveSolidTopSurface. */
static int solid_top_surface(struct spawner *s, int x, int y, int z)
{
    int id = sp_block(s, x, y, z) & 4095;
    int meta = sp_meta(s, x, y, z);
    const struct block_def *b = &BLOCKS[id];

    if (MATERIALS[b->material].is_opaque && b->normal_block) return 1;

    switch (id)
    {
    case 43: case 44: case 126: return (meta & 8) == 8;   /* BlockSlab */
    case 154: return 1;                                   /* BlockHopper */
    case 78: return (meta & 7) == 7;                      /* BlockSnow */
    case 109: case 128: case 134: case 135: case 136: case 156:
    case 163: case 164: case 180: case 181: case 182:
        return (meta & 4) == 4;                           /* BlockStairs */
    default:
        return 0;
    }
}

/* doesBlockHaveSolidTopSurface on a block id and metadata */
static int solid_top_id(int id, int meta)
{
    const struct block_def *b = &BLOCKS[id];

    if (MATERIALS[b->material].is_opaque && b->normal_block) return 1;

    switch (id)
    {
    case 43: case 44: case 126: return (meta & 8) == 8;   /* BlockSlab */
    case 154: return 1;                                   /* BlockHopper */
    case 78: return (meta & 7) == 7;                      /* BlockSnow */
    case 109: case 128: case 134: case 135: case 136: case 156:
    case 163: case 164: case 180: case 181: case 182:
        return (meta & 4) == 4;                           /* BlockStairs */
    default:
        return 0;
    }
}

static int can_spawn_at_location_slow(struct spawner *s, int type, int x, int y, int z);

/* SpawnerAnimals.canCreatureTypeSpawnAtLocation. Every read is in the one
 * column, so with its three cells inside the world it is one chunk (one
 * sp_ensure, as the first read makes) read directly. */
static int can_spawn_at_location(struct spawner *s, int type, int x, int y, int z)
{
    if (y < 1 || y > 254) return can_spawn_at_location_slow(s, type, x, y, z);

    const struct chunk *c = sp_read_chunk(s, x, z);

    if (c == NULL) return can_spawn_at_location_slow(s, type, x, y, z);

    int lx = x & 15, lz = z & 15;

    if (type == CT_WATER)
    {
        int a = chunk_get_block(c, lx, y, lz) & 4095;
        int b = chunk_get_block(c, lx, y - 1, lz) & 4095;
        int d = chunk_get_block(c, lx, y + 1, lz) & 4095;

        return MATERIALS[BLOCKS[a].material].is_liquid && MATERIALS[BLOCKS[b].material].is_liquid &&
               !BLOCKS[d].normal_cube;
    }

    int below = chunk_get_block(c, lx, y - 1, lz) & 4095;

    if (!solid_top_id(below, chunk_get_meta(c, lx, y - 1, lz))) return 0;

    int here = chunk_get_block(c, lx, y, lz) & 4095;
    int above = chunk_get_block(c, lx, y + 1, lz) & 4095;

    return below != SP_BEDROCK && !BLOCKS[here].normal_cube &&
           !MATERIALS[BLOCKS[here].material].is_liquid && !BLOCKS[above].normal_cube;
}

static int can_spawn_at_location_slow(struct spawner *s, int type, int x, int y, int z)
{
    if (type == CT_WATER)
    {
        int a = sp_block(s, x, y, z) & 4095;
        int c = sp_block(s, x, y - 1, z) & 4095;
        int d = sp_block(s, x, y + 1, z) & 4095;

        return MATERIALS[BLOCKS[a].material].is_liquid &&
               MATERIALS[BLOCKS[c].material].is_liquid &&
               !BLOCKS[d].normal_cube;
    }

    if (!solid_top_surface(s, x, y - 1, z)) return 0;

    int below = sp_block(s, x, y - 1, z) & 4095;
    int here = sp_block(s, x, y, z) & 4095;
    int above = sp_block(s, x, y + 1, z) & 4095;

    return below != SP_BEDROCK && !BLOCKS[here].normal_cube &&
           !MATERIALS[BLOCKS[here].material].is_liquid && !BLOCKS[above].normal_cube;
}

/* World.getClosestPlayer: 1 when the one player is inside the range, so the
 * spawn attempt fails. */
static int player_within(struct spawner *s, double x, double y, double z, double range)
{
    double dx = s->player_x - x, dy = s->player_y - y, dz = s->player_z - z;
    double d = dx * dx + dy * dy + dz * dz;

    return range < 0.0 || d < range * range;
}

/* getCurrentMoonPhaseFactor. */
static float moon_factor(struct spawner *s)
{
    static const float factors[8] = { 1.0F, 0.75F, 0.5F, 0.25F, 0.0F, 0.25F, 0.5F, 0.75F };
    int64_t t = s->world_time;
    int phase = (int)((t / 24000L % 8L + 8L) % 8L);

    return factors[phase];
}

/* World.func_147462_b -> func_147473_B, the local difficulty the armor and
 * GroupData chances read, over the chunk's inhabitedTime. */
float spawner_local_difficulty(struct spawner *s, double x, double y, double z)
{
    int hard = s->difficulty == 3;
    int fx = mh_floor(x), fy = mh_floor(y), fz = mh_floor(z);
    float v = 0.0F;

    const struct chunk *ch = fy >= 0 && fy < 256 ? world_chunk(s->w, fx >> 4, fz >> 4) : NULL;

    if (ch != NULL)
    {
        int64_t inh = ch->inhabited_time;

        v = clamp_float((float)inh / 3600000.0F, 0.0F, 1.0F) * (hard ? 1.0F : 0.75F)
            + moon_factor(s) * 0.25F;
    }

    if (s->difficulty == 0 || s->difficulty == 1) v *= (float)s->difficulty / 2.0F;

    return clamp_float(v, 0.0F, hard ? 1.5F : 1.0F);
}


/* Chunk.getRandomWithSeed(987234911L), the slime-chunk test's Random: the
 * seeded sum, the int multiplies overflowing exactly as Java's do. */
static jrand chunk_random(struct spawner *s, int cx, int cz, int64_t seed)
{
    int32_t a = (int32_t)((int64_t)cx * (int64_t)cx * 4987142L);
    int32_t b = (int32_t)((int64_t)cx * 5947611L);
    int32_t c = (int32_t)((int64_t)cz * (int64_t)cz);
    int32_t d = (int32_t)((int64_t)cz * 389711L);
    int64_t v = s->seed + (int64_t)a + (int64_t)b + (int64_t)c * 4392871L + (int64_t)d;

    jrand r;
    jr_seed(&r, v ^ seed);
    return r;
}

/* ------------------------------------------------------- record construction */

/* Entity.setSize and setPosition: the box, at the entity's feet. */
static void rec_make_box(struct sp_rec *r, float w, float h)
{
    double half = (double)(w / 2.0F);

    r->min_x = r->x - half;
    r->max_x = r->x + half;
    r->min_y = r->y;
    r->max_y = r->y + (double)h;
    r->min_z = r->z - half;
    r->max_z = r->z + half;
}

/* The kind's width and height at construction (Entity.setSize calls). */
static void kind_dims(int kind, int slime_size, float *w, float *h)
{
    *w = 0.6F;
    *h = 1.8F;

    switch (kind)
    {
    case SP_ENDERMAN: *w = 0.6F; *h = 2.9F; break;
    case SP_SPIDER: *w = 1.4F; *h = 0.9F; break;
    case SP_SLIME: *w = 0.6F * (float)slime_size; *h = 0.6F * (float)slime_size; break;
    case SP_SHEEP: *w = 0.9F; *h = 1.3F; break;
    case SP_PIG: *w = 0.9F; *h = 0.9F; break;
    case SP_CHICKEN: *w = 0.3F; *h = 0.7F; break;
    case SP_COW: case SP_MOOSHROOM: *w = 0.9F; *h = 1.4F; break;
    case SP_BAT: *w = 0.5F; *h = 0.9F; break;
    case SP_SQUID: *w = 0.95F; *h = 0.95F; break;
    case SP_GHAST: *w = 4.0F; *h = 4.0F; break;
    case SP_MAGMA_CUBE: *w = 0.6F * (float)slime_size; *h = 0.6F * (float)slime_size; break;
    default: break;
    }
}

/* One entity constructor: Det.nextEntityId, Det.newRandom, Det.uuid, the three
 * Math.random draws of EntityLivingBase, then the kind's own. The box is not
 * placed yet: setLocationAndAngles does that. */
static void rec_construct(struct spawner *s, struct sp_rec *r, int kind)
{
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->riding = r->ridden_by = -1;
    r->box = -1;
    r->seeder_before = det_seeder_state(s->det, s->role);
    r->math_before = det_math_state(s->det, s->role);
    r->next_id_before = s->det->next_id[s->role];

    for (int i = 0; i < 5; ++i) { r->eq_item[i] = -1; r->eq_dmg[i] = -1; }

    r->id = det_next_entity_id_role(s->det, s->role);

    det_rng e = det_new_random_role(s->det, s->role);

    det_uuid_role(s->det, s->role, &r->uuid_msb, &r->uuid_lsb);

    if (kind == SP_ZOMBIE || kind == SP_PIG_ZOMBIE)
    {
        /* EntityZombie.applyEntityAttributes, before EntityLivingBase's math
         * draws: the spawnReinforcements attribute's base, this.rand.nextDouble()
         * (the pigman's own applyEntityAttributes zeroes it afterwards) */
        (void)det_rng_double(&e);
    }

    r->field_70770_ap = (float)(det_math_random_role(s->det, s->role) + 1.0) * 0.01F;
    r->field_70769_ao = (float)det_math_random_role(s->det, s->role) * 12398.0F;
    /* the initial rotationYaw, (float)(Math.random() * PI * 2); a spawn's
     * setLocationAndAngles replaces it, a setPosition keeps it */
    r->yaw = (float)(det_math_random_role(s->det, s->role) * 3.141592653589793 * 2.0);


    if (kind == SP_SLIME || kind == SP_MAGMA_CUBE)
    {
        int v = det_rng_int_n(&e, 3);
        r->slime_size = 1 << v;
        (void)det_rng_int_n(&e, 20);               /* slimeJumpDelay */
    }
    else if (kind == SP_SQUID)
    {
        (void)det_rng_float(&e);                   /* rotationVelocity's draw */
    }
    else if (kind == SP_CHICKEN)
    {
        (void)det_rng_int_n(&e, 6000);             /* timeUntilNextEgg */
    }
    else if (kind == SP_SKELETON)
    {
    }

    r->e = e;
    r->rnd = det_rng_state(&e);
}

/* record i's flags byte from its fields (spawner.h rec_flags) */
static void rec_flags_set(struct spawner *s, int i)
{
    const struct sp_rec *r = &s->recs[i];
    int t = type_of_kind(r->kind);
    uint8_t f = (uint8_t)((r->alive != 0) | (r->persistent != 0) << 1 | (t >= 0 ? t + 1 : 0) << 2);

    if (s->rec_flags[i] != f) s->rec_flags[i] = f;
}

/* A new record or box: the arrays are fixed (spawner_init), so a pointer to a
 * record stays valid across a nested spawn. */
static void room_recs(struct spawner *s)
{
    if (s->nrecs == SP_MAX_RECS) abort();
}

static void room_boxes(struct spawner *s)
{
    if (s->nboxes == SP_MAX_BOXES) abort();
}

/* World.spawnEntityInWorld for a record: the box joins the queries and the
 * record joins the tick's out list in spawn order. */
static void rec_spawn(struct spawner *s, struct sp_rec *r, double x, double y, double z, float yaw)
{
    float w, h;

    kind_dims(r->kind, r->slime_size, &w, &h);
    r->x = x;
    r->y = y;
    r->z = z;
    r->yaw = yaw;
    rec_make_box(r, w, h);

    /* the record array moves when it grows, and an onSpawnWithEgg that
     * spawns (the chicken jockey's chicken, the spider jockey's skeleton)
     * runs while its caller holds a pointer to the record it is egging:
     * grow ahead, outside an egg path, so a nested spawn never moves it */
    room_recs(s);

    room_boxes(s);

    if (s->nout == SP_MAX_OUT) abort();

    struct sp_box *b = &s->boxes[s->nboxes];

    b->min_x = r->min_x;
    b->min_y = r->min_y;
    b->min_z = r->min_z;
    b->max_x = r->max_x;
    b->max_y = r->max_y;
    b->max_z = r->max_z;
    b->prevent = 1;
    b->list = -1;
    b->has_home = 0;

    struct sp_rec *st = &s->recs[s->nrecs++];

    *st = *r;
    st->alive = 1;
    rec_flags_set(s, (int)(st - s->recs));
    st->order_key = s->pop_rand != NULL ? s->checking_id : 0;
    st->chunk_stamp = ++entity_chunk_stamp;
    st->box = s->nboxes++;

    s->out[s->nout++] = st;
}

/* The armor item table, EntityLiving.getArmorItemForSlot with its switch
 * fall-through: a tier past 4 falls through every case to the default null. */
static int armor_item(int slot, int tier)
{
    static const int table[4][5] = {
        { 301, 317, 305, 309, 313 },         /* slot 1: boots */
        { 300, 316, 304, 308, 312 },         /* slot 2: leggings */
        { 299, 315, 303, 307, 311 },         /* slot 3: chestplate */
        { 298, 314, 302, 306, 310 },         /* slot 4: helmet */
    };

    if (slot < 1 || slot > 4) return -1;
    if (tier > 4) return -1;
    return table[slot - 1][tier];
}

/* The child zombie's half scale, EntityZombie.func_146069_a through
 * Entity.setSize: the box keeps its minimum corner and takes the new width
 * and height from there. */
static void rec_shrink_box(struct spawner *s, struct sp_rec *r)
{
    float w, h;

    kind_dims(SP_ZOMBIE, 0, &w, &h);
    w *= 0.5F;
    h *= 0.5F;

    r->max_x = r->min_x + (double)w;
    r->max_z = r->min_z + (double)w;
    r->max_y = r->min_y + (double)h;

    if (r->box >= 0)
    {
        s->boxes[r->box].max_x = r->max_x;
        s->boxes[r->box].max_z = r->max_z;
        s->boxes[r->box].max_y = r->max_y;
    }
}

/* EnchantmentHelper.addRandomEnchantment on one equipment stack. */
static void add_random_ench(struct sp_rec *r, det_rng *e, int slot, float local)
{
    struct enchant_data data[8];
    jrand tmp;

    int level = (int)(5.0F + local * (float)det_rng_int_n(e, 18));
    tmp.seed = det_rng_state(e);   /* the build continues the level draw */
    int n = build_enchantment_list(&tmp, r->eq_item[slot], level, data, 8);

    e->r.seed = tmp.seed;

    if (n <= 0) return;

    r->eq_tag[slot] = enchant_list_tag(r->eq_tag[slot], data, n);
}

/* EntityLiving.addRandomArmor. */
static void add_random_armor(struct spawner *s, struct sp_rec *r, det_rng *e)
{
    float local = spawner_local_difficulty(s, r->x, r->y, r->z);

    if (det_rng_float(e) < 0.15F * local)
    {
        int tier = det_rng_int_n(e, 2);
        float chance = s->difficulty == 3 ? 0.1F : 0.25F;

        if (det_rng_float(e) < 0.095F) ++tier;
        if (det_rng_float(e) < 0.095F) ++tier;
        if (det_rng_float(e) < 0.095F) ++tier;

        for (int slot = 3; slot >= 0; --slot)
        {
            int cur = slot + 1;

            if (slot < 3 && det_rng_float(e) < chance) break;

            if (r->eq_item[cur] < 0)
            {
                int item = armor_item(cur, tier);

                if (item > 0)
                {
                    r->eq_item[cur] = item;
                    r->eq_dmg[cur] = 0;
                    r->eq_cnt[cur] = 1;
                }
            }
        }
    }
}

/* EntityZombie.addRandomArmor: the base, then the iron-sword/shovel weapon
 * chance the subclass adds. */
static void add_random_armor_zombie(struct spawner *s, struct sp_rec *r, det_rng *e)
{
    add_random_armor(s, r, e);

    if (det_rng_float(e) < (s->difficulty == 3 ? 0.05F : 0.01F))
    {
        r->eq_item[0] = det_rng_int_n(e, 3) == 0 ? 267 : 256;   /* Items.iron_sword : iron_shovel */
        r->eq_dmg[0] = 0;
        r->eq_cnt[0] = 1;
    }
}

/* EntityLiving.enchantEquipment. */
static void enchant_equipment(struct spawner *s, struct sp_rec *r, det_rng *e)
{
    float local = spawner_local_difficulty(s, r->x, r->y, r->z);

    if (r->eq_item[0] >= 0 && det_rng_float(e) < 0.25F * local)
        add_random_ench(r, e, 0, local);

    for (int slot = 0; slot < 4; ++slot)
    {
        int cur = slot + 1;

        if (r->eq_item[cur] >= 0 && det_rng_float(e) < 0.5F * local)
            add_random_ench(r, e, cur, local);
    }
}

/* WeightedRandom.getRandomItem(world.rand, list). */
static int weighted_pick(struct spawner *s, const struct sp_list *l)
{
    int total = 0;

    for (int i = 0; i < l->n; ++i) total += l->weight[i];

    int pick = jr_int_n(s->rand, total);

    for (int i = 0; i < l->n; ++i)
    {
        pick -= l->weight[i];

        if (pick < 0) return i;
    }

    return -1;
}

/* WorldServer.spawnRandomCreature: ChunkProviderGenerate.getPossibleCreatures
 * (the replay's possible_creatures hands back a swamp hut's witch list for a
 * monster inside the hut), else the biome's list at (x, z); one weighted
 * pick. */
static int spawn_random_creature(struct spawner *s, int type, int x, int y, int z,
                                 const struct sp_list **list)
{
    const struct sp_list *l = NULL;

    if (s->possible_creatures != NULL) l = s->possible_creatures(s->possible_ctx, type, x, y, z);
    if (l == NULL) l = spawning_list_for(world_get_biome(s->w, x, z), type);

    *list = l;

    if (l->n == 0) return -1;

    return weighted_pick(s, l);
}
/* ---------------------------------------------------------- getCanSpawnHere */

/* EntityLiving.getCanSpawnHere, the base body: the entity collision, the block
 * collision, then the liquid check. */
static int can_spawn_here_base(struct spawner *s, const struct sp_rec *r)
{
    struct aabb b = aabb_make(r->min_x, r->min_y, r->min_z, r->max_x, r->max_y, r->max_z);

    if (!no_entity_collision(s, &b)) return 0;
    if (!colliding_empty(s, &b)) return 0;
    if (any_liquid(s, &b)) return 0;

    return 1;
}

/* EntityMob.isValidLightLevel, both draws off the entity's own Random. */
static int valid_light_level(struct spawner *s, struct sp_rec *r)
{
    int x = mh_floor(r->x);
    int y = mh_floor(r->min_y);
    int z = mh_floor(r->z);

    int sky = saved_light(s, LIGHT_SKY, x, y, z);
    int roll32 = det_rng_int_n(&r->e, 32);


    if (sky > roll32) return 0;

    int v = block_light_value(s, x, y, z);

    if (s->thundering)
    {
        int save = s->skylight_subtracted;

        s->skylight_subtracted = 10;
        v = block_light_value(s, x, y, z);
        s->skylight_subtracted = save;
    }

    {
        int roll8 = det_rng_int_n(&r->e, 8);
        int verdict = v <= roll8;

        return verdict;
    }
}

/* EntityMob.getCanSpawnHere and above: the difficulty, the light level, the
 * base body, then EntityCreature's getBlockPathWeight (EntityMob's own:
 * 0.5 - getLightBrightness). */
static int can_spawn_here_monster(struct spawner *s, struct sp_rec *r)
{
    if (s->difficulty == 0) return 0;
    if (!valid_light_level(s, r)) return 0;
    if (!can_spawn_here_base(s, r)) return 0;

    int x = mh_floor(r->x), y = mh_floor(r->min_y), z = mh_floor(r->z);

    return 0.5F - spawner_brightness(s->dim, block_light_value(s, x, y, z)) >= 0.0F;
}

/* EntityAnimal.getCanSpawnHere and above. */
static int can_spawn_here_animal(struct spawner *s, const struct sp_rec *r)
{
    int x = mh_floor(r->x), y = mh_floor(r->min_y), z = mh_floor(r->z);

    if ((sp_block(s, x, y - 1, z) & 4095) != SP_GRASS) return 0;
    if (full_block_light(s, x, y, z) <= 8) return 0;

    /* EntityCreature's weight is 0.0F for the animals */
    return can_spawn_here_base(s, r);
}

/* EntitySlime.getCanSpawnHere. */
static int can_spawn_here_slime(struct spawner *s, struct sp_rec *r)
{
    if (r->slime_size == 1 || s->difficulty != 0)
    {
        int x = mh_floor(r->x), y = mh_floor(r->y), z = mh_floor(r->z);

        if (world_get_biome(s->w, x, z) == 6 && r->y > 50.0 && r->y < 70.0)
        {
            if (det_rng_float(&r->e) < 0.5F)
            {
                if (det_rng_float(&r->e) < moon_factor(s))
                {
                    if (block_light_value(s, x, y, z) <= det_rng_int_n(&r->e, 8))
                        return can_spawn_here_base(s, r);
                }
            }
        }

        if (det_rng_int_n(&r->e, 10) == 0)
        {
            jrand cr = chunk_random(s, x >> 4, z >> 4, 987234911L);

            if (jr_int_n(&cr, 10) == 0 && r->y < 40.0)
                return can_spawn_here_base(s, r);
        }
    }

    return 0;
}

/* EntityBat.getCanSpawnHere and above. */
static int can_spawn_here_bat(struct spawner *s, struct sp_rec *r)
{
    int by = mh_floor(r->min_y);

    if (by >= 63) return 0;

    int x = mh_floor(r->x), z = mh_floor(r->z);
    int v = block_light_value(s, x, by, z);
    int n = 4;
    int halloween = (s->cal_month == 10 && s->cal_day >= 20) || (s->cal_month == 11 && s->cal_day <= 3);

    if (!halloween)
    {
        if (det_rng_bool(&r->e)) return 0;
    }
    else
    {
        n = 7;
    }

    if (v > det_rng_int_n(&r->e, n)) return 0;

    return can_spawn_here_base(s, r);
}

/* getCanSpawnHere for one candidate, the per-type chains in vanilla's order. */
static int can_spawn_here(struct spawner *s, struct sp_rec *r)
{
    switch (r->kind)
    {
    case SP_ZOMBIE: case SP_SKELETON: case SP_SPIDER: case SP_CREEPER:
    case SP_ENDERMAN: case SP_WITCH:
        return can_spawn_here_monster(s, r);

    case SP_SHEEP: case SP_PIG: case SP_CHICKEN: case SP_COW: case SP_MOOSHROOM:
        return can_spawn_here_animal(s, r);

    case SP_SLIME:
        return can_spawn_here_slime(s, r);

    case SP_BAT:
        return can_spawn_here_bat(s, r);

    case SP_PIG_ZOMBIE: case SP_MAGMA_CUBE:
        /* EntityPigZombie's and EntityMagmaCube's own: the difficulty and
         * the base body, no light test */
        return s->difficulty != 0 && can_spawn_here_base(s, r);

    case SP_GHAST:
        /* EntityGhast: one in twenty, then EntityLiving's body */
        if (det_rng_int_n(&r->e, 20) != 0) return 0;
        return can_spawn_here_base(s, r) && s->difficulty != 0;

    case SP_BLAZE:
        /* EntityMob.getCanSpawnHere with EntityBlaze's isValidLightLevel
         * (always true, no draws) */
        if (s->difficulty == 0) return 0;
        if (!can_spawn_here_base(s, r)) return 0;
        {
            int x = mh_floor(r->x), y = mh_floor(r->min_y), z = mh_floor(r->z);
            return 0.5F - spawner_brightness(s->dim, block_light_value(s, x, y, z)) >= 0.0F;
        }

    case SP_SQUID:
        /* EntityWaterMob.getCanSpawnHere is the entity collision alone, and
         * EntitySquid adds its bounds */
        if (r->y <= 45.0 || r->y >= 63.0) return 0;

        {
            struct aabb b = aabb_make(r->min_x, r->min_y, r->min_z, r->max_x, r->max_y, r->max_z);
            return no_entity_collision(s, &b);
        }

    default:
        return can_spawn_here_base(s, r);
    }
}

/* --------------------------------------------------------- onSpawnWithEgg */

static void egg_base(struct spawner *s, struct sp_rec *r);
static void egg_skeleton(struct spawner *s, struct sp_rec *r, struct sp_data *d);

/* egg_on for the two kinds a jockey path spawns (the chicken under a baby
 * zombie, the skeleton on a spider), whose onSpawnWithEgg spawns nothing
 * more: the jockey paths call this, so egg_on does not call itself. */
static void egg_on_rider(struct spawner *s, struct sp_rec *r, struct sp_data *d)
{
    ++s->egg_depth;
    r->world_before_egg = s->rand->seed;

    if (r->kind == SP_SKELETON) egg_skeleton(s, r, d);
    else egg_base(s, r);

    r->rnd = det_rng_state(&r->e);
    --s->egg_depth;
}

/* EntityLiving.onSpawnWithEgg's base: the followRange spawn bonus. The
 * modifier's value lives in the recorded NBT; the draws are what the digest
 * pins: one nextGaussian off the entity's own Random, and the AttributeModifier
 * constructor's Det.uuid() (two seeder longs). */
static void egg_base(struct spawner *s, struct sp_rec *r)
{
    r->follow_bonus = det_rng_gaussian(&r->e) * 0.05;
    det_uuid_role(s->det, s->role, &r->follow_msb, &r->follow_lsb);
}

static void egg_zombie(struct spawner *s, struct sp_rec *r, struct sp_data *d)
{
    float local = spawner_local_difficulty(s, r->x, r->y, r->z);

    egg_base(s, r);

    r->pickup = det_rng_float(&r->e) < 0.55F * local;

    if (!d->zombie_made)
    {
        d->zombie_made = 1;
        d->child = jr_float(s->rand) < 0.05F;
        d->villager = jr_float(s->rand) < 0.05F;
    }

    if (d->villager) r->villager = 1;

    if (d->child)
    {
        r->child = 1;
        rec_shrink_box(s, r);

        if ((double)jr_float(s->rand) < 0.05)
        {
            /* the jockey hunt: an EntityChicken within the expanded box, alive
             * and not mounted */
            double ex0 = r->min_x - 5.0, ey0 = r->min_y - 3.0, ez0 = r->min_z - 5.0;
            double ex1 = r->max_x + 5.0, ey1 = r->max_y + 3.0, ez1 = r->max_z + 5.0;
            struct sp_rec *found = NULL;

            for (int i = 0; i < s->nrecs; ++i)
            {
                struct sp_rec *c = &s->recs[i];

                /* IEntitySelector.field_152785_b: alive, and a record kept
                 * for a chicken since saved with its chunk is not */
                if (c->kind != SP_CHICKEN || !c->alive || c->riding >= 0 || c->ridden_by >= 0) continue;

                if (c->max_x > ex0 && c->min_x < ex1 && c->max_y > ey0 && c->min_y < ey1 &&
                    c->max_z > ez0 && c->min_z < ez1)
                {
                    found = c;
                    break;
                }
            }

            if (found != NULL)
            {
                found->chicken_jockey = 1;
                found->ridden_by = r->id;
                r->riding = found->id;
            }
        }
        else if ((double)jr_float(s->rand) < 0.05)
        {
            struct sp_rec chicken;

            rec_construct(s, &chicken, SP_CHICKEN);
            egg_on_rider(s, &chicken, d);
            rec_spawn(s, &chicken, r->x, r->y, r->z, r->yaw);
            struct sp_rec *jc = s->out[s->nout - 1];

            jc->chicken_jockey = 1;
            jc->ridden_by = r->id;
            r->riding = jc->id;
        }
    }

    r->break_doors = det_rng_float(&r->e) < local * 0.1F;

    if (r->kind == SP_PIG_ZOMBIE)
    {
        /* EntityPigZombie.addRandomArmor: the golden sword, no draw */
        r->eq_item[0] = 283;
        r->eq_dmg[0] = 0;
        r->eq_cnt[0] = 1;
    }
    else add_random_armor_zombie(s, r, &r->e);
    enchant_equipment(s, r, &r->e);

    /* the Halloween helmet, BlockPumpkin's day: the calendar check reads no
     * draw outside the Oct 31 window */
    if (r->eq_item[4] < 0 && s->cal_month == 10 && s->cal_day == 31 && det_rng_float(&r->e) < 0.25F)
    {
        r->eq_item[4] = det_rng_float(&r->e) < 0.1F ? 91 : 86;
        r->eq_dmg[4] = 0;
        r->eq_cnt[4] = 1;
    }

    /* the knockbackResistance spawn bonus: one nextDouble, and the
     * AttributeModifier's UUID off the seeder, always. The modifier's UUID
     * is not the record's: that stays the entity's own construction UUID. */
    {
        int64_t mu, lu;
        r->knockback_bonus = det_rng_double(&r->e) * 0.05000000074505806;
        det_uuid_role(s->det, s->role, &r->knockback_msb, &r->knockback_lsb);

        if (det_rng_double(&r->e) * 1.5 * (double)local > 1.0)
        {
            /* the followRange modifier applies; its value lives in the NBT,
             * its UUID is drawn with it */
            det_uuid_role(s->det, s->role, &mu, &lu);
        }

        if (det_rng_float(&r->e) < local * 0.05F)
        {
            r->leader_reinforcements = det_rng_double(&r->e) * 0.25 + 0.5;
            det_uuid_role(s->det, s->role, &r->leader_reinforcements_msb, &r->leader_reinforcements_lsb);
            r->leader_health = det_rng_double(&r->e) * 3.0 + 1.0;
            det_uuid_role(s->det, s->role, &r->leader_health_msb, &r->leader_health_lsb);
            r->leader = 1;
        }
    }
}

/* EntitySkeleton.onSpawnWithEgg: in the Nether four in five are wither
 * skeletons (setSkeletonType(1) grows the box from its min corner, a stone
 * sword), the rest take the overworld branch. */
static void egg_skeleton(struct spawner *s, struct sp_rec *r, struct sp_data *d)
{
    (void)d;
    egg_base(s, r);

    if (s->dim == -1 && det_rng_int_n(&r->e, 5) > 0)
    {
        r->skel_type = 1;
        r->eq_item[0] = 272;          /* Items.stone_sword */
        r->eq_dmg[0] = 0;
        r->eq_cnt[0] = 1;
        r->max_x = r->min_x + (double)0.72F;
        r->max_z = r->min_z + (double)0.72F;
        r->max_y = r->min_y + (double)2.34F;
        if (r->box >= 0)
        {
            s->boxes[r->box].max_x = r->max_x;
            s->boxes[r->box].max_z = r->max_z;
            s->boxes[r->box].max_y = r->max_y;
        }
    }
    else
    {
        add_random_armor(s, r, &r->e);
        r->eq_item[0] = 261;          /* Items.bow */
        r->eq_dmg[0] = 0;
        r->eq_cnt[0] = 1;
        enchant_equipment(s, r, &r->e);
    }

    r->pickup = det_rng_float(&r->e) < 0.55F * spawner_local_difficulty(s, r->x, r->y, r->z);

    if (r->eq_item[4] < 0 && s->cal_month == 10 && s->cal_day == 31 && det_rng_float(&r->e) < 0.25F)
    {
        r->eq_item[4] = det_rng_float(&r->e) < 0.1F ? 91 : 86;
        r->eq_dmg[4] = 0;
        r->eq_cnt[4] = 1;
    }
}

/* EntitySpider.onSpawnWithEgg: the 1-in-100 jockey, then the GroupData. */
static void egg_spider(struct spawner *s, struct sp_rec *r, struct sp_data *d)
{
    egg_base(s, r);

    if (jr_int_n(s->rand, 100) == 0)
    {
        struct sp_rec sk;

        rec_construct(s, &sk, SP_SKELETON);
        /* setLocationAndAngles before the skeleton's onSpawnWithEgg: its
         * armour and enchantment rolls read the local difficulty there */
        sk.x = r->x;
        sk.y = r->y;
        sk.z = r->z;
        sk.yaw = r->yaw;
        egg_on_rider(s, &sk, d);
        rec_spawn(s, &sk, r->x, r->y, r->z, r->yaw);
        struct sp_rec *st = s->out[s->nout - 1];

        st->riding = r->id;
        r->ridden_by = st->id;
    }

    if (!d->spider_made)
    {
        d->spider_made = 1;

        if (s->difficulty == 3 && jr_float(s->rand) < 0.1F * spawner_local_difficulty(s, r->x, r->y, r->z))
        {
            /* EntitySpider.GroupData.func_111104_a: which effect the pack's
             * spiders carry */
            d->spider_potion = spider_group_potion(jr_int_n(s->rand, 5));
        }
    }

    /* every spider of the pack: addPotionEffect(id, Integer.MAX_VALUE),
     * which sr_adopt_mob's egg path applies */
    r->spider_potion = d->spider_potion;
}

/* EntitySheep.onSpawnWithEgg: the fleece colour off World.rand. */
static void egg_sheep(struct spawner *s, struct sp_rec *r)
{
    egg_base(s, r);

    int v = jr_int_n(s->rand, 100);

    r->fleece = v < 5 ? 15 : (v < 10 ? 7 : (v < 15 ? 8 : (v < 18 ? 12 : (jr_int_n(s->rand, 500) == 0 ? 6 : 0))));
}

/* The kind's onSpawnWithEgg, run after World.spawnEntityInWorld. The tail
 * leaves the record's rnd at the path's end state: the recording's line
 * carries it, and the path may have spawned records after this one (the
 * jockey's chicken, the spider jockey's skeleton), so the caller's
 * after-the-path read lands on the wrong record. */
static void egg_on(struct spawner *s, struct sp_rec *r, struct sp_data *d)
{
    ++s->egg_depth;
    r->world_before_egg = s->rand->seed;
    switch (r->kind)
    {
    case SP_ZOMBIE:
        egg_zombie(s, r, d);
        break;
    case SP_PIG_ZOMBIE:
    {
        /* EntityPigZombie.onSpawnWithEgg: the zombie's path, setVillager
         * (false), and it returns the data it was handed, so the zombie's
         * GroupData never reaches the pack's next spawn */
        struct sp_data keep = *d;
        egg_zombie(s, r, d);
        *d = keep;
        r->villager = 0;
        break;
    }
    case SP_SKELETON:
        egg_skeleton(s, r, d);
        break;
    case SP_SPIDER:
        egg_spider(s, r, d);
        break;
    case SP_SHEEP:
        egg_sheep(s, r);
        break;
    default:
        egg_base(s, r);
        break;
    }

    r->rnd = det_rng_state(&r->e);
    --s->egg_depth;
}
/* The Dev summon's construction (EntityList.createEntityFromNBT over a tag
 * holding only the id, whose readFromNBT draws nothing and zeroes the
 * rotation), setLocationAndAngles, then onSpawnWithEgg: the record's draws on
 * the spawner's streams, outside the natural spawn's out list. */
int spawner_summon(struct spawner *s, int kind, double x, double y, double z,
                   int child, int size, struct sp_rec *out)
{
    struct sp_rec r;
    struct sp_data d;
    float w, h;

    memset(&d, 0, sizeof d);
    rec_construct(s, &r, kind);
    /* Dev.java's tag keys, EntityZombie.readEntityFromNBT's IsBaby and
     * EntitySlime.readEntityFromNBT's Size (setSlimeSize(var2 + 1)): the forced
     * form replaces the constructor's own draw before anything reads it. */
    if ((kind == SP_ZOMBIE || kind == SP_PIG_ZOMBIE) && child) r.child = 1;
    if ((kind == SP_SLIME || kind == SP_MAGMA_CUBE) && size > 0) r.slime_size = size;
    kind_dims(kind, r.slime_size, &w, &h);
    r.x = x;
    r.y = y;
    r.z = z;
    r.yaw = 0.0F;
    rec_make_box(&r, w, h);
    if (r.child) rec_shrink_box(s, &r);
    egg_on(s, &r, &d);
    *out = r;
    return 1;
}

/* EntityZombie.attackEntityFrom's HARD reinforcement: new EntityZombie (the
 * constructor's draws on the spawner's streams, spent whether or not it
 * spawns), then up to 50 tries off the CALLER's Random, each axis
 * getRandomIntegerInRange(7, 40) * getRandomIntegerInRange(-1, 1); a try
 * with a solid top below and getBlockLightValue under 10 takes setPosition
 * (the integer corner, not a cell centre) and EntityLiving.getCanSpawnHere's
 * body; the first that passes is spawned and runs onSpawnWithEgg(null).
 * 1 with *out filled when one spawned. */
int spawner_reinforce(struct spawner *s, det_rng *caller, int x, int y, int z, struct sp_rec *out)
{
    struct sp_rec r;
    struct sp_data d;
    float w, h;

    memset(&d, 0, sizeof d);
    rec_construct(s, &r, SP_ZOMBIE);
    kind_dims(SP_ZOMBIE, 0, &w, &h);

    for (int i = 0; i < 50; ++i)
    {
        int ax = det_rng_int_n(caller, 34) + 7;
        int bx = x + ax * (det_rng_int_n(caller, 3) - 1);
        int ay = det_rng_int_n(caller, 34) + 7;
        int by = y + ay * (det_rng_int_n(caller, 3) - 1);
        int az = det_rng_int_n(caller, 34) + 7;
        int bz = z + az * (det_rng_int_n(caller, 3) - 1);

        if (!solid_top_surface(s, bx, by - 1, bz) || block_light_value(s, bx, by, bz) >= 10) continue;

        r.x = (double)bx;
        r.y = (double)by;
        r.z = (double)bz;
        rec_make_box(&r, w, h);
        if (!can_spawn_here_base(s, &r)) continue;

        egg_on(s, &r, &d);
        *out = r;
        return 1;
    }
    return 0;
}

/* ItemMonsterPlacer.spawnCreature(world, id, x, y, z), BlockPortal.updateTick's
 * pigman: EntityList.createEntityByID, then setLocationAndAngles with
 * MathHelper.wrapAngleTo180_float(World.rand.nextFloat() * 360.0F) and pitch
 * 0, onSpawnWithEgg(null), and playLivingSound, whose getSoundPitch draws two
 * nextFloat off the entity's own Random (the sound itself is not state). */
int spawner_spawn_creature(struct spawner *s, int kind, double x, double y, double z,
                           struct sp_rec *out)
{
    struct sp_rec r;
    struct sp_data d;
    float w, h;

    memset(&d, 0, sizeof d);
    rec_construct(s, &r, kind);
    kind_dims(kind, r.slime_size, &w, &h);
    r.x = x;
    r.y = y;
    r.z = z;
    float yaw = fmodf(jr_float(s->rand) * 360.0F, 360.0F);
    if (yaw >= 180.0F) yaw -= 360.0F;
    if (yaw < -180.0F) yaw += 360.0F;
    r.yaw = yaw;
    rec_make_box(&r, w, h);
    egg_on(s, &r, &d);
    (void)det_rng_float(&r.e);
    (void)det_rng_float(&r.e);
    r.rnd = det_rng_state(&r.e);
    *out = r;
    return 1;
}

/* --------------------------------------------------- findChunksForSpawning */

/* The static air and water material indexes, by name. */
static int sp_mat(const char *name)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
        if (MATERIALS[i].name != NULL && strcmp(MATERIALS[i].name, name) == 0) return (int)i;

    return -1;
}

/* World.countEntities(Class) over the constructed records: a living whose
 * isNoDespawnRequired is set (a name tag, a picked-up item) is not counted. */
static int count_entities(struct spawner *s, int type)
{
    int n = 0;
    uint8_t want = (uint8_t)((type + 1) << 2 | 1);

    for (int i = 0; i < s->nrecs; ++i) n += s->rec_flags[i] == want;

    return n;
}

/* The one type's cap, with the negative check's delta. */
static int cap_of(struct spawner *s, int type)
{
    static const int caps[CT_TYPES] = { CAP_MONSTER, CAP_CREATURE, CAP_AMBIENT, CAP_WATER };
    int cap = caps[type];

    if (s->neg_cap_type == type) cap += s->neg_cap_add;

    return cap;
}

/* Chunk.getTopFilledSegment; a chunk the provider has not loaded is the empty
 * chunk, whose top segment is 0. */
static int top_filled(struct spawner *s, int cx, int cz)
{
    struct chunk *c = sp_chunk(s, cx, cz);

    if (c == NULL) return 0;

    for (int sec = 15; sec >= 0; --sec)
        if ((c->mask >> sec) & 1) return sec << 4;

    return 0;
}

/* SpawnerAnimals.func_151350_a: the chunk, then three World.rand draws, the position. */
static void spawn_position(struct spawner *s, int cx, int cz, int *x, int *y, int *z)
{
    /* getChunkFromChunkCoords comes first: a load there can reseed World.rand */
    sp_chunk(s, cx, cz);
    *x = cx * 16 + jr_int_n(s->rand, 16);
    *z = cz * 16 + jr_int_n(s->rand, 16);
    *y = jr_int_n(s->rand, top_filled(s, cx, cz) + 16 - 1);
}

/* The pack walk for one eligible chunk whose material precheck passed: three
 * rounds, each a fresh walk from the same point, four attempts each, abandoned
 * to the next round the moment the type pick fails, done for the chunk the
 * moment a pack reaches its per-chunk cap. */
static void pack_walk(struct spawner *s, int type, int x, int y, int z)
{
    int packed = 0;   /* var17, the chunk's pack count across the rounds */

    /* the walk's own stream: the tick's is World.rand, the worldgen walk's
     * the populate chunk rand the caller set */
    jrand *walk = s->pop_rand ? s->pop_rand : s->rand;

    for (int round = 0; round < 3; ++round)
    {
        struct sp_data data;

        memset(&data, 0, sizeof data);   /* var24, fresh each round */

        int wx = x, wy = y, wz = z;
        int entry = -1;          /* var23, the SpawnListEntry, made once per round */
        int entry_kind = -1;     /* the entry's entityClass, resolved at the pick */
        int attempts = 0;

        while (1)
        {
            if (attempts < (s->neg_pack ? 3 : 4))
            {
                wx += jr_int_n(walk, 6) - jr_int_n(walk, 6);
                wy += jr_int_n(walk, 1) - jr_int_n(walk, 1);
                wz += jr_int_n(walk, 6) - jr_int_n(walk, 6);

                if (can_spawn_at_location(s, type, wx, wy, wz))
                {
                    float fx = (float)wx + 0.5F;
                    float fy = (float)wy;
                    float fz = (float)wz + 0.5F;

                    if (!player_within(s, (double)fx, (double)fy, (double)fz, 24.0))
                    {
                        float dxs = fx - (float)s->spawn_x;
                        float dys = fy - (float)s->spawn_y;
                        float dzs = fz - (float)s->spawn_z;
                        float dist = dxs * dxs + dys * dys + dzs * dzs;

                        if (dist >= 576.0F)
                        {
                            if (entry < 0)
                            {
                                const struct sp_list *lp;

                                entry = spawn_random_creature(s, type, wx, wy, wz, &lp);

                                /* break label103: the round is abandoned, the
                                 * next round starts fresh from the chunk */
                                if (entry < 0) break;

                                /* var23 holds the SpawnListEntry itself, so
                                 * the kind is fixed for the round even when
                                 * the walk crosses into another biome */
                                entry_kind = lp->kind[entry];
                            }

                            {
                                struct sp_rec r;
                                float yaw;

                                rec_construct(s, &r, entry_kind);
                                yaw = jr_float(walk) * 360.0F;
                                {
                                    uint32_t yb;
                                    memcpy(&yb, &yaw, sizeof yb);
                                }

                                /* setLocationAndAngles ran before
                                 * getCanSpawnHere: the light and collision
                                 * checks read the candidate's box here */
                                {
                                    float w, h;

                                    kind_dims(r.kind, r.slime_size, &w, &h);
                                    r.x = fx;
                                    r.y = fy;
                                    r.z = fz;
                                    r.yaw = yaw;
                                    rec_make_box(&r, w, h);
                                }

                                s->checking_id = r.id;
                                int can = can_spawn_here(s, &r);
                                s->checking_id = 0;
                                if (can)
                                {
                                    ++packed;

                                    rec_spawn(s, &r, fx, fy, fz, yaw);
                                    struct sp_rec *made = s->out[s->nout - 1];

                                    egg_on(s, made, &data);

                                    /* the line's rnd is the state after the
                                     * egg path; egg_on's tail sets it (also
                                     * for the records the path itself
                                     * spawns: the jockey's chicken, the
                                     * spider jockey's skeleton) */
                                    made->rnd = det_rng_state(&made->e);

                                    /* getMaxSpawnedInChunk: 4, the ghast's 1 */
                                    if (packed >= (entry_kind == SP_GHAST ? 1 : 4)) return;   /* continue label110 */
                                }
                            }
                        }
                    }
                }

                ++attempts;
                continue;
            }

            break;
        }
    }
}

/* findChunksForSpawning, one tick's worth. The eligible set is the 17x17
 * around the player, in its Java HashMap order (jorder.h: the 289 keys in a
 * 512-bin table, cleared since the first fill); the edges
 * carry the value TRUE and are skipped, the interior FALSE. */
static void find_chunks(struct spawner *s)
{
    int pcx = mh_floor(s->player_x / 16.0);
    int pcz = mh_floor(s->player_z / 16.0);
    if (!s->elig_valid || s->elig_pcx != pcx || s->elig_pcz != pcz)
    {
        int bins = jord_cap(289);
        s->elig_n = jord_ccp_window(pcx, pcz, 8, &bins, s->elig_order);
        s->elig_pcx = pcx;
        s->elig_pcz = pcz;
        s->elig_valid = 1;
    }
    const int *order = s->elig_order;
    int nelig = s->elig_n;

    for (int type = 0; type < CT_TYPES; ++type)
    {

        if (type == CT_MONSTER && !s->hostile) continue;
        if (type == CT_CREATURE && !s->animal_gate) continue;

        /* countEntities <= maxNumberOfCreature * eligible / 256 */
        if (count_entities(s, type) > cap_of(s, type) * nelig / 256) continue;

        int want_mat = type == CT_WATER ? sp_mat("water") : sp_mat("air");

        for (int i = 0; i < nelig; ++i)
        {
            int cx = order[i * 2], cz = order[i * 2 + 1];

            if (cx == pcx - 8 || cx == pcx + 8 || cz == pcz - 8 || cz == pcz + 8) continue;

            int x, y, z;

            spawn_position(s, cx, cz, &x, &y, &z);

            int here = sp_block(s, x, y, z) & 4095;

            if (BLOCKS[here].normal_cube || BLOCKS[here].material != want_mat) continue;

            pack_walk(s, type, x, y, z);
        }
    }

}

/* ------------------------------------------------------------- public parts */

void spawner_init(struct spawner *s, struct world *w, det_state *det, jrand *rand)
{
    memset(s, 0, sizeof *s);
    s->w = w;
    s->det = det;
    s->rand = rand;
    s->role = DET_SERVER;
    s->hostile = s->peaceful = 1;
    s->recs = fixed_array(SP_MAX_RECS, sizeof *s->recs);
    s->boxes = fixed_array(SP_MAX_BOXES, sizeof *s->boxes);
    s->out = fixed_array(SP_MAX_OUT, sizeof *s->out);
    s->rec_index_cap = 2 * SP_MAX_RECS;
    s->rec_index = fixed_array((size_t)s->rec_index_cap, sizeof *s->rec_index);
    s->rec_index_mask = 1023;
    s->rec_flags = fixed_array(SP_MAX_RECS, sizeof *s->rec_flags);
    s->caprecs = SP_MAX_RECS;
    s->capboxes = SP_MAX_BOXES;
    s->capout = SP_MAX_OUT;
    s->player_box = -1;
}

void spawner_free(struct spawner *s)
{
    fixed_array_free(s->recs, SP_MAX_RECS, sizeof *s->recs);
    fixed_array_free(s->boxes, SP_MAX_BOXES, sizeof *s->boxes);
    fixed_array_free(s->out, SP_MAX_OUT, sizeof *s->out);
    fixed_array_free(s->rec_index, (size_t)s->rec_index_cap, sizeof *s->rec_index);
    fixed_array_free(s->rec_flags, SP_MAX_RECS, sizeof *s->rec_flags);
    memset(s, 0, sizeof *s);
}

void spawner_set_player(struct spawner *s, double x, double y, double z)
{
    s->player_x = x;
    s->player_y = y;
    s->player_z = z;

    room_boxes(s);

    /* the parked player's box, the 0.6 x 1.8 the two collision checks read */
    s->player_box = s->nboxes;
    struct sp_box *b = &s->boxes[s->nboxes++];

    b->min_x = x - 0.3;
    b->max_x = x + 0.3;
    b->min_y = y;
    b->max_y = y + 1.8;
    b->min_z = z - 0.3;
    b->max_z = z + 0.3;
    b->prevent = 1;
    b->list = -1;
    b->has_home = 0;
}

/* World.setAllowedSpawnTypes, the flags WorldServer.tick hands
 * findChunksForSpawning: MinecraftServer.func_147139_a sets them from the
 * difficulty at load and on every difficulty change (single player, not
 * hardcore: hostile unless PEACEFUL, peaceful always). */
void spawner_set_spawn_types(struct spawner *s, int hostile, int peaceful)
{
    s->hostile = hostile;
    s->peaceful = peaceful;
}

void spawner_set_scene(struct spawner *s, int64_t seed, int difficulty,
                       int cal_month, int cal_day, int spawn_x, int spawn_y, int spawn_z)
{
    s->seed = seed;
    s->difficulty = difficulty;
    s->cal_month = cal_month;
    s->cal_day = cal_day;
    s->spawn_x = spawn_x;
    s->spawn_y = spawn_y;
    s->spawn_z = spawn_z;
}

static size_t rec_slot(int id, int cap)
{
    return (size_t)(((uint32_t)id * 0x9e3779b9u) & (uint32_t)(cap - 1));
}

/* The first record with this id, or -1: the linear walk's answer, through
 * rec_index. Records appended since the last call are indexed first (an id
 * already present keeps its first record); fewer records than indexed means
 * the list started over, and the index is rebuilt. */
static int rec_first(struct spawner *s, int id)
{
    if (s->nrecs == 0)
    {
        s->rec_indexed = 0;
        return -1;
    }

    /* the list started over, or the used part is past half full: indexed
     * again from the first record */
    size_t used = (size_t)s->rec_index_mask + 1;

    if (s->nrecs < s->rec_indexed || (2 * (size_t)s->nrecs > used && used < (size_t)s->rec_index_cap))
    {
        while (2 * (size_t)s->nrecs > used && used < (size_t)s->rec_index_cap) used *= 2;
        memset(s->rec_index, 0, used * sizeof *s->rec_index);
        s->rec_index_mask = (int)used - 1;
        s->rec_indexed = 0;
    }

    const size_t mask = (size_t)s->rec_index_mask;

    for (; s->rec_indexed < s->nrecs; ++s->rec_indexed)
    {
        int rid = s->recs[s->rec_indexed].id;
        size_t k = rec_slot(rid, (int)(mask + 1));

        while (s->rec_index[k].at != 0 && s->rec_index[k].id != rid) k = (k + 1) & mask;
        if (s->rec_index[k].at == 0)
        {
            s->rec_index[k].id = rid;
            s->rec_index[k].at = s->rec_indexed + 1;
        }
    }

    for (size_t k = rec_slot(id, (int)(mask + 1)); s->rec_index[k].at != 0; k = (k + 1) & mask)
        if (s->rec_index[k].id == id) return s->rec_index[k].at - 1;

    return -1;
}

/* stored only when the bits differ: a living that did not move leaves its
 * record's and box's lines clean (every living is tracked every tick;
 * lane/villscale) */
static inline void st_dbl(double *d, double v)
{
    if (memcmp(d, &v, sizeof v)) *d = v;
}

static inline void st_int(int *d, int v)
{
    if (*d != v) *d = v;
}

void spawner_track(struct spawner *s, int id, const struct entity *e)
{
    int i = rec_first(s, id);

    if (i >= 0)
    {
        struct sp_rec *r = &s->recs[i];
        st_int(&r->alive, e != NULL);
        rec_flags_set(s, i);
        if (r->box < 0) return;
        struct sp_box *b = &s->boxes[r->box];
        if (e == NULL)
        {
            b->prevent = 0;
            b->min_x = b->min_y = b->min_z = 1.0e30;
            b->max_x = b->max_y = b->max_z = 1.0e30;
        }
        else
        {
            st_dbl(&r->x, e->pos_x);
            st_dbl(&r->y, e->pos_y);
            st_dbl(&r->z, e->pos_z);
            st_dbl(&r->min_x, e->bounding_box.min_x);
            st_dbl(&r->min_y, e->bounding_box.min_y);
            st_dbl(&r->min_z, e->bounding_box.min_z);
            st_dbl(&r->max_x, e->bounding_box.max_x);
            st_dbl(&r->max_y, e->bounding_box.max_y);
            st_dbl(&r->max_z, e->bounding_box.max_z);
            st_dbl(&b->min_x, e->bounding_box.min_x);
            st_dbl(&b->min_y, e->bounding_box.min_y);
            st_dbl(&b->min_z, e->bounding_box.min_z);
            st_dbl(&b->max_x, e->bounding_box.max_x);
            st_dbl(&b->max_y, e->bounding_box.max_y);
            st_dbl(&b->max_z, e->bounding_box.max_z);
            st_int(&b->has_home, 1);
            st_int(&b->hcx, mh_floor(e->pos_x / 16.0));
            st_int(&b->hcy, mh_floor(e->pos_y / 16.0));
            st_int(&b->hcz, mh_floor(e->pos_z / 16.0));
        }
        return;
    }
}

void spawner_compact(struct spawner *s)
{
    int dead = 0;
    for (int i = 0; i < s->nrecs; ++i) dead += !s->recs[i].alive;
    if (s->nrecs < 1024 || 2 * dead < s->nrecs) return;

    /* a dead record goes when no other record has its id (rec_first
     * answers the first of an id: a dead first with a live second must stay) */
    int cap = 64;
    while (cap < 2 * s->nrecs) cap *= 2;
    struct { int32_t id, n; } *ids = calloc((size_t)cap, sizeof *ids);
    int *newbox = malloc(((size_t)s->nboxes + 1) * sizeof *newbox);
    unsigned char *gone = calloc((size_t)s->nrecs + 1, 1), *boxgone = calloc((size_t)s->nboxes + 1, 1);
    if (ids == NULL || newbox == NULL || gone == NULL || boxgone == NULL) abort();
    for (int i = 0; i < s->nrecs; ++i)
    {
        size_t k = rec_slot(s->recs[i].id, cap);
        while (ids[k].n && ids[k].id != s->recs[i].id) k = (k + 1) & (size_t)(cap - 1);
        ids[k].id = s->recs[i].id;
        ++ids[k].n;
    }
    for (int i = 0; i < s->nrecs; ++i)
    {
        const struct sp_rec *r = &s->recs[i];
        if (r->alive) continue;
        size_t k = rec_slot(r->id, cap);
        while (ids[k].id != r->id) k = (k + 1) & (size_t)(cap - 1);
        if (ids[k].n != 1) continue;
        gone[i] = 1;
        if (r->box >= 0 && r->box != s->player_box) boxgone[r->box] = 1;
    }

    int nb = 0;
    for (int b = 0; b < s->nboxes; ++b)
    {
        newbox[b] = boxgone[b] ? -1 : nb;
        if (!boxgone[b]) s->boxes[nb++] = s->boxes[b];
    }
    int nr = 0;
    for (int i = 0; i < s->nrecs; ++i)
    {
        if (gone[i]) continue;
        s->recs[nr] = s->recs[i];
        s->rec_flags[nr] = s->rec_flags[i];
        if (s->recs[nr].box >= 0) s->recs[nr].box = newbox[s->recs[nr].box];
        ++nr;
    }
    if (s->player_box >= 0) s->player_box = newbox[s->player_box];
    s->nrecs = nr;
    s->nboxes = nb;
    /* the last run's spawn list is read only by the run that made it */
    s->nout = 0;
    /* the id index, from the first record again */
    memset(s->rec_index, 0, ((size_t)s->rec_index_mask + 1) * sizeof *s->rec_index);
    s->rec_indexed = 0;
    free(ids);
    free(newbox);
    free(gone);
    free(boxgone);
}

void spawner_set_persistent(struct spawner *s, int id, int persistent)
{
    int i = rec_first(s, id);
    /* the record written only when its flag changes (every living is synced
     * every tick) */
    if (i >= 0 && ((s->rec_flags[i] >> 1 & 1) != (persistent != 0)))
    {
        s->recs[i].persistent = persistent != 0;
        rec_flags_set(s, i);
    }
}

void spawner_register_living(struct spawner *s, int kind, int id, const struct entity *e)
{
    if (rec_first(s, id) >= 0)
    {
        spawner_track(s, id, e);
        return;
    }

    room_recs(s);
    room_boxes(s);

    struct sp_rec *r = &s->recs[s->nrecs++];
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->id = id;
    r->alive = 1;
    r->box = s->nboxes;
    r->riding = r->ridden_by = -1;
    rec_flags_set(s, (int)(r - s->recs));
    r->x = e->pos_x; r->y = e->pos_y; r->z = e->pos_z;
    r->min_x = e->bounding_box.min_x; r->max_x = e->bounding_box.max_x;
    r->min_y = e->bounding_box.min_y; r->max_y = e->bounding_box.max_y;
    r->min_z = e->bounding_box.min_z; r->max_z = e->bounding_box.max_z;

    struct sp_box *b = &s->boxes[s->nboxes++];
    b->min_x = r->min_x; b->max_x = r->max_x;
    b->min_y = r->min_y; b->max_y = r->max_y;
    b->min_z = r->min_z; b->max_z = r->max_z;
    b->prevent = 1;
    b->list = -1;
    b->has_home = 1;
    b->hcx = mh_floor(e->pos_x / 16.0);
    b->hcy = mh_floor(e->pos_y / 16.0);
    b->hcz = mh_floor(e->pos_z / 16.0);
}

/* Adopt the tick body's spawns: the item, falling-block and lightning boxes
 * the colliding check reads (none of them prevents spawning). */
void spawner_absorb(struct spawner *s, const struct st_spawn *sp, int n)
{
    for (int i = 0; i < n; ++i)
    {
        float w = 0.25F, h = 0.25F;

        if (sp[i].cls != NULL && strcmp(sp[i].cls, "EntityFallingBlock") == 0)
        {
            w = 0.98F;
            h = 0.98F;
        }
        else if (sp[i].cls != NULL && strcmp(sp[i].cls, "EntityLightningBolt") == 0)
        {
            w = 0.0F;
            h = 0.0F;
        }

        room_boxes(s);

        struct sp_box *b = &s->boxes[s->nboxes++];
        double half = (double)(w / 2.0F);

        b->min_x = sp[i].x - half;
        b->max_x = sp[i].x + half;
        b->min_y = sp[i].y;
        b->max_y = sp[i].y + (double)h;
        b->min_z = sp[i].z - half;
        b->max_z = sp[i].z + half;
        b->prevent = 0;
        b->list = -1;
        b->has_home = 0;
    }
}

/* The tick hook: the servertick's state is the spawner's inputs. */
void spawner_tick(void *ctx, struct servertick *st)
{
    struct spawner *s = ctx;

    s->rand = &ST_RAND(st);
    s->world_time = st->world_time;
    s->total_time = st->total_time;
    s->skylight_subtracted = st->skylight_subtracted;
    /* World.isThundering, the weighted thunder strength over 0.9, not the
     * WorldInfo flag (EntityMob.isValidLightLevel reads it) */
    s->thundering = servertick_is_thundering(st);
    /* the world's difficultySetting as it stands (a dev op can change it) */
    s->difficulty = st->difficulty;
    s->animal_gate = st->total_time % 400L == 0L;

    s->nout = 0;

    s->in_tick = 1;
    find_chunks(s);
    s->in_tick = 0;
}

/* --------------------------------------------- performWorldGenSpawning */

/* SpawnerAnimals.performWorldGenSpawning, one populate call: the biome's
 * creature list, the group walk over the populate chunk rand (pop_rand, the
 * caller's p->rand), the weighted pick off World.rand, the constructors off
 * Det's role. The spawn order is the walk's: the group's animals one by one,
 * each at the attempt that passed. */
int spawner_world_gen(struct spawner *s, int biome, int bx, int bz)
{
    const struct sp_list *l = spawning_list_for(biome, CT_CREATURE);
    int made = 0;
    struct sp_data data;

    memset(&data, 0, sizeof data);

    if (l->n == 0) return 0;

    jrand *walk = s->pop_rand;


    /* while (p_77191_6_.nextFloat() < p_77191_1_.getSpawningChance()) */
    while (jr_float(walk) < 0.1F)
    {
        int entry = weighted_pick(s, l);
        int group = l->min_group[entry] + jr_int_n(walk, 1 + l->max_group[entry] - l->min_group[entry]);
        int wx = bx + jr_int_n(walk, 16);
        int wz = bz + jr_int_n(walk, 16);
        int sx = wx, sz = wz;

        for (int i = 0; i < group; ++i)
        {
            int placed = 0;

            for (int attempt = 0; !placed && attempt < 4; ++attempt)
            {
                /* p_77191_0_.getTopSolidOrLiquidBlock: from the top filled
                 * section down, the first block whose material blocks
                 * movement and is not leaves, + 1 */
                int top = -1;
                {
                    struct chunk *c = sp_chunk(s, wx >> 4, wz >> 4);

                    if (c != NULL)
                    {
                        for (top = top_filled(s, wx >> 4, wz >> 4) + 15; top > 0; --top)
                        {
                            int id = sp_block(s, wx, top, wz) & 4095;

                            if (MATERIALS[BLOCKS[id].material].blocks_movement &&
                                strcmp(MATERIALS[BLOCKS[id].material].name, "leaves") != 0) break;
                        }

                        top = top > 0 ? top + 1 : -1;
                    }
                }

                if (can_spawn_at_location(s, CT_CREATURE, wx, top, wz))
                {
                    struct sp_rec r;
                    float yaw = jr_float(walk) * 360.0F;

                    rec_construct(s, &r, l->kind[entry]);
                    rec_spawn(s, &r, (double)wx + 0.5, (double)top, (double)wz + 0.5, yaw);
                    egg_on(s, s->out[s->nout - 1], &data);
                    s->out[s->nout - 1]->rnd = det_rng_state(&s->out[s->nout - 1]->e);
                    ++made;
                    placed = 1;
                }

                wx += jr_int_n(walk, 5) - jr_int_n(walk, 5);

                int nz;

                for (nz = wz + jr_int_n(walk, 5) - jr_int_n(walk, 5);
                     wx < bx || wx >= bx + 16 || nz < bz || nz >= bz + 16;
                     nz = sz + jr_int_n(walk, 5) - jr_int_n(walk, 5))
                {
                    wx = sx + jr_int_n(walk, 5) - jr_int_n(walk, 5);
                }

                wz = nz;
            }
        }
    }

    return made;
}
