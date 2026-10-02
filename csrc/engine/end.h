/* The End chunk pipeline: ChunkProviderEnd, ported from
 * oracle/src/world/gen/ChunkProviderEnd.java. 128 high, so the block array is
 * 32768 cells, index x << 11 | z << 7 | y (nether.h holds that layout and the
 * block ids). The End has no structure generator and no caves. */
#ifndef NETHERITE_END_H
#define NETHERITE_END_H

#include <stdint.h>
#include "nether.h"

/* One ChunkProviderEnd, minus the provider's own bookkeeping. */
struct end {
    jrand rand;                          /* endRNG; provideChunk reseeds it per chunk */
    struct octaves gen1, gen2, gen3, gen4, gen5;
    int64_t seed;
};

void end_init(struct end *e, int64_t seed);

/* WorldProviderEnd registers WorldChunkManagerHell(BiomeGenBase.sky), so
 * loadBlockGeneratorData returns the same biome for every position. The dump's
 * biomes stage records the 256 ids of one chunk, index x + z * 16. */
void end_biomes(uint8_t *biomes);

/* initializeNoiseField: the 3x33x3 density field for chunk-grid origin
 * (x, 0, z), index (i * 3 + j) * 33 + y (public for the CUDA check). */
void end_field(struct end *e, int x, int z, double *field);

/* func_147420_a: the density pass. End stone where the density is positive,
 * air elsewhere. Ids only; metas stay zero. Its biome argument is unused in
 * vanilla, so there is none here. */
void end_terrain(struct end *e, int cx, int cz, uint16_t *blocks);

/* func_147421_b: the surface pass. Vanilla only rewrites stone, and the End's
 * density pass writes end stone, so this is a no-op today; it is ported
 * literally so it stays one. Its biome argument is unused in vanilla. */
void end_surface(int cx, int cz, uint16_t *blocks);

#endif
