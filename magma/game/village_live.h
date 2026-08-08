#ifndef MAGMA_GAME_VILLAGE_LIVE_H
#define MAGMA_GAME_VILLAGE_LIVE_H

#include <stdint.h>
#include "mc_rng.h"

enum {
    GM_VILLAGE_START = 0,
    GM_VILLAGE_PATH,
    GM_VILLAGE_TORCH,
    GM_VILLAGE_HOUSE4_GARDEN,
    GM_VILLAGE_CHURCH,
    GM_VILLAGE_HOUSE1,
    GM_VILLAGE_WOOD_HUT,
    GM_VILLAGE_HALL,
    GM_VILLAGE_FIELD1,
    GM_VILLAGE_FIELD2,
    GM_VILLAGE_HOUSE2,
    GM_VILLAGE_HOUSE3
};

enum {
    GM_VILLAGE_PLAINS = 0,
    GM_VILLAGE_DESERT = 1,
    GM_VILLAGE_SAVANNA = 2,
    GM_VILLAGE_TAIGA = 3
};

enum {
    GM_VILLAGE_NORTH = 2,
    GM_VILLAGE_SOUTH = 3,
    GM_VILLAGE_WEST = 4,
    GM_VILLAGE_EAST = 5,
    GM_VILLAGE_MAX_PIECES = 256
};

/* Village/WorldSavedData is distinct from the structure-piece graph above.
 * These bounds cover the loaded single-player runtime without allocation;
 * operations fail atomically when a deliberately oversized fixture exceeds
 * them. UUIDs are stored as the two raw 64-bit Java UUID halves. */
enum {
    GM_VILLAGE_STATE_DOORS = 256,
    GM_VILLAGE_STATE_REPUTATIONS = 32,
    GM_VILLAGE_STATE_AGGRESSORS = 32
};

typedef struct {
    int x, y, z;
    int inside_dx, inside_dz;
    int timestamp;
    int restriction;
    unsigned char detached;
} GmVillageDoorState;

typedef struct {
    uint64_t uuid_most, uuid_least;
    int score;
} GmVillageReputationState;

typedef struct {
    int eid;
    int timestamp;
} GmVillageAggressorState;

typedef struct {
    int num_villagers;
    int radius;
    int num_golems;
    int last_add_door_timestamp;
    int tick_counter;
    int no_breed_ticks;
    int center_x, center_y, center_z;
    int helper_x, helper_y, helper_z;
    GmVillageDoorState doors[GM_VILLAGE_STATE_DOORS];
    int door_count;
    GmVillageReputationState
        reputations[GM_VILLAGE_STATE_REPUTATIONS];
    int reputation_count;
    /* Entity references are a live-only Village field in Java. Persisting a
     * village deliberately drops this bounded eid/timestamp mirror. */
    GmVillageAggressorState aggressors[GM_VILLAGE_STATE_AGGRESSORS];
    int aggressor_count;
} GmVillageState;

typedef struct {
    void *ctx;
    int (*is_wood_door)(void *ctx, int x, int y, int z);
    int (*count_villagers)(void *ctx, int center_x, int center_y,
                           int center_z, int radius);
    int (*count_golems)(void *ctx, int center_x, int center_y,
                        int center_z, int radius);
    int (*area_clear)(void *ctx, int x, int y, int z,
                      int size_x, int size_y, int size_z);
    void (*spawn_golem)(void *ctx, int x, int y, int z);
    int (*entity_alive)(void *ctx, int eid);
} GmVillageStateAccess;

typedef struct {
    int min_x, min_y, min_z;
    int max_x, max_y, max_z;
} GmVillageBox;

typedef struct {
    int kind;
    int component_type;
    int facing;
    GmVillageBox box;
    /* Constructor state that consumes structure RNG: path length, field crop
     * ids, terrace/tall-house/table flags, and the Start biome/zombie flags. */
    int extra[4];
    /* Serialized StructureVillagePieces.Village placement state. */
    int average_ground_lvl;
    int placement_flags;
    int villagers_spawned;
} GmVillagePiece;

typedef struct {
    GmVillagePiece pieces[GM_VILLAGE_MAX_PIECES];
    int count;
    int valid;
    int biome_type;
    int zombie_infested;
} GmVillage;

typedef struct {
    void *ctx;
    /* Raw 1.11.2 state encoding: (block_id << 4) | legacy_meta. */
    uint16_t (*get)(void *ctx, int x, int y, int z);
    void (*set)(void *ctx, int x, int y, int z, uint16_t state);
    int (*contains)(void *ctx, int x, int y, int z);
    /* Y of the first air block above the top solid/liquid block. */
    int (*top)(void *ctx, int x, int z);
    void (*chest)(void *ctx, int x, int y, int z, int facing_meta,
                  long long loot_seed);
    void (*villager)(void *ctx, int x, int y, int z,
                     int profession, int zombie_infested);
} GmVillageAccess;

/* Direct StructureVillagePieces graph. random_seed is the state passed to
 * java.util.Random before getStructureVillageWeightedPieceList. x/z are the
 * Start well's world block coordinates, normally chunk*16+2. */
int gm_village_build(long long random_seed, int x, int z,
                     int biome_type, int size, GmVillage *out);

/* MapGenBase's exact per-start cursor, including recursiveGenerate's discarded
 * nextInt(), followed by the recursive piece graph. */
int gm_village_build_for_world(long long world_seed, int chunk_x, int chunk_z,
                               int biome_type, int size, GmVillage *out);

/* Place one graph piece into a clipped world. The caller owns the structure
 * placement Random used by MapGenStructure.generateStructure. */
int gm_village_place_piece(const GmVillageAccess *access,
                           GmVillagePiece *piece, int biome_type,
                           int zombie_infested, JavaRandom *placement_random);

/* MapGenVillage spacing candidate, independent of structure construction. */
void gm_village_candidate_for_region(long long world_seed, int region_x,
                                     int region_z, int *chunk_x, int *chunk_z);
int gm_village_candidate(long long world_seed, int chunk_x, int chunk_z);

void gm_village_state_init(GmVillageState *state);
/* Copy only Village.writeVillageDataToNBT fields. Door restriction/detached
 * flags and aggressors are intentionally reset because vanilla does not save
 * them. */
int gm_village_state_persist(
    GmVillageState *out, const GmVillageState *state);
int gm_village_state_add_door(
    GmVillageState *state, int x, int y, int z,
    int inside_dx, int inside_dz, int timestamp);
int gm_village_state_reputation(
    const GmVillageState *state, uint64_t uuid_most, uint64_t uuid_least);
int gm_village_state_modify_reputation(
    GmVillageState *state, uint64_t uuid_most, uint64_t uuid_least,
    int delta);
int gm_village_state_reputation_too_low(
    const GmVillageState *state, uint64_t uuid_most, uint64_t uuid_least);
void gm_village_state_default_reputation(
    GmVillageState *state, int delta);
int gm_village_state_add_or_renew_aggressor(
    GmVillageState *state, int eid);
int gm_village_state_aggressor_count(const GmVillageState *state);
int gm_village_state_aggressor(
    const GmVillageState *state, int index, int *eid, int *timestamp);
void gm_village_state_end_mating(GmVillageState *state);
int gm_village_state_is_mating(const GmVillageState *state);
int gm_village_state_tick(
    GmVillageState *state, int tick_counter, JavaRandom *world_random,
    const GmVillageStateAccess *access);

#endif
