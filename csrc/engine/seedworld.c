/* The seed-world build, see seedworld.h. One pass, from the seed:
 *
 *   1. the world objects the Java side builds and the Det seeder draws their
 *      own Randoms take, in the Java order (World.rand first);
 *   2. WorldServer.createSpawnPosition: the biome search over the generator's
 *      1/4-resolution layer, then the fuzz loop, whose getTopBlock reads
 *      generate chunks through the same provider a run's chunk load goes
 *      through;
 *   3. MinecraftServer.initialWorldChunkLoad's 625 chunk loads;
 *   4. Chunk.populateChunk's four-neighbour rule after every load (world.c's
 *      on_chunk hook), so a chunk a populate call generates itself runs the
 *      rule too, exactly as ChunkProviderServer.loadChunk does;
 *   5. ChunkProviderServer.populate's body for every chunk the rule picks:
 *      func_150809_p, the population Random's seed, the four structure
 *      generators, the lakes, the dungeons, the biome's decorate,
 *      SpawnerAnimals.performWorldGenSpawning and the freeze pass.
 *
 * The world core writes everything: world_set_block (its listeners, the block
 * callbacks, the queued light), the scheduled-update list in ticks.c, the tile
 * entities. Rows.blkHash's chain is rebuilt from world->on_block, so every
 * changed write the build makes is part of the compared state.
 */
#include "seedworld.h"
#include "env.h"

#include "blockcb.h"
#include "blocks.h"
#include "chunkgen.h"
#include "det.h"
#include "features_nether.h"
#include "jrand.h"
#include "populate.h"
#include "populate_nether.h"
#include "randomtick.h"
#include "seedworld_creatures.h"
#include "structure_blocks.h"
#include "ticks.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Block ids the build names, Block.getIdFromBlock. */
enum { SW_BLOCK_AIR = 0, SW_BLOCK_GRASS = 2, SW_BLOCK_WATER = 8, SW_BLOCK_STILL_WATER = 9, SW_BLOCK_BEDROCK = 7 };

/* WorldProviderSurface.getAverageGroundLevel, the y the spawn point is set to. */
#define SW_AVERAGE_GROUND_LEVEL 64

/* BiomeGenBase.getSpawningChance. */
#define SW_SPAWNING_CHANCE 0.1F

static const char *const SW_CLASS_NAME[] = {
    "EntitySheep", "EntityPig", "EntityChicken", "EntityCow", "EntityMooshroom",
    "EntityItem", "EntityVillager", "EntityWitch", "EntityDragon", "EntityEnderCrystal"
};

/* Rows.onBlock's chain, over the writes the world core reports. */
#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

static void sw_on_block(void *ctx, int x, int y, int z, int id, int meta)
{
    struct seedworld *s = ctx;

    /* Rows.onBlock's metadata-only marker: world.c's write listener reports the
     * 16-bit 0xffff the probes' record uses, Java passes -1 */
    if (id == 0xffff) id = -1;

    uint64_t h = s->blk_hash;

    /* Rows.onBlock hashes w.provider.dimensionId first: 0 the overworld's
     * WorldServer, -1 and 1 the two WorldServerMulti worlds */
    h = (h ^ (uint64_t)(int64_t)s->dim) * FNV_PRIME;
    h = (h ^ (uint64_t)(int64_t)x) * FNV_PRIME;
    h = (h ^ (uint64_t)(int64_t)y) * FNV_PRIME;
    h = (h ^ (uint64_t)(int64_t)z) * FNV_PRIME;
    h = (h ^ (uint64_t)(int64_t)id) * FNV_PRIME;
    h = (h ^ (uint64_t)(int64_t)meta) * FNV_PRIME;
    s->blk_hash = h;
    ++s->blk_count;
}

/* ------------------------------------------------------------ the entities */

static struct sw_entity *sw_new(struct seedworld *s, int cls)
{
    if (s->nents == s->capents)
    {
        s->capents = s->capents ? s->capents * 2 : 256;
        s->ents = realloc(s->ents, sizeof *s->ents * (size_t)s->capents);

        if (s->ents == NULL) abort();
    }

    struct sw_entity *e = &s->ents[s->nents++];
    memset(e, 0, sizeof *e);
    e->cls = cls;
    e->item = -1;
    e->damage = -1;
    e->count = -1;
    return e;
}

/* World.spawnEntityInWorld's guard: `if (!forceSpawn && !this.chunkExists(floor(
 * posX / 16), floor(posZ / 16))) return false`. The entity is constructed either
 * way (its Det draws are spent), but an item a drop threw over the edge of the
 * loaded set never reaches loadedEntityList, so nothing records it. */
static int sw_entity_spawned(struct seedworld *s, double x, double z)
{
    return world_chunk_loaded(&s->p.world, (int)floor(x / 16.0), (int)floor(z / 16.0));
}

/* Entity's constructor: the per-role id, the entity's own Random (one seeder
 * draw) and its UUID (two more). */
