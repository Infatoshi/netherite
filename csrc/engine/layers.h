/* The Minecraft 1.7.10 biome layer stack (GenLayer), ported line for line.
 *
 * A layer maps an area (x, z, w, h) of its own grid to biome ints. Most layers
 * read a slightly larger area of their parent. Each call seeds a per-cell LCG
 * from the layer's world seed and the cell position, so results depend only on
 * the seed and position, never on call order.
 */
#ifndef NETHERITE_LAYERS_H
#define NETHERITE_LAYERS_H

#include <stdint.h>

enum layer_kind {
    L_ISLAND, L_ZOOM, L_FUZZY_ZOOM, L_ADD_ISLAND, L_REMOVE_OCEAN, L_ADD_SNOW,
    L_EDGE_COOL_WARM, L_EDGE_HEAT_ICE, L_EDGE_SPECIAL, L_ADD_MUSHROOM, L_DEEP_OCEAN,
    L_RIVER_INIT, L_BIOME, L_BIOME_EDGE, L_HILLS, L_RIVER, L_SMOOTH, L_RARE_BIOME,
    L_SHORE, L_RIVER_MIX, L_VORONOI,
};

struct layer {
    enum layer_kind kind;
    int64_t base_seed;
    int64_t world_seed;      /* stays 0 unless initialized: see layers_init */
    struct layer *p1, *p2;   /* parent; second input of hills and river mix */
};

#define LAYER_MAX 64

struct layers {
    struct layer node[LAYER_MAX];
    int n;
    struct layer *gen;       /* 1/4 resolution: WorldChunkManager.genBiomes */
    struct layer *full;      /* block resolution: biomeIndexLayer (voronoi) */
};

void layers_init(struct layers *g, int64_t world_seed);

/* The layer's raw ints for area (x, z, w, h), row-major (z outer). */
void layer_ints(const struct layer *l, int x, int z, int w, int h, int *out);

/* WorldChunkManager.getBiomesForGeneration / loadBlockGeneratorData: raw ints
 * mapped through the registry to the id of the biome object (161 -> 160). */
void biomes_gen(const struct layers *g, int x, int z, int w, int h, int *out);
void biomes_full(const struct layers *g, int x, int z, int w, int h, int *out);

/* Chunk (cx, cz)'s two queries from one run of the generation layer: its
 * raw ints over biomes_gen's area (cx * 4 - 2, cz * 4 - 2, 10, 10) into raw,
 * and then biomes_gen's ids for that area into gen, and biomes_full(cx * 16,
 * cz * 16, 16, 16) into full. The voronoi's parent area (cx * 4 - 1,
 * cz * 4 - 1, 6, 6) lies inside biomes_gen's, and every layer's value at a
 * cell depends on the cell alone, not on the area asked for, so the answers
 * are the two calls'. */
void biomes_chunk(const struct layers *g, int cx, int cz, int *gen, int *full);

#endif
