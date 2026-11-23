/* The Nether chunk pipeline: ChunkProviderHell and MapGenCavesHell, ported
 * from oracle/src/world/gen/. Both dimensions are 128 high (not the overworld's
 * 256), so their block arrays are 32768 cells, index x << 11 | z << 7 | y; the
 * layout and the block ids live here because end.h needs them too. */
#ifndef NETHERITE_NETHER_H
#define NETHERITE_NETHER_H

#include <stdint.h>
#include "jrand.h"
#include "noise.h"

#define DIM_CELLS 32768
#define DIM_CELL(x, y, z) ((x) << 11 | (z) << 7 | (y))

enum {
    DIM_AIR = 0, DIM_STONE = 1, DIM_GRASS = 2, DIM_DIRT = 3, DIM_BEDROCK = 7,
    DIM_FLOWING_LAVA = 10, DIM_LAVA = 11, DIM_GRAVEL = 13, DIM_NETHERRACK = 87,
    DIM_SOUL_SAND = 88, DIM_END_STONE = 121,
    /* both dimensions are one BiomeGenBase everywhere: WorldProviderHell and
     * WorldProviderEnd both register a WorldChunkManagerHell */
    DIM_BIOME_HELL = 8, DIM_BIOME_SKY = 9,
};

/* One ChunkProviderHell, minus the Nether fortress generator (a separate lane)
 * and the provider's own bookkeeping. */
struct nether {
    jrand rand;                          /* hellRNG; provideChunk reseeds it per chunk */
    struct octaves gen1, gen2, gen3;     /* netherNoiseGen1..3 */
    struct octaves slowsand_gravel;      /* slowsandGravelNoiseGen */
    struct octaves exclusivity;          /* netherrackExculsivityNoiseGen */
    struct octaves gen6, gen7;           /* netherNoiseGen6, netherNoiseGen7 */
    int64_t seed;
};

void nether_init(struct nether *n, int64_t seed);

/* initializeNoiseField: the 5x17x5 density field for chunk-grid origin
 * (x, 0, z), index (i * 5 + j) * 17 + y (public for the CUDA check). */
void nether_field(struct nether *n, int x, int z, double *field);

/* func_147419_a: the density pass. Netherrack where the density is positive,
 * lava below y = 32, air elsewhere. Ids only; metas stay zero. */
void nether_terrain(struct nether *n, int cx, int cz, uint16_t *blocks);

/* func_147418_b: the surface pass (soul sand, gravel, the lava ocean and the
 * bedrock floor). Draws from n->rand, which provideChunk reseeds per chunk;
 * the noise tables themselves are fixed at construction. */
void nether_surface(struct nether *n, int cx, int cz, uint16_t *blocks);

/* MapGenCavesHell.func_151539_a over chunk (cx, cz), in place. Self-contained:
 * the carver's rand comes from the world seed, not from the provider's. */
void nether_caves(int64_t seed, int cx, int cz, uint16_t *blocks);

#endif