static void sw_entity_base(struct seedworld *s, struct sw_entity *e, det_rng *rand_out)
{
    e->id = det_next_entity_id_role(&s->det, DET_SERVER);
    *rand_out = det_new_random_role(&s->det, DET_SERVER);
    det_uuid_role(&s->det, DET_SERVER, &e->uuid_msb, &e->uuid_lsb);
}

/* EntityLivingBase.<init>: the three Math.random draws of its body, after
 * Entity's constructor and before the subclass's. None is read here (the
 * yaw setLocationAndAngles writes replaces the third), the stream is. */
static void sw_living_base(struct seedworld *s)
{
    (void)((float)(det_math_random_role(&s->det, DET_SERVER) + 1.0) * 0.01F);
    (void)((float)det_math_random_role(&s->det, DET_SERVER) * 12398.0F);
    (void)((float)(det_math_random_role(&s->det, DET_SERVER) * 3.141592653589793 * 2.0));
}

/* EntityLiving.onSpawnWithEgg, and the two overrides in the way:
 *   - `new AttributeModifier("Random spawn bonus", this.rand.nextGaussian() *
 *     0.05, 1)`: the Gaussian draws on the entity's own Random, then the
 *     constructor's Det.uuid();
 *   - EntitySheep.onSpawnWithEgg's setFleeceColor(AnimalColor values, from
 *     EntitySheep.getRandomFleeceColor(World.rand)): one nextInt(100), and a
 *     second nextInt(500) only for the 0 sheep at the plain colour. */
static void sw_on_spawn_with_egg(struct seedworld *s, struct sw_entity *e, det_rng *r)
{
    e->follow_bonus = det_rng_gaussian(r) * 0.05;

    int64_t msb, lsb;
    det_uuid_role(&s->det, DET_SERVER, &msb, &lsb);
    e->follow_msb = msb;
    e->follow_lsb = lsb;

    if (e->cls == SW_SHEEP)
    {
        int roll = jr_int_n(&s->world_rand, 100);

        e->fleece = roll < 5 ? 15 : roll < 10 ? 7 : roll < 15 ? 8 : roll < 18 ? 12
            : jr_int_n(&s->world_rand, 500) == 0 ? 6 : 0;
    }
}

/* The subclass constructor's own draw on the entity's Random: of the four
 * creature classes in the spawn lists only EntityChicken's constructor touches
 * it (`timeUntilNextEgg = rand.nextInt(6000) + 6000`). */
static void sw_subclass_ctor(struct sw_entity *e, det_rng *r)
{
    if (e->cls == SW_CHICKEN) (void)jr_int_n(&r->r, 6000);
}

/* SpawnerAnimals.performWorldGenSpawning's construction of one creature, at
 * the position its caller computed. */
static void sw_creature(struct seedworld *s, int cls, double x, double y, double z, jrand *poprand)
{
    struct sw_entity *e = sw_new(s, cls);
    det_rng r;

    sw_entity_base(s, e, &r);
    sw_living_base(s);
    sw_subclass_ctor(e, &r);

    e->x = x;
    e->y = y;
    e->z = z;
    /* setLocationAndAngles(..., p_77191_6_.nextFloat() * 360.0F, 0.0F): the yaw
     * draw happens between the constructor and the call */
    e->yaw = jr_float(poprand) * 360.0F;

    sw_on_spawn_with_egg(s, e, &r);
    e->rand_state = det_rng_state(&r);
    e->rand = r;

    if (!sw_entity_spawned(s, x, z)) --s->nents;
}

/* Block.dropBlockAsItem_do's entity: the EntityItem constructor's four
 * Math.random draws (hoverStart, rotationYaw, motionX, motionZ) and
 * delayBeforeCanPickup = 10. The item stack is the drop's. */
static struct sw_entity *sw_item(struct seedworld *s, double x, double y, double z, int item, int damage, int count)
{
    struct sw_entity *e = sw_new(s, SW_ITEM);
    det_rng r;

    sw_entity_base(s, e, &r);

    (void)(float)(det_math_random_role(&s->det, DET_SERVER) * 3.141592653589793 * 2.0);   /* hoverStart */
    e->x = x;
    e->y = y;
    e->z = z;
    e->yaw = (float)(det_math_random_role(&s->det, DET_SERVER) * 360.0);
    e->motion_x = (double)(float)(det_math_random_role(&s->det, DET_SERVER) * 0.20000000298023224 - 0.10000000149011612);
    e->motion_y = 0.20000000298023224;
    e->motion_z = (double)(float)(det_math_random_role(&s->det, DET_SERVER) * 0.20000000298023224 - 0.10000000149011612);
    e->item = item;
    e->damage = damage;
    e->count = count;
    e->rand_state = det_rng_state(&r);
    return e;
}

/* randomtick.c's drop sink: the world-Random draws are already spent, so only
 * the entity is left. */
