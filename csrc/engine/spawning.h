/* SpawnerAnimals.findChunksForSpawning and
 * SpawnerAnimals.performWorldGenSpawning, bit for bit: the eligible chunk set
 * in its Java HashMap order, the per-type caps over the counted records, the
 * pack walk's Random draws, getCanSpawnHere per type with its light and block
 * rules, and onSpawnWithEgg's equipment. The spawned mobs are constructed
 * records (class, position, the Det draws already spent): the mob lanes tick
 * them. See spawning.c. */
#ifndef NETHERITE_SPAWNING_H
#define NETHERITE_SPAWNING_H

#include <stdint.h>

#include "det.h"
#include "jrand.h"
#include "servertick.h"
#include "world.h"

struct entity;

/* The kinds the overworld spawn lists carry, after WorldConf.pruneSpawns took
 * the horses, wolves and ocelots out. */
enum {
    SP_ZOMBIE = 0, SP_SKELETON, SP_SPIDER, SP_CREEPER, SP_SLIME, SP_ENDERMAN, SP_WITCH,
    SP_SHEEP, SP_PIG, SP_CHICKEN, SP_COW, SP_MOOSHROOM,
    SP_BAT, SP_SQUID,
    /* the Nether's lists (spawning_hell_list), which the Nether's spawner
     * draws (the replay's possible_creatures) */
    SP_GHAST, SP_PIG_ZOMBIE, SP_MAGMA_CUBE, SP_BLAZE,
    SP_KINDS
};

/* EnumCreatureType, values() order. */
enum { CT_MONSTER = 0, CT_CREATURE, CT_AMBIENT, CT_WATER, CT_TYPES };

/* Records the spawner never constructs, registered for the End's fight: the
 * dragon (an IMob, so a monster in the cap count) and an ender crystal (only
 * its box, which prevents spawning). */
enum { SP_DRAGON = 100, SP_CRYSTAL = 101 };
/* A living no EnumCreatureType counts (a villager, an iron golem): only its
 * box, which prevents spawning. */
enum { SP_OTHER = 102 };

/* The caps (EnumCreatureType.maxNumberOfCreature). */
enum { CAP_MONSTER = 70, CAP_CREATURE = 10, CAP_AMBIENT = 15, CAP_WATER = 5 };

/* A biome's spawn list: one row per entry, the weights the lists carry. */
struct sp_list {
    int n;
    int kind[8];
    int weight[8];
    int min_group[8], max_group[8];
};

/* The lists per biome id, the way BiomeGenBase builds them plus the
 * subclasses' changes: mushroom island (and its shore biome 15, which shares
 * the island's object in the registry) carries mooshroom only, the swamps add
 * a slime monster row, the oceans, rivers, deserts, snow plains and mesas
 * (and their mutations) no creatures, everything else the base lists
 * (checked against BiomeSpawnProbe by tests/test_biome_spawn.c). */
const struct sp_list *spawning_list_for(int biome, int type);

/* MapGenScatteredFeature.getScatteredFeatureSpawnList: the witch list the
 * ChunkProviderGenerate.getPossibleCreatures hands the spawner inside a swamp
 * hut (EntityWitch, weight 1, one mob, one mob). */
const struct sp_list *spawning_scattered_witch(void);

struct populate;

/* ChunkProviderHell.getPossibleCreatures(type, x, y, z): for the monster
 * type the fortress list (MapGenNetherBridge.spawnList: blaze 10 2-3, pig
 * zombie 5, skeleton 10, magma cube 3, all 4-4 but the blaze) inside a
 * fortress (populate_hell_fortress_at over the Nether driver's fortress map,
 * below the block id under the point), else the hell biome's lists (ghast
 * 50, pig zombie 100, magma cube 1, all 4-4; the other types empty). *via
 * gets the fortress test's answer (0 biome, 1 hasStructureAt, 2 the
 * func_142038_b branch). */
const struct sp_list *spawning_hell_list(struct populate *p, int type, int x, int y, int z,
                                         int below, int *via);

/* One mob the spawner constructed: the fields the recording's spawn line
 * carries, plus the box the later getCanSpawnHere queries read. */
