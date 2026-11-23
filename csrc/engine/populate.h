/* The populate driver: ChunkProviderServer.populate for one chunk over raw
 * terrain, in vanilla order, the reference for the oracle's PopulateProbe
 * (oracle/harness/netherite/oracle/PopulateProbe.java). The four overworld
 * structure generators generate their blocks natively: the driver holds each
 * generator's structure map (the starts, in java.util.HashMap order over their
 * chunk keys) and runs every start that meets the populate box through the
 * shared population Random, as MapGenStructure.generateStructuresInChunk
 * does. */
#ifndef NETHERITE_POPULATE_H
#define NETHERITE_POPULATE_H

#include <stdint.h>

#include "jhashmap.h"
#include "jrand.h"
#include "structure.h"
#include "world.h"

/* Stage ids, PopulateProbe.STAGES in the same order: 0..10 the populate
 * driver, 11..29 the decorator. A hook fires at each marker line with the
 * populate Random's internal seed and the call's write count so far. */
enum {
    POP_SEED, POP_MINESHAFT, POP_VILLAGE, POP_STRONGHOLD, POP_SCATTERED,
    POP_WATERLAKE, POP_LAVALAKE, POP_DUNGEONS, POP_DECORATE, POP_SPAWNING, POP_SNOW,
    POP_ORES, POP_SAND, POP_CLAY, POP_GRAVEL, POP_TREES, POP_BIGMUSHROOMS,
    POP_FLOWERS, POP_GRASS, POP_DEADBUSH, POP_WATERLILY, POP_MUSHROOMS,
    POP_MUSHROOM1, POP_MUSHROOM2, POP_REEDS, POP_REEDS10, POP_PUMPKIN, POP_CACTUS,
    POP_SPRINGS, POP_SPRINGSLAVA,
    POP_STAGES
};

struct populate_stage_hook
{
    void (*fn)(void *ctx, int stage, uint64_t seed);
    void *ctx;
};

/* One structure start as the populate driver knows it: the start itself (its
 * pieces carry their state across calls, and one whose addComponentParts
 * returns false is dropped for good), the chunk key its generator's map holds
 * it under, and StructureStart.isSizeableStructure, latched when the start was
 * built (the village's hasMoreThanTwoComponents, which does not follow its
 * piece list). */
struct pop_start
{
    struct start start;
    int sizeable;
    /* the chunks whose populate box meets the start's box and have not
     * populated since it was built: at 0 nothing can generate it again, and
     * a type nothing else reads (the mineshaft, the village) frees its
     * pieces, its key staying in the map (the HashMap order is of every key
     * ever put, as vanilla's structureMap never lets one go) */
    int pop_left;
    /* its pieces in the environment's blob file (regionspill.h), 0 while
     * they are here: a start far from the player goes there between rows
     * (populate_spill_far) and comes back when a populate box meets it */
    uint64_t blob;
};

/* One generator's structure map: the starts in the order the candidate walk
 * built them, and a java.util.HashMap over the same chunk keys for the
 * containsKey test and the iteration order
 * (MapGenStructure.generateStructuresInChunk walks values()). The map's value
 * is the start's index, so the two orders stay one array. */
struct pop_map
{
    struct pop_start *starts;
    int n, cap;
    struct jhm64 keys;
};

struct populate
{
    struct world world;

    jrand rand;                  /* ChunkProviderGenerate.rand, seeded per call */
    int64_t mul_x, mul_z;        /* func_151539_a's two placement longs */
    struct perlin temp_noise;    /* BiomeGenBase.field_150605_ac, Random(1234), 1 */
    struct perlin flower_noise;  /* field_150606_ad, Random(2345), 1 */

    struct populate_stage_hook stage;

    /* the four structure maps, in ChunkProviderGenerate field order */
    struct pop_map maps[4];

    /* scratch: the map's iteration order, grown as the largest map needs */
    int64_t *order;
    int cap_order;

    /* the dim 1 driver's own state: the End World's rand, the stream
     * BiomeEndDecorator draws from */
    jrand end_rand;
};

/* World.rand, for the structure walk: MapGenVillage's and
 * MapGenScatteredFeature's canSpawnStructureAtCoords call
 * World.setRandomSeed(x, z, salt) and then draw twice from it, so every
 * candidate chunk the walk offers reseeds and advances the real World.rand.
 * NULL when the caller keeps no World Random (the probes that only model the
 * structure maps): the same values are then computed on a private Random and
 * the world stream is left alone. Declared in structure.h, where the two bodies
 * that read it live. */

/* The Det streams the structure pieces construct their entities from, with
 * whatever role the caller set on them (the world-build lane runs as SERVER).
 * NULL - the probes - and no entity is constructed at all, so a piece's chest
 * cart spends no draws. */

/* Called for every entity a structure piece hands over, in construction order,
 * the moment the piece spawns it: the world-build lane records it there. NULL -
 * the probes - and the context keeps them to itself and frees them. */

void populate_init(struct populate *p, int64_t seed);
void populate_free(struct populate *p);
/* Between rows: the starts of map type (the mineshaft: nothing but
 * population reads its pieces) whose box lies more than reach blocks from
 * (x, z) keep their pieces in the environment's blob file until a
 * population meets them again. */