static void sw_drop_sink(void *ctx, double x, double y, double z, int item, int damage, int count)
{
    struct seedworld *s = ctx;

    sw_item(s, x, y, z, item, damage, count);

    if (!sw_entity_spawned(s, x, z)) --s->nents;
}

/* blockcb.c's environment sink, for the torch and rail support drops: that
 * path spends the entity draws itself and hands them over. */
static void sw_item_sink(void *ctx, int entity_id, uint64_t rand_state, int64_t uuid_msb, int64_t uuid_lsb,
                         double x, double y, double z, float yaw, float hover, double mx, double mz,
                         int item, int damage, int count)
{
    struct seedworld *s = ctx;
    struct sw_entity *e = sw_new(s, SW_ITEM);

    (void)hover;
    e->rand_state = (uint64_t)rand_state;
    e->id = entity_id;
    e->uuid_msb = uuid_msb;
    e->uuid_lsb = uuid_lsb;
    e->x = x;
    e->y = y;
    e->z = z;
    e->yaw = yaw;
    e->motion_x = mx;
    e->motion_y = 0.20000000298023224;
    e->motion_z = mz;
    e->item = item;
    e->damage = damage;
    e->count = count;

    if (!sw_entity_spawned(s, x, z)) --s->nents;
}

/* A structure piece's entity, handed over at the moment the piece spawns it:
 *
 *   - a corridor's chest cart is built and filled but never spawned while
 *     config.yaml keeps minecarts off, so it is not part of the world at all;
 *   - an item a piece dropped (a torch that could not stay, a rail a flow
 *     destroyed) is spawned, and the chunk test decides whether it reaches
 *     loadedEntityList;
 *   - a village's villager and a swamp hut's witch are spawned (no seed of the
 *     five has one yet, so their records are untested).
 */
static void sw_sc_ent(void *ctx, const struct sc_ent *e)
{
    struct seedworld *s = ctx;

    if (e->kind == SC_CART) return;

    int cls = e->kind == SC_ITEM ? SW_ITEM : (e->kind == SC_VILLAGER ? SW_VILLAGER : SW_WITCH);
    struct sw_entity *out = sw_new(s, cls);

    out->id = e->entity_id;
    out->rand_state = e->rand_state;
    out->uuid_msb = e->uuid_msb;
    out->uuid_lsb = e->uuid_lsb;
    out->x = e->x;
    out->y = e->y;
    out->z = e->z;
    out->yaw = e->yaw;
    out->motion_x = e->motion_x;
    out->motion_z = e->motion_z;

    if (e->kind == SC_ITEM)
    {
        out->motion_y = e->spilled ? e->motion_y : 0.20000000298023224;
        out->item = e->item;
        out->damage = e->damage;
        out->count = e->count;
    }

    if (!sw_entity_spawned(s, e->x, e->z)) --s->nents;
}

/* WorldGenSpikes' ender crystal, the entity the End's spikes spawn: the Entity
 * constructor's id, its own Random and its UUID, then the crystal's own
 * constructor draw (innerRotation = rand.nextInt(100000)), then the position
 * setLocationAndAngles writes. y and yaw are the feature's own report: the
 * feature passes (x + 0.5, y, z + 0.5) and the crystal's yOffset (its height
 * over two, one) makes the recorded y the peak's top plus one. */
static void sw_crystal(struct seedworld *s, double x, double y, double z, float yaw)
{
    struct sw_entity *e = sw_new(s, SW_CRYSTAL);
    det_rng r;

    sw_entity_base(s, e, &r);
    e->crystal_rotation = jr_int_n(&r.r, 100000);

    e->x = x;
    e->y = y;
    e->z = z;
    e->yaw = yaw;
    e->rand_state = det_rng_state(&r);
    e->rand = r;

    if (!sw_entity_spawned(s, x, z)) --s->nents;
}

/* BiomeEndDecorator's dragon at chunk 0,0: EntityDragon's constructor is the
 * Entity constructor (id, its own Random, its UUID) then EntityLivingBase's
 * three Math.random draws (none of them read: setLocationAndAngles replaces the
 * yaw), then the seven EntityDragonParts, each a full Entity constructor of its
 * own - they take seven ids and three seeder draws each, and none of them is
 * spawned. EntityLiving's constructor draws nothing.
 *
 * setLocationAndAngles(0.0D, 128.0D, 0.0D, rand.nextFloat() * 360.0F, 0.0F):
 * yOffset is 0 for a dragon (nothing sets it after Entity's default), and the
 * yaw is the decorator Random's draw, which the feature reported. */
