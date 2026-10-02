/* The Nether and End populate drivers: ChunkProviderHell.populate and
 * ChunkProviderEnd.populate's BiomeEndDecorator.func_150513_a, the reference
 * for the oracle's PopulateProbe in those dimensions (the manifest's dim, the
 * test reads it). See csrc/tests/test_populate.c.
 *
 * The history of the Randoms is the world core's own. hellRNG is
 * p->world.nether.rand: every chunk the world generates reseeds it at its
 * provideChunk and func_147418_b draws from it, so a populate call starts from
 * whatever the last event left, and every chunk load the populate itself makes
 * (a fortress piece or a feature write past the loaded edge) reseeds it mid
 * call. The End's decorate draws from end_rand, the End World's own Random,
 * which nothing else touches: the test seeds it from the first call's recorded
 * start state and carries it across calls, and the decorate pieces' own chunk
 * loads never touch it (ChunkProviderEnd reseeds endRNG, a different stream).
 *
 * The fortress stage's pieces are the structure lane's work: the test replays
 * the oracle's recorded writes for a segment whose start intersects the
 * populate box, exactly as the overworld driver does, and continues hellRNG
 * from the recorded post-stage seed. The offers still grow the fortress map
 * here, one per generated chunk, because the intersect count decides which
 * segments replay. */
#ifndef NETHERITE_POPULATE_NETHER_H
#define NETHERITE_POPULATE_NETHER_H

#include "populate.h"

/* The stages of dim -1, in the order the manifest's stages list holds: a mark
 * after the fortress stage, then after each of the eight feature stages. */
enum
{
    HELL_FORTRESS, HELL_HELLLAVA, HELL_FIRE, HELL_GLOW1, HELL_GLOW2,
    HELL_BROWN, HELL_RED, HELL_QUARTZ, HELL_HIDDENLAVA,
    HELL_STAGES
};

/* The stages of dim 1: a mark after generateOres, then after the spike
 * attempt. The dragon's yaw draw comes after the last one, and nothing after
 * it draws the decorate Random. */
enum { END_ORES, END_SPIKE, END_STAGES };

/* The fortress map's once-per-walk state, at construction (the overworld
 * types' begin pass in populate_init covers dim 0 only). */
void populate_hell_init(struct populate *p, int64_t seed);

/* The fortress map is the dim -1 driver's own scratch, not a populate field:
 * free it after the last call. */
void populate_hell_free(void);

/* MapGenBase.func_151539_a for MapGenNetherBridge over chunk (cx, cz): the
 * offer every generated Nether chunk makes, candidates -8..8 around it, x
 * outer, z inner. Wire it to the world's on_chunk in the dim -1 test. */
void populate_hell_offer_chunk(struct populate *p, int cx, int cz);

/* A start the fortress map already holds (a snapshot's structures.json, in
 * the map's order), rebuilt as func_151538_a built it. */
void populate_hell_add_start(struct populate *p, int ox, int oz);

/* MapGenStructure.generateStructuresInChunk's count for the populate box: the
 * sizeable fortress starts whose bounding box intersects
 * (cx*16+8 .. +15)^2. */
int populate_hell_count(struct populate *p, int cx, int cz);

/* The fortress stage itself: every start in the map, in HashMap order, that
 * is sizeable and whose box meets the populate box generates through
 * sc_generate with hellRNG as the population Random, then the stage marker
 * fires. */
void populate_hell_structures(struct populate *p, int cx, int cz);

/* ChunkProviderHell.getPossibleCreatures' fortress test for the monster type
 * at (x, y, z) with the block id below: 1 hasStructureAt, 2 func_142038_b
 * over nether brick, 0 the biome's list (spawning.c spawning_hell_list). */
int populate_hell_fortress_at(struct populate *p, int x, int y, int z, int below);

/* The negative check nw_env->cfg.populate_hell_negative_anystart:
 * func_142038_b over every start instead of the first. */

/* ChunkProviderHell.populate's feature stages, one function per vanilla
 * segment, in vanilla order: the eight open lava springs, fire, glowstone 1,
 * glowstone 2, the brown and red mushrooms, the quartz veins and the hidden
 * lava springs. Each fires its HELL_* stage hook after its writes, from
 * hellRNG (p->world.nether.rand) and the world's own block callbacks. */
void populate_hell_lava(struct populate *p, int cx, int cz);
void populate_hell_fire(struct populate *p, int cx, int cz);
void populate_hell_glow1(struct populate *p, int cx, int cz);
void populate_hell_glow2(struct populate *p, int cx, int cz);
void populate_hell_brown(struct populate *p, int cx, int cz);
void populate_hell_red(struct populate *p, int cx, int cz);
void populate_hell_quartz(struct populate *p, int cx, int cz);
void populate_hell_hidden(struct populate *p, int cx, int cz);

/* BiomeEndDecorator.func_150513_a's three pieces, one per stage hook: the
 * ores (they draw from end_rand and place nothing, the replaced block being
 * stone), the spike with the ender crystal it would spawn, and the dragon at
 * chunk 0,0. end_rand is the End World's rand, seeded by the first populate
 * call's recorded start state and carried across calls by the test. */
void populate_end_ores(struct populate *p, int cx, int cz);
void populate_end_spike(struct populate *p, int cx, int cz);
void populate_end_dragon(struct populate *p, int cx, int cz);

/* The spike chance: 5 in vanilla, 4 with nw_env->cfg.populate_negative_spike
 * (the negative check). */

/* BiomeEndDecorator's dragon spawn at chunk 0,0, cleared at every call: 0 when
 * the chunk is not 0,0, otherwise the yaw it drew from the decorate Random.
 * Entities are not ported; the test compares this report against the oracle's
 * record. */
struct end_dragon_spawn
{
    int spawned;
    float yaw;
};

#endif