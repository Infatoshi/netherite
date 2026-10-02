/* The per-biome surface pass: ChunkProviderGenerate.func_147422_a and each
 * biome's func_150573_a. */
#ifndef NETHERITE_SURFACE_H
#define NETHERITE_SURFACE_H

#include <stdint.h>
#include "terrain.h"

/* State the biome objects keep between calls. */
struct surface {
    /* topBlock of each biome object as the last surface call left it. Hills,
     * mega taiga and Savanna M rewrite it per column; caves read it back. */
    uint16_t top[256];
    /* Every Mesa object builds the same state for one world, so one copy
     * serves them all. */
    unsigned char bands[64];   /* field_150621_aC, from Random(world seed) */
    struct perlin band_shift;  /* field_150625_aG, from the same Random */
    struct perlin spire;       /* field_150623_aE, 4 octaves */
    struct perlin spire_cap;   /* field_150624_aF, 1 octave */
};

void surface_init(struct surface *s, int64_t seed);

/* func_147422_a over one chunk. biomes are the 16x16 ids from biomes_full,
 * and t->rand must already hold the chunk seed that provideChunk sets. */
void surface_pass(struct terrain *t, struct surface *s, int cx, int cz, const int *biomes,
                  uint16_t *ids, uint8_t *metas);

/* The topBlock entries a surface pass over a chunk of these biomes (256 ids,
 * any order) wrote, set to tops' (the pass's own table after it): the state
 * the pass leaves, from a pass run elsewhere. */
void surface_tops_merge(struct surface *s, const uint8_t *biome, const uint8_t *tops);

#endif
