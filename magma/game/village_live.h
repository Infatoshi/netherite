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

#endif