static void sw_dragon(struct seedworld *s, float yaw)
{
    struct sw_entity *e = sw_new(s, SW_DRAGON);
    det_rng r;

    sw_entity_base(s, e, &r);
    sw_living_base(s);

    for (int i = 0; i < 7; ++i)
    {
        (void)det_next_entity_id_role(&s->det, DET_SERVER);
        det_rng part = det_new_random_role(&s->det, DET_SERVER);
        int64_t msb, lsb;

        (void)part;
        det_uuid_role(&s->det, DET_SERVER, &msb, &lsb);
    }

    e->x = 0.0;
    e->y = 128.0;
    e->z = 0.0;
    e->yaw = yaw;
    e->rand_state = det_rng_state(&r);
    e->rand = r;

    if (!sw_entity_spawned(s, e->x, e->z)) --s->nents;
}

/* --------------------------------------------------------- the world reads */

/* World.isAirBlock. */
static int sw_air_at(struct world *w, int x, int y, int z)
{
    return BLOCKS[world_get_block(w, x, y, z) & 4095].material == 0;   /* Material.air */
}

/* World.getTopBlock: from y 63 up while the block above is not air, the block
 * there. Every read generates the chunk it lands in. */
static int sw_top_block(struct world *w, int x, int z)
{
    int y = 63;

    while (!sw_air_at(w, x, y + 1, z)) ++y;

    return world_get_block(w, x, y, z) & 4095;
}

/* World.getTopSolidOrLiquidBlock: from the top filled section down, the first
 * block whose material blocks movement and is not leaves, + 1. */
static int sw_top_solid_or_liquid(struct world *w, int x, int z)
{
    struct chunk *c = world_load_chunk(w, x >> 4, z >> 4);
    int lx = x & 15, lz = z & 15;
    int y = populate_top_filled_segment(c) + 15;

    for (; y > 0; --y)
    {
        int id = ((c->mask >> (y >> 4)) & 1) ? chunk_cell_id(c, lx << 12 | lz << 8 | y) & 4095 : 0;
        const struct material_def *m = &MATERIALS[BLOCKS[id].material];

        if (m->blocks_movement && strcmp(m->name, "leaves") != 0) return y + 1;
    }

    return -1;
}

/* World.doesBlockHaveSolidTopSurface: an opaque normal block, a stair with its
 * top half, a slab with its top half, a hopper, or a full snow layer. */
static int sw_solid_top_surface(struct world *w, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z) & 4095;
    int meta = world_get_meta(w, x, y, z);
    const struct block_def *b = &BLOCKS[id];

    if (MATERIALS[b->material].is_opaque && b->normal_block) return 1;
    if (b->class_name == NULL) return 0;

    const char *cn = b->class_name;

    if (strcmp(cn, "BlockStairs") == 0) return (meta & 4) == 4;
    if (strcmp(cn, "BlockSlab") == 0) return (meta & 8) == 8;
    if (strcmp(cn, "BlockHopper") == 0) return 1;
    if (strcmp(cn, "BlockSnow") == 0) return (meta & 7) == 7;

    return 0;
}

/* SpawnerAnimals.canCreatureTypeSpawnAtLocation for the creature type (whose
 * material is air, so the block-surface branch runs). */
static int sw_can_spawn_at(struct world *w, int x, int y, int z)
{
    if (!sw_solid_top_surface(w, x, y - 1, z)) return 0;

    int below = world_get_block(w, x, y - 1, z) & 4095;

    if (below == SW_BLOCK_BEDROCK) return 0;
    if (BLOCKS[world_get_block(w, x, y, z) & 4095].normal_cube) return 0;
    if (MATERIALS[BLOCKS[world_get_block(w, x, y, z) & 4095].material].is_liquid) return 0;
    if (BLOCKS[world_get_block(w, x, y + 1, z) & 4095].normal_cube) return 0;

    return 1;
}

/* --negative=earlypop: armed after the spawn search, so the first chunk the
 * 625-chunk loop loads is the one populated before its neighbours exist. */
#define sw_neg_early_armed (nw_env->seedworld.neg_early_armed)

/* ------------------------------------------------------- the provider path */

/* The list a biome's creature type spawns from, seedworld_creatures.h. */
static const struct sw_spawn_entry *sw_creatures(int biome, int *n)
{
    int8_t idx = SW_BIOME_LIST[biome & 255];

    if (idx < 0)
    {
        fprintf(stderr, "seedworld: biome id %d has no registered biome\n", biome);
        abort();
    }

    switch (idx)
    {
    case 1: *n = (int)(sizeof SW_LIST_DEFAULT / sizeof SW_LIST_DEFAULT[0]); return SW_LIST_DEFAULT;
    case 2: *n = (int)(sizeof SW_LIST_MOOSHROOM / sizeof SW_LIST_MOOSHROOM[0]); return SW_LIST_MOOSHROOM;
    case 3: *n = (int)(sizeof SW_LIST_JUNGLE / sizeof SW_LIST_JUNGLE[0]); return SW_LIST_JUNGLE;
    default: *n = 0; return NULL;
    }
}

/* SpawnerAnimals.performWorldGenSpawning(world, biome, bx, bz, 16, 16, rand):
 * the biome's creature list, the group size, the position walk and the four
 * attempts per member. */
