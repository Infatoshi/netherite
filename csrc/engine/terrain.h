/* ChunkProviderGenerate (overworld, WorldType.DEFAULT), ported stage by stage. */
#ifndef NETHERITE_TERRAIN_H
#define NETHERITE_TERRAIN_H

#include <stdint.h>
#include "jrand.h"
#include "layers.h"
#include "noise.h"

/* Block array layout matches Java: index = x << 12 | z << 8 | y, 65536 cells. */
#define CHUNK_CELLS 65536
#define CELL(x, y, z) ((x) << 12 | (z) << 8 | (y))

enum {
    BLK_AIR = 0, BLK_STONE = 1, BLK_GRASS = 2, BLK_DIRT = 3, BLK_BEDROCK = 7, BLK_WATER = 9,
    BLK_SAND = 12, BLK_GRAVEL = 13, BLK_SANDSTONE = 24, BLK_ICE = 79,
    BLK_STAINED_CLAY = 159, BLK_HARDENED_CLAY = 172,
};

struct terrain {
    jrand rand;                          /* the provider's rand; provideChunk reseeds it per chunk */
    struct octaves min_limit, max_limit, main, depth5, depth, mob;
    struct perlin stone;                 /* surface depth noise (field_147430_m) */
    float parabolic[25];
    struct layers layers;
    int64_t seed;
};

void terrain_init(struct terrain *t, int64_t seed);

/* func_147423_a: the 5x33x5 density field for chunk-grid origin (x, 0, z),
 * from the 10x10 generation biomes at (x - 2, z - 2); index (i * 5 + j) * 33 + y.
 * Public so the CUDA port (csrc/cuda) can check its noise stage. */
void terrain_field(struct terrain *t, int x, int z, const int *biomes, double *field);

/* func_147424_a: the density pass. Writes stone, water or air ids. */
void terrain_density(struct terrain *t, int cx, int cz, uint16_t *blocks);
/* terrain_density with biomes_gen's 10x10 ids for the chunk given */
void terrain_density_biomes(struct terrain *t, int cx, int cz, const int *biomes, uint16_t *blocks);

#endif
