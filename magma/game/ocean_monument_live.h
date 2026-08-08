#ifndef MAGMA_GAME_OCEAN_MONUMENT_LIVE_H
#define MAGMA_GAME_OCEAN_MONUMENT_LIVE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned short (*GmMonumentGetBlock)(
    void *opaque, int x, int y, int z);
typedef void (*GmMonumentSetBlock)(
    void *opaque, int x, int y, int z, unsigned short state);
typedef void (*GmMonumentSpawnElder)(
    void *opaque, double x, double y, double z);

typedef struct {
    void *opaque;
    GmMonumentGetBlock get_block;
    GmMonumentSetBlock set_block;
    GmMonumentSpawnElder spawn_elder;
    int sea_level;
} GmMonumentAccess;

/* MapGenOceanMonument's exact region candidate, excluding biome viability. */
void gm_monument_candidate_for_region(
    long long world_seed, int region_x, int region_z,
    int *chunk_x, int *chunk_z);
int gm_monument_candidate(
    long long world_seed, int chunk_x, int chunk_z);

/* Build the 1.11.2 room graph and place the full 58x23x58 structure. The
 * caller supplies the real world state because foundations and fill-only
 * surfaces intentionally inspect terrain. Returns the horizontal facing
 * index (2=N, 3=S, 4=W, 5=E), or zero for invalid arguments. */
int gm_monument_generate(
    long long world_seed, int start_chunk_x, int start_chunk_z,
    const GmMonumentAccess *access);

#ifdef __cplusplus
}
#endif
#endif