static void sw_spawning(struct seedworld *s, int biome, int cx, int cz)
{
    struct populate *p = &s->p;
    jrand *r = &p->rand;
    int n;
    const struct sw_spawn_entry *list = sw_creatures(biome, &n);
    int bx = cx * 16 + 8, bz = cz * 16 + 8;

    if (n == 0) return;

    while (jr_float(r) < SW_SPAWNING_CHANCE)
    {
        int total = 0;

        for (int i = 0; i < n; ++i) total += list[i].weight;

        /* WeightedRandom.getRandomItem(World.rand, list): one nextInt(total)
         * on World.rand, then the walk */
        int pick = jr_int_n(&s->world_rand, total);
        int k = 0;

        while (k < n - 1 && (pick -= list[k].weight) >= 0) ++k;

        const struct sw_spawn_entry *e = &list[k];
        int count = e->min + jr_int_n(r, 1 + e->max - e->min);
        int x = bx + jr_int_n(r, 16);
        int z = bz + jr_int_n(r, 16);
        int x0 = x, z0 = z;

        for (int member = 0; member < count; ++member)
        {
            int done = 0;

            for (int attempt = 0; !done && attempt < 4; ++attempt)
            {
                int y = sw_top_solid_or_liquid(&p->world, x, z);

                if (sw_can_spawn_at(&p->world, x, y, z))
                {
                    float fx = (float)x + 0.5F;
                    float fy = (float)y;
                    float fz = (float)z + 0.5F;

                    sw_creature(s, e->cls, (double)fx, (double)fy, (double)fz, r);
                    done = 1;
                }

                x += jr_int_n(r, 5) - jr_int_n(r, 5);

                for (z += jr_int_n(r, 5) - jr_int_n(r, 5);
                     x < bx || x >= bx + 16 || z < bz || z >= bz + 16;
                     z = z0 + jr_int_n(r, 5) - jr_int_n(r, 5))
                    x = x0 + jr_int_n(r, 5) - jr_int_n(r, 5);
            }
        }
    }
}

/* ChunkProviderServer.populate's body: ChunkProviderGenerate.populate for one
 * chunk, in vanilla's order. */
static void sw_populate_body(struct seedworld *s, struct chunk *c)
{
    struct populate *p = &s->p;
    int cx = c->cx, cz = c->cz;
    int biome = world_get_biome(&p->world, cx * 16 + 16, cz * 16 + 16);
    int village = 0;

    /* ChunkProviderGenerate.populate holds BlockFalling.fallInstantly for the
     * whole call */
    ticks_set_fall_instantly(1);

    populate_seed(p, cx, cz);

    for (int g = 0; g < 4; ++g)
    {
        int any = populate_structures(p, g, cx, cz);

        if (g == 1) village = any;
        }

    populate_lakes(p, cx, cz, village);
    populate_dungeons(p, cx, cz);
    decorator_decorate(p, biome, cx, cz);
    sw_spawning(s, biome, cx, cz);
    populate_freeze(p, cx, cz);

    ticks_set_fall_instantly(0);

    /* ChunkProviderServer.populate's setChunkModified after the body sets
     * isModified, a save flag the native chunk does not carry; field_150815_m
     * is what the chunk's own tick (Chunk.func_150804_b) raises, never a
     * populate call, so nothing here touches it. */
}

/* ChunkProviderHell.populate for one chunk: the fortress stage (every sizeable
 * start whose box meets the populate box, through the shared piece executor),
 * then the eight feature stages in vanilla order. hellRNG is the provider's own
 * Random, the same stream provideChunk reseeds, and the world core writes every
 * block, so the pending lava and fire ticks land in the compared state. */
static void sw_populate_hell(struct seedworld *s, struct chunk *c)
{
    struct populate *p = &s->p;
    int cx = c->cx, cz = c->cz;

    /* ChunkProviderHell.populate holds BlockFalling.field_149832_M for the
     * whole call */
    ticks_set_fall_instantly(1);

    populate_hell_structures(p, cx, cz);
    populate_hell_lava(p, cx, cz);
    populate_hell_fire(p, cx, cz);
    populate_hell_glow1(p, cx, cz);
    populate_hell_glow2(p, cx, cz);
    populate_hell_brown(p, cx, cz);
    populate_hell_red(p, cx, cz);
    populate_hell_quartz(p, cx, cz);
    populate_hell_hidden(p, cx, cz);

    ticks_set_fall_instantly(0);
}

/* ChunkProviderEnd.populate: BiomeEndDecorator.func_150513_a over the End
 * World's rand - the ores (every vein draws and places nothing: the End has no
 * stone), the spike and its ender crystal, then the dragon at chunk 0,0. The
 * world has no structure generator, no creatures and no freeze pass. */