void populate_spill_far(struct populate *p, int type, double x, double z, int reach);
/* The world (the overworld or the Nether) of another seed from here on (a
 * benchmark's per-env worlds, csrc/runtime/pipe_bench.c --env-seeds): the
 * provider's generators, the placement longs and the structure types'
 * per-walk state; the chunks, the structure maps and the owed generations
 * already held stay. Outside the tick. */
void populate_reseed(struct populate *p, int64_t seed);

/* MapGenBase.func_151539_a over (cx, cz) plus range 8 for each of the four
 * overworld types, in vanilla order; every generated chunk runs it, so a
 * populate call that loads a chunk extends the maps exactly as the oracle's
 * does. Also wired to the world's on_chunk callback by populate_init. */
void populate_offer_chunk(struct populate *p, int cx, int cz);

/* A start the structureMap of type (mineshaft, village, stronghold, temple)
 * already holds, rebuilt from its chunk: a snapshot's structures.json. */
void populate_add_start(struct populate *p, int type, int ox, int oz);

/* The start at (ox, oz) as the snapshot's structures.json holds it now
 * (<Type>.pieces: per component minX minY minZ maxX maxY maxZ HPos VCount, 8
 * ints each): what a start read back from data/<Type>.dat carries and one
 * rebuilt from the seed does not. The rebuilt pieces are matched to the saved
 * ones by their x and z extents and height, take the saved box (the HPos
 * offset), HPos and VCount, and a piece population dropped is dropped.
 * Returns 0 when a saved component has no rebuilt match. */
int populate_restore_pieces(struct populate *p, int type, int ox, int oz, const int *v, int n, const int *flags);

/* WorldChunkManager.func_150795_a(0, 0, 256, biomesToSpawnIn, new
 * Random(seed)): the biome search WorldServer.createSpawnPosition runs to pick
 * the world spawn. Writes the position it picked and the 48-bit state of that
 * Random, which the caller's canCoordinateBeSpawn fuzz loop continues from
 * (the loop draws four nextInt(64) per step, so its state is world state). */
void populate_biome_spawn_search(int64_t seed, int *sx, int *sz, uint64_t *rand_state);

/* MinecraftServer.initialWorldChunkLoad: the 625 chunks the game generates
 * before any populate call, a 25x25 chunk square at 16-block spacing around
 * the world spawn (WorldServer.createSpawnPosition's biome search), offered in
 * vanilla's order (x outer). The oracle's world ran it before the probe, so
 * the four maps hold those starts too. The chunks are never loaded (only the
 * walk needs them, and the walk is a pure function of the seed). Returns the
 * number of offers. */
int populate_initial_chunks(struct populate *p);

/* One generator's structure map size, for the test's check against the
 * oracle's recorded sizes. */
int populate_map_size(struct populate *p, int type);

/* Chunk.func_150809_p: mark populated and light-populated, run the sky light
 * column check func_150811_f over the chunk's 256 columns, then the four
 * neighbours' func_150801_a relights. */
void populate_150809_p(struct populate *p, struct chunk *c);

/* ChunkProviderGenerate.populate, one piece per function, in record order:
 * seed, the four structure generators, the water and lava lakes, the eight
 * dungeon tries, biome.decorate, the ice and snow pass. Each fires the stage
 * hooks at the vanilla markers. */
void populate_seed(struct populate *p, int cx, int cz);

/* MapGenStructure.generateStructuresInChunk for one generator: every start in
 * the map, in HashMap order, that is sizeable and whose box meets the
 * populate box (cx*16+8 .. +15) generates with the shared population Random,
 * then the stage marker fires. Returns 1 when one generated, the value the
 * village generator's call hands back to gate the lakes. */
int populate_structures(struct populate *p, int type, int cx, int cz);

/* The same walk without generating: how many starts would run, for the test's
 * count check. */
int populate_structures_count(struct populate *p, int type, int cx, int cz);

void populate_lakes(struct populate *p, int cx, int cz, int village);
void populate_dungeons(struct populate *p, int cx, int cz);
void populate_freeze(struct populate *p, int cx, int cz);

/* Fire the stage hook now, for the stages the test drives (the decorate and
 * the skipped spawning marker). */
void populate_stage_now(struct populate *p, int stage);

/* The freeze temperature threshold: 0.15F, 0.25F under the negative check. */

/* The two structure negatives: walk each map in insertion order instead of
 * HashMap order, and drop the village gate the lakes read. */



/* The decorator mutations the negative checks plant: one biome's tree count and
 * one mutated biome's tree pick. */

/* BiomeDecorator.func_150512_a for the biome's decorate at the chunk. */
void decorator_decorate(struct populate *p, int biome, int cx, int cz);

/* Chunk.getTopFilledSegment and Chunk.getPrecipitationHeight (with its -999
 * cache), for the freeze pass. */
int populate_top_filled_segment(const struct chunk *c);
int populate_precipitation_height(struct populate *p, int x, int z);

/* The temperature of the column the freeze pass asks about:
 * BiomeGenBase.getFloatTemperature with its perlin above y 64. */
float populate_float_temperature(struct populate *p, int biome, int x, int y, int z);

#endif