struct sp_rec {
    int kind;
    int id;
    int alive;
    /* EntityLiving.isNoDespawnRequired (persistenceRequired): World.
     * countEntities leaves such a living out of the caps */
    int persistent;
    int64_t uuid_msb, uuid_lsb;
    double x, y, z;
    float yaw;
    uint64_t rnd;                 /* the entity's own Random, Det.newRandom's state */
    uint64_t seeder_before, math_before, world_before_egg;
    int next_id_before;
    double follow_bonus, knockback_bonus;
    int64_t follow_msb, follow_lsb;       /* the two spawn bonus modifiers' UUIDs */
    int64_t knockback_msb, knockback_lsb;
    double leader_reinforcements, leader_health;
    int64_t leader_reinforcements_msb, leader_reinforcements_lsb;
    int64_t leader_health_msb, leader_health_lsb;
    int child, villager, pickup, slime_size, fleece, skel_type;
    int break_doors, leader, chicken_jockey;
    int spider_potion;            /* the pack's EntitySpider.GroupData effect, 0 none */
    int eq_item[5], eq_dmg[5], eq_cnt[5];
    int eq_tag[5];                /* the tag in the item tag store, 0 for none */
    int riding, ridden_by;        /* mount links, -1 */
    /* 0, or the id of the mob whose getCanSpawnHere was running when this
     * worldgen spawn happened (a chunk load inside the check): it joined
     * loadedEntityList before that mob, whose spawnEntityInWorld follows */
    int order_key;
    /* the chunk section list's stamp at spawnEntityInWorld (entity_chunk_stamp
     * then): an entity a chunk load later in the pass brings joins the list
     * after it, though the record is adopted after the pass */
    uint64_t chunk_stamp;
    /* the two EntityLivingBase fields the constructor's Math.random draws set */
    float field_70770_ap, field_70769_ao;
    /* the box, Entity.setSize and setPosition, and its index in the spawner's
     * box list (the child zombie's shrink patches it) */
    double min_x, min_y, min_z, max_x, max_y, max_z;
    int box;
    /* the entity's own Random, still carrying its state after the spawn path */
    det_rng e;
};

/* The pack's IEntityLivingData, the state onSpawnWithEgg passes on. */
struct sp_data {
    int zombie_made, child, villager;   /* EntityZombie.GroupData, made once per pack */
    int spider_made, spider_potion;     /* EntitySpider.GroupData, HARD only */
};

/* The spawner's fixed capacities: the records and boxes of every entity it
 * has seen (append-only), the tick's spawn list, the id index. */
#define SP_MAX_RECS (1 << 17)
#define SP_MAX_BOXES (1 << 17)
#define SP_MAX_OUT 4096

struct spawner {
    /* findChunksForSpawning's eligible chunks in the HashMap's order for the
     * player's chunk (pcx, pcz): a function of the two alone (jorder.c's
     * window), kept while the player stays in the chunk */
    int elig_valid, elig_pcx, elig_pcz, elig_n;
    int elig_order[289 * 2];
    struct world *w;
    det_state *det;
    jrand *rand;                  /* World.rand: the tick walk's draws, the
                                   * worldgen spawning's picks and egg paths */
    jrand *pop_rand;              /* the worldgen walk's own stream (the
                                   * populate chunk rand); NULL in the tick
                                   * role, whose walk draws from World.rand */
    int role;                     /* whose Det streams the constructors draw: DET_SERVER for the tick spawner, DET_OTHER in the populate stage */
    void (*ensure_chunk)(void *ctx, int cx, int cz);
    void *ensure_ctx;
    int in_tick;
    int checking_id;              /* the constructed mob under getCanSpawnHere, 0 outside */
    int difficulty;
    int skylight_subtracted;      /* the pre-tick value the light checks read */
    int raining, thundering;
    int64_t world_time, total_time;
    double player_x, player_y, player_z;
    int spawn_x, spawn_y, spawn_z;
    int cal_month, cal_day;       /* the calendar the bat rule reads */
    int64_t seed;

    /* the spawn flags WorldServer.tick passes in */
    int hostile, peaceful, animal_gate;

    /* The world's provider: dim -1 reads WorldProviderHell's light
     * brightness table and its skeleton egg branch; possible_creatures is
     * ChunkProviderServer.getPossibleCreatures when the provider overrides
     * the biome's list (ChunkProviderHell's fortress list), NULL for the
     * biome's own. */
    int dim;
    const struct sp_list *(*possible_creatures)(void *ctx, int type, int x, int y, int z);
    void *possible_ctx;
    /* World.checkNoEntityCollision's other entities: 1 when an entity the
     * boxes do not carry (the replay's falling blocks and primed TNT, which
     * set preventEntitySpawning) is live in b; NULL for none */
    int (*blocked)(void *ctx, const struct aabb *b);
    void *blocked_ctx;

    struct sp_rec *recs;
    int nrecs, caprecs;
    int egg_depth;              /* onSpawnWithEgg nesting: rec_spawn grows recs only outside it */
    /* recs by id: rec_index[slot] holds an id and its first record plus one
     * (at 0: empty), covering recs[0 .. rec_indexed); records are only ever
     * appended and an id never changes (spawning.c, rec_first). The table
     * hashes into its first rec_index_mask + 1 slots, at most half full, so
     * a lookup reads a few lines of a table sized to the records, and no
     * record. */
    struct sp_rec_slot { int32_t id, at; } *rec_index;
    int rec_index_cap, rec_indexed, rec_index_mask;
    /* per record: bit 0 alive, bit 1 persistent (the record's own fields,
     * kept beside them), bits 2 up the kind's creature type plus one (0:
     * none): World.countEntities reads these bytes, not the records */
    uint8_t *rec_flags;