static void sw_populate_end(struct seedworld *s, struct chunk *c)
{
    struct populate *p = &s->p;
    int cx = c->cx, cz = c->cz;

    /* ChunkProviderEnd.populate holds BlockFalling.field_149832_M too */
    ticks_set_fall_instantly(1);

    populate_end_ores(p, cx, cz);

    /* the feature clears its report at every generate, so a call the spike
     * chance skips leaves nothing standing */
    spike_crystal_last.spawned = 0;
    populate_end_spike(p, cx, cz);

    /* WorldGenSpikes' EntityEnderCrystal: constructed and spawned inside the
     * feature, after its stone checks and its obsidian, before the dragon */
    if (spike_crystal_last.spawned)
        sw_crystal(s, spike_crystal_last.x, spike_crystal_last.y, spike_crystal_last.z, spike_crystal_last.yaw);

    populate_end_dragon(p, cx, cz);

    if (end_dragon_last.spawned) sw_dragon(s, end_dragon_last.yaw);

    ticks_set_fall_instantly(0);
}

/* ChunkProviderServer.populate for one chunk. */
static void sw_populate(struct seedworld *s, int cx, int cz)
{
    struct chunk *c = world_chunk(&s->p.world, cx, cz);

    if (c == NULL || c->terrain_populated) return;

    populate_150809_p(&s->p, c);

    if (s->dim == 0) sw_populate_body(s, c);
    else if (s->dim == -1) sw_populate_hell(s, c);
    else sw_populate_end(s, c);
}

/* Chunk.populateChunk's four-neighbour rule, run after every chunk load
 * (world.c's on_chunk): the chunk itself, then west, north and northwest when
 * their companion chunks are loaded and the target is still unpopulated. The
 * conditions are the decompiled ones (the north test spells its companion
 * check twice), the same the chunkload lane's order recording carries. */
static void sw_populate_chunk(struct seedworld *s, int cx, int cz)
{
    struct world *w = &s->p.world;

    /* the negative check: the first chunk the spawn loop loads is populated at
     * once, before its three companions exist, and the rest run the real rule */
    if (sw_neg_early_armed)
    {
        sw_neg_early_armed = 0;
        sw_populate(s, cx, cz);
        return;
    }

    if (world_chunk_loaded(w, cx + 1, cz + 1)
        && world_chunk_loaded(w, cx, cz + 1) && world_chunk_loaded(w, cx + 1, cz))
        sw_populate(s, cx, cz);

    if (world_chunk_loaded(w, cx - 1, cz) && world_chunk_loaded(w, cx - 1, cz + 1) && world_chunk_loaded(w, cx, cz + 1))
        sw_populate(s, cx - 1, cz);

    if (world_chunk_loaded(w, cx, cz - 1) && world_chunk_loaded(w, cx + 1, cz - 1) && world_chunk_loaded(w, cx + 1, cz))
        sw_populate(s, cx, cz - 1);

    if (world_chunk_loaded(w, cx - 1, cz - 1) && world_chunk_loaded(w, cx, cz - 1) && world_chunk_loaded(w, cx - 1, cz))
        sw_populate(s, cx - 1, cz - 1);
}

/* world.c's on_chunk: every generated chunk extends the dimension's structure
 * maps (MapGenBase.func_151539_a: the overworld's four types, or the Nether's
 * fortress map; the End has none), then the provider's populate rule runs. */
static void sw_on_chunk(void *ctx, int cx, int cz)
{
    struct seedworld *s = ctx;

    if (s->dim == 0) populate_offer_chunk(&s->p, cx, cz);
    else if (s->dim == -1) populate_hell_offer_chunk(&s->p, cx, cz);

    sw_populate_chunk(s, cx, cz);
}

/* ChunkProviderServer.loadChunk. */
static void sw_load(struct seedworld *s, int cx, int cz)
{
    world_load_chunk(&s->p.world, cx, cz);
}

/* --------------------------------------------------------- the spawn point */

/* WorldServer.createSpawnPosition, in full: the biome search, then the fuzz
 * loop's canCoordinateBeSpawn test, which generates the chunks it reads. */
static void sw_spawn_position(struct seedworld *s)
{
    int sx, sz;
    uint64_t state;

    populate_biome_spawn_search(s->p.world.seed, &sx, &sz, &state);

    jrand r;
    r.seed = state;

    int x = sx, z = sz;
    int guard = 0;

    while (sw_top_block(&s->p.world, x, z) != SW_BLOCK_GRASS)
    {
        if (nw_env->cfg.seedworld_negative_fuzz_order)
        {
            /* the negative check: the z step's two draws come first */
            z += jr_int_n(&r, 64) - jr_int_n(&r, 64);
            x += jr_int_n(&r, 64) - jr_int_n(&r, 64);
        }
        else
        {
            x += jr_int_n(&r, 64) - jr_int_n(&r, 64);
            z += jr_int_n(&r, 64) - jr_int_n(&r, 64);
        }

        if (++guard == 1000) break;
    }

    s->spawn_x = x;
    s->spawn_y = SW_AVERAGE_GROUND_LEVEL;
    s->spawn_z = z;
}

