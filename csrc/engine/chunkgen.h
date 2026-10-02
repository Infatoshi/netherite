/* One ChunkProviderGenerate, minus the structure generators (they only record
 * a pending start; their blocks land in populate) and minus the provider's own
 * bookkeeping. The raw pipeline for one chunk, as the oracle's chunk dump
 * records it. */
#ifndef NETHERITE_CHUNKGEN_H
#define NETHERITE_CHUNKGEN_H

#include <stdint.h>
#include "surface.h"
#include "terrain.h"

struct chunk;

/* One per world. The terrain noise generators and the surface state are built
 * from the seed and reused for every chunk, exactly as the provider keeps them
 * (the Mesa band tables are one object's state, not per chunk). */
struct chunkgen {
    struct terrain terrain;
    struct surface surface;
    int64_t seed;
    /* set when the world holding this provider inserted chunks without
     * generating them (world_insert_chunk) and owes their generations;
     * chunkgen_init clears it, since a fresh provider owes nothing */
    uint8_t owed;
};

void chunkgen_init(struct chunkgen *g, int64_t seed);

/* ChunkProviderGenerate.provideChunk(cx, cz): the density pass, the full-
 * resolution biomes, the chunk-seeded surface pass, caves, ravines, then the
 * Chunk(World, Block[], byte[], int, int) constructor and
 * Chunk.generateSkylightMap. c is overwritten; the caller owns it. */
void provide_chunk(struct chunkgen *g, int cx, int cz, struct chunk *c);

/* provide_chunk's two halves: the raw pipeline (the provider rand reseeded
 * from the chunk, density, biomes, surface, caves, ravines) into ids, metas
 * (CHUNK_CELLS each, index x << 12 | z << 8 | y) and biomes (256, z << 4 | x),
 * leaving the provider state it leaves; and the Chunk constructor with
 * generateSkylightMap over them. */
void chunkgen_raw(struct chunkgen *g, int cx, int cz, uint16_t *ids, uint8_t *metas, int *biomes);
void chunkgen_construct(struct chunk *c, int cx, int cz, const uint16_t *ids, const uint8_t *metas,
                        const int *biomes);

#endif