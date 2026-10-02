/* The world a 1.7.10 server has before its first tick, built from the seed
 * alone: MinecraftServer.initialWorldChunkLoad's 625 chunks around the spawn
 * point, each generated raw, populated when Chunk.populateChunk's rule says so
 * (including the chunks a populate call loads itself, which run the rule too),
 * the pending block ticks those calls scheduled, the tile entities, and the
 * creatures SpawnerAnimals.performWorldGenSpawning constructs, with the Det
 * draws they spend.
 *
 * The Nether and the End are not loaded at world start (initialWorldChunkLoad
 * only runs for worldServers[0]), so their spawn areas never appear in a run's
 * state unless a player travels there: seedworld_init_dim/seedworld_build_dim
 * build them the way the server would on the first travel, the dimension's own
 * spawn point and the 625 chunks around it, with the two extra WorldServers'
 * draws in the prologue and the dimension's own provider as the pipeline.
 *
 * The reference is out/java/seedworld/<name>/ (netherite.oracle.SpawnDump for
 * the overworld, SpawnDumpDim for the dimensions: the snapshot format
 * csrc/engine/snapshot.c reads). csrc/tests/test_seedworld.c compares the two,
 * chunk by chunk, and stops at the first difference.
 *
 * Everything here draws its randoms from the world's own generators: the
 * population Random (populate.c's), World.rand (a jrand built by the first Det
 * seeder draw, as World's constructor field is), and Det's per-role streams for
 * the entities. Nothing is replayed from a recording.
 */
#ifndef NETHERITE_SEEDWORLD_H
#define NETHERITE_SEEDWORLD_H

#include <stdint.h>

#include "det.h"
#include "jrand.h"
#include "populate.h"
#include "world.h"

/* The creature classes the spawn lists name (seedworld_creatures.h's cls), the
 * three kinds a structure piece constructs (EntityItem for an item drop,
 * EntityVillager for a house, EntityWitch for a swamp hut), and the two the
 * End's decorator constructs: EntityDragon at chunk 0,0 and EntityEnderCrystal
 * for every spike WorldGenSpikes places. */
enum { SW_SHEEP, SW_PIG, SW_CHICKEN, SW_COW, SW_MOOSHROOM, SW_ITEM, SW_VILLAGER, SW_WITCH, SW_DRAGON, SW_CRYSTAL, SW_CLASSES };

/* One entity the world build constructed and spawned, the fields
 * SpawnDump's animals.jsonl carries. */
struct sw_entity {
    int id;                  /* Det's per-role counter, the SERVER range */
    int cls;
    int64_t uuid_msb, uuid_lsb;
    double x, y, z, motion_x, motion_y, motion_z;
    float yaw, pitch;
    uint64_t rand_state;     /* the entity's own Random, after its construction */
    det_rng rand;            /* including nextGaussian's pending value */
    double follow_bonus;
    int fleece;
    int64_t follow_msb, follow_lsb;
    int fire;
    int crystal_rotation;    /* EntityEnderCrystal.innerRotation's ctor draw */
    /* EntityItem only: the stack the drop made */
    int item, damage, count;
};

struct seedworld {
    struct populate p;       /* the generator: the world, the structure maps */
    det_state det;           /* Det's streams, the SERVER role */
    jrand world_rand;        /* World.rand */
    int update_lcg;          /* World.updateLCG, its constructor's first draw */
    int dim;                 /* 0 the overworld, -1 the Nether, 1 the End */
    int64_t spawn_x, spawn_z;
    int spawn_y;

    struct sw_entity *ents;
    int nents, capents;

    /* Rows.blkHash: the chain over every changed server block write, the
     * worldstate.nbt value the comparison checks */
    uint64_t blk_hash;
    long long blk_count;
};

/* Build the overworld for one seed. */
void seedworld_init(struct seedworld *s, int64_t seed);

/* Build one dimension's spawn area for one seed: the same world object and the
 * same hooks, with the two extra WorldServers' draws in the prologue and the
 * dimension's own provider as the chunk pipeline. dim is -1 (the Nether) or 1
 * (the End); the caller frees the fortress map with populate_hell_free for the
 * Nether. */
void seedworld_init_dim(struct seedworld *s, int64_t seed, int dim);
void seedworld_free(struct seedworld *s);

/* Det is one JVM-global object: in a real run the overworld's
 * initialWorldChunkLoad has already spent its seeder, Math and entity-id draws
 * before a dimension's spawn area loads. Hand the streams from an overworld
 * build to the dimension's build (the move leaves from empty, so the two never
 * share the split list). */
void seedworld_move_det(struct seedworld *to, struct seedworld *from);

/* WorldServer.createSpawnPosition then MinecraftServer.initialWorldChunkLoad. */
void seedworld_build(struct seedworld *s);

/* The same for a dimension: createSpawnPosition's !canRespawnHere branch sets
 * the spawn point to (0, WorldProvider.getAverageGroundLevel(), 0) - 64 for the
 * Nether, 50 for the End - and the 625 chunks around it load through the
 * dimension's provider (world.c's dim pipeline). */
void seedworld_build_dim(struct seedworld *s);

/* The entities the build constructed and spawned, in load order. */
const struct sw_entity *seedworld_entities(const struct seedworld *s, int *n);

/* The class name of one SW_* kind, as the oracle's dumps spell it. */
const char *seedworld_class_name(int cls);

/* The two negative checks (run_config, env.h). seedworld_negative_fuzz_order swaps the two draws
 * the spawn fuzz loop's x step makes for the z step's; seedworld_negative_
 * early_pop populates every chunk as it loads, before its neighbours exist. */
/* the dimension negative seedworld_negative_dim_block: one block written into the first chunk the
 * dimension's load generates, so the chunk comparison must fail naming it */

#endif