/* ------------------------------------------------------------------- build */

/* Everything the two builds share: the world object, the Det stream and the
 * hooks every chunk load, block write and entity construction goes through.
 * The WorldServers' own draws are the caller's, before this. */
static void sw_init_shared(struct seedworld *s, int64_t seed, int dim)
{
    memset(s, 0, sizeof *s);
    s->blk_hash = FNV_OFFSET;
    s->dim = dim;

    det_init(&s->det);
    det_set_role(&s->det, DET_SERVER);
    det_reset(&s->det, seed);

    populate_init(&s->p, seed);
    /* the dimension's chunk pipeline: dim 0 the generator the p.world holds,
     * -1 and 1 world.c's providers (load_dim_chunk) */
    s->p.world.dim = dim;
    s->p.world.on_chunk = sw_on_chunk;
    s->p.world.on_chunk_ctx = s;
    s->p.world.on_block = sw_on_block;
    s->p.world.on_block_ctx = s;

    /* The two SplitRandoms the Java classes have registered by this point
     * (Item.itemRand and EnchantmentHelper.enchantmentRand, both untouched):
     * det.nbt lists every registered split, used or not. */
    (void)det_split_random(&s->det, "./net/minecraft/item/Item.java:itemRand");
    (void)det_split_random(&s->det, "./net/minecraft/enchantment/EnchantmentHelper.java:enchantmentRand");

    /* the structure walk's World.setRandomSeed calls land in World.rand, and the
     * pieces construct their entities (the corridor chest carts config.yaml never
     * spawns) from the SERVER role's streams */
    populate_world_rand = &s->world_rand;
    populate_det = &s->det;
    populate_on_ent = sw_sc_ent;
    populate_on_ent_ctx = s;

    /* the world's own Random for the update ticks the population runs at once */
    ticks_reset(0);
    ticks_set_total_time(0);
    ticks_set_rand(&s->world_rand);

    /* the block callbacks' and the drop helpers' streams */
    randomtick_tick_rand(&s->world_rand);
    randomtick_tick_det(&s->det, DET_SERVER);
    randomtick_tick_drop_sink(sw_drop_sink, s);
    nw_env->blockcb.env.world_rand = &s->world_rand;
    nw_env->blockcb.env.det = &s->det;
    nw_env->blockcb.env.role = DET_SERVER;
    nw_env->blockcb.env.item_drop = sw_item_sink;
    nw_env->blockcb.env.ctx = s;
    nw_env->blockcb.env.item_spill = NULL;
}

void seedworld_init(struct seedworld *s, int64_t seed)
{
    sw_init_shared(s, seed, 0);

    /* The first WorldServer's World constructor draws twice, in World's field
     * declaration order: updateLCG first (`(new Random()).nextInt()`), then
     * World.rand (`new Random()`). The other worlds' pairs and the generator's
     * own Randoms follow. */
    det_rng lu = det_new_random_role(&s->det, DET_SERVER);
    s->update_lcg = det_rng_int(&lu);

    det_rng wr = det_new_random_role(&s->det, DET_SERVER);
    s->world_rand.seed = det_rng_state(&wr);

    /* The remaining thirteen: the two extra worlds' four draws (updateLCG and
     * rand each), the eight chunk-generator MapGenBase instances (the
     * overworld's six, the Nether's two), and MapGenStronghold's
     * once-per-world placement Random. None of them is read: they only advance
     * the seeder, which det.nbt's SERVER state compares. The overworld,
     * Nether and End worlds are three WorldServers, six World draws in all. */
    for (int i = 0; i < 13; ++i) (void)det_new_random_role(&s->det, DET_SERVER);
}

/* The dimension build's prologue: the same fifteen draws in the order
 * MinecraftServer.loadAllWorlds makes them, but this time the dimension's own
 * pair is read.
 *
 *   1     world 0's World: updateLCG
 *   2     world 0's World: rand
 *   3..8  world 0's six MapGenBase instances (ChunkProviderGenerate's fields)
 *   9     MapGenStronghold's placement Random: drawn lazily, when the
 *         overworld's createSpawnPosition first offers a candidate chunk to it,
 *         so it lands before world 1 exists
 *   10/14 world 1's (the Nether's) and world 2's (the End's) updateLCG
 *   11/15 their World.rand
 *   12/13 the Nether's two MapGenBase instances (MapGenNetherBridge then
 *         MapGenCavesHell, ChunkProviderHell's field order); the End's provider
 *         has none
 *
 * World's constructor then draws one value from its own rand for
 * ambientTickCountdown, which is why the two states below are stepped once:
 * worlds.nbt records what the field holds afterwards.
 */