    /* every entity's box the two collision checks read: the mobs above, the
     * player, and the item, falling-block and lightning records the tick body
     * constructed (none of them prevent spawning, but all of them collide) */
    struct sp_box {
        double min_x, min_y, min_z, max_x, max_y, max_z;
        int prevent;          /* checkNoEntityCollision's filter */
        int cx, cz;           /* the chunk list it sits in */
        int list;             /* index into lists, or -1 once gone */
        /* a living's own chunk and section (its position's): the Java query
         * reaches an entity only through the chunk lists two blocks past
         * the box, which the dragon's 16-wide box can outreach */
        int has_home, hcx, hcy, hcz;
    } *boxes;
    int nboxes, capboxes;
    int player_box;           /* the player's box in boxes, -1 before spawner_set_player */

    /* the per-chunk index into boxes: entities the Java query walks in chunk
     * entity lists */
    struct sp_entlist { int cx, cz; int *idx; int n, cap; } *lists;
    int nlists, caplists;

    /* the negative checks */
    int neg_cap_type;             /* a type whose cap changes */
    int neg_cap_add;              /* the delta */
    int neg_pack;                 /* 1: the pack walk's four-attempt cap moves */

    /* the last run's spawn records, for the test: pointers into recs, in
     * spawn order (onSpawnWithEgg's equipment and mounts land after the
     * spawn, so the test reads them through the adopted record) */
    struct sp_rec **out;
    int nout, capout;
};

/* Install: the hook field of the servertick points at spawner_tick with this
 * struct as the context. */
void spawner_init(struct spawner *s, struct world *w, det_state *det, jrand *rand);
void spawner_free(struct spawner *s);

/* The static scene the spawner sees: the parked player (its box joins the
 * queries) and the box of one non-living entity the tick body constructed. */
void spawner_set_player(struct spawner *s, double x, double y, double z);
void spawner_set_spawn_types(struct spawner *s, int hostile, int peaceful);
void spawner_set_scene(struct spawner *s, int64_t seed, int difficulty,
                       int cal_month, int cal_day, int spawn_x, int spawn_y, int spawn_z);
float spawner_local_difficulty(struct spawner *s, double x, double y, double z);
/* getSavedLightValue, getBlockLightValue (at s->skylight_subtracted) and the
 * provider's brightness table, for the tile spawner's getCanSpawnHere. */
int spawner_saved_light(struct spawner *s, int type, int x, int y, int z);
int spawner_block_light_value(struct spawner *s, int x, int y, int z);
float spawner_brightness(int dim, int light);

/* Adopt the tick body's spawns (item, falling block, lightning) after the
 * tick: their boxes join the colliding check. */
void spawner_absorb(struct spawner *s, const struct st_spawn *sp, int n);


void spawner_track(struct spawner *s, int id, const struct entity *e);
/* Between ticks: the records of entities gone for good (dead, and the only
 * record of their id) and their boxes out of the arrays, the rest in their
 * order; nothing a query or a count reads changes. Only for a world whose
 * gone entities never come back under their id (the overworld: a reloaded
 * mob is a new entity; the Nether keeps some whole, the End its dragon). */
void spawner_compact(struct spawner *s);
/* A Dev summon of kind at (x, y, z): the constructor and onSpawnWithEgg draws,
 * the record left in out (not in the spawn list). */
int spawner_summon(struct spawner *s, int kind, double x, double y, double z,
                   int child, int size, struct sp_rec *out);
/* ItemMonsterPlacer.spawnCreature of kind at (x, y, z): the constructor, the
 * World.rand yaw, onSpawnWithEgg and playLivingSound's pitch draws, the
 * record left in out (not in the spawn list). */
int spawner_spawn_creature(struct spawner *s, int kind, double x, double y, double z,
                           struct sp_rec *out);
/* A creature loaded from chunk NBT joins World.loadedEntityList without going
 * through this spawner's constructor path. Count its class and collision box. */
void spawner_register_living(struct spawner *s, int kind, int id, const struct entity *e);
/* EntityZombie.attackEntityFrom's HARD reinforcement: the constructor, the
 * caller's 50 position tries, the egg; 1 when one spawned (*out) */
int spawner_reinforce(struct spawner *s, det_rng *caller, int x, int y, int z, struct sp_rec *out);
/* a tracked living's persistenceRequired, which countEntities reads */
void spawner_set_persistent(struct spawner *s, int id, int persistent);

/* The hook servertick's on_spawner calls. */
void spawner_tick(void *ctx, struct servertick *st);

/* One populate call's worldgen spawning, ChunkProviderGenerate's slot: the
 * chunk walk with p->rand (owned by the driver), the pack picks off world's
 * rand and the entity constructors off Det. biome is the chunk's biome, bx/bz
 * the chunk's (cx*16+8, cz*8+16) box, exactly vanilla's call. When det is
 * NULL the stage draws the chunk rand only, which is what the block-only
 * populate recordings carry. The spawns go to out (grown), the count back. */
int spawner_world_gen(struct spawner *s, int biome, int bx, int bz);

#endif