void seedworld_init_dim(struct seedworld *s, int64_t seed, int dim)
{
    if (dim != -1 && dim != 1) abort();

    sw_init_shared(s, seed, dim);

    (void)det_new_random_role(&s->det, DET_SERVER);   /* 1: world 0 updateLCG */
    (void)det_new_random_role(&s->det, DET_SERVER);   /* 2: world 0 rand */

    for (int i = 0; i < 6; ++i) (void)det_new_random_role(&s->det, DET_SERVER);   /* 3..8 */
    (void)det_new_random_role(&s->det, DET_SERVER);   /* 9: stronghold */

    if (dim == -1)
    {
        det_rng lcg = det_new_random_role(&s->det, DET_SERVER);   /* 10 */
        s->update_lcg = det_rng_int(&lcg);

        det_rng wr = det_new_random_role(&s->det, DET_SERVER);    /* 11 */
        s->world_rand.seed = det_rng_state(&wr);
        (void)jr_int_n(&s->world_rand, 12000);

        for (int i = 0; i < 4; ++i) (void)det_new_random_role(&s->det, DET_SERVER);   /* 12..15 */

        /* the fortress map, MapGenNetherBridge's once-per-world state */
        populate_hell_init(&s->p, seed);
    }
    else
    {
        for (int i = 0; i < 4; ++i) (void)det_new_random_role(&s->det, DET_SERVER);   /* 10..13 */

        det_rng lcg = det_new_random_role(&s->det, DET_SERVER);   /* 14 */
        s->update_lcg = det_rng_int(&lcg);

        det_rng wr = det_new_random_role(&s->det, DET_SERVER);    /* 15 */
        s->world_rand.seed = det_rng_state(&wr);
        (void)jr_int_n(&s->world_rand, 12000);

        /* ChunkProviderEnd.populate hands BiomeEndDecorator this.worldObj.rand,
         * the End World's rand: the same stream the driver holds as end_rand
         * (nothing else draws it, the End's populate schedules no tick) */
        s->p.end_rand.seed = s->world_rand.seed;
    }
}

void seedworld_move_det(struct seedworld *to, struct seedworld *from)
{
    /* the dimension's own prologue streams (the same fifteen draws' tail) are
     * dropped: the run's Det state is the overworld build's */
    det_free(&to->det);
    to->det = from->det;
    memset(&from->det, 0, sizeof from->det);
}

void seedworld_free(struct seedworld *s)
{
    populate_free(&s->p);
    det_free(&s->det);
    free(s->ents);
    memset(s, 0, sizeof *s);
}

void seedworld_build(struct seedworld *s)
{
    sw_spawn_position(s);
    sw_neg_early_armed = nw_env->cfg.seedworld_negative_early_pop;

    /* MinecraftServer.initialWorldChunkLoad: x outer, z inner, step 16 */
    for (int a = -192; a <= 192; a += 16)
        for (int b = -192; b <= 192; b += 16)
            sw_load(s, (int)((s->spawn_x + a) >> 4), (int)((s->spawn_z + b) >> 4));
}

/* The dimension's spawn area. WorldServer.createSpawnPosition takes its
 * !canRespawnHere branch for both providers, so the spawn point is
 * (0, WorldProvider.getAverageGroundLevel(), 0) and the biome search, the fuzz
 * loop and the chunks they would read never run; then
 * MinecraftServer.initialWorldChunkLoad's loop, over the same 625 chunk
 * positions the overworld's load covers. */
void seedworld_build_dim(struct seedworld *s)
{
    s->spawn_x = 0;
    s->spawn_y = s->dim == -1 ? 64 : 50;   /* WorldProvider.getAverageGroundLevel / WorldProviderEnd's */
    s->spawn_z = 0;

    for (int a = -192; a <= 192; a += 16)
        for (int b = -192; b <= 192; b += 16)
            sw_load(s, (int)((s->spawn_x + a) >> 4), (int)((s->spawn_z + b) >> 4));

    /* The End's decorate draws from the End World's rand, which the driver holds
     * as p.end_rand (seeded from the same stream state): the world's own copy
     * has to follow, that is the state worldstate.nbt carries. */
    if (s->dim == 1) s->world_rand.seed = s->p.end_rand.seed;

    /* the negative check: the first chunk the loop loads takes one block the
     * generator did not put there, so the comparison names that chunk */
    if (nw_env->cfg.seedworld_negative_dim_block)
    {
        int cx = (int)(s->spawn_x + -192) >> 4, cz = (int)(s->spawn_z + -192) >> 4;

        world_set_block(&s->p.world, cx * 16 + 1, 40, cz * 16 + 1, s->dim == -1 ? 87 : 121, 0, 2);
    }
}

const struct sw_entity *seedworld_entities(const struct seedworld *s, int *n)
{
    *n = s->nents;
    return s->ents;
}

const char *seedworld_class_name(int cls)
{
    return cls >= 0 && cls < SW_CLASSES ? SW_CLASS_NAME[cls] : "?";
}
