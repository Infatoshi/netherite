/* ChunkProviderGenerate.provideChunk for one chunk: the raw pipeline, then the
 * Chunk constructor and generateSkylightMap, with no population and no
 * structures. The order is the one test_worldgen.c checks against the oracle's
 * stage dumps; the provider rand is seeded from the chunk coords first, as
 * vanilla does, and only the surface pass draws from it. */
#include "chunkgen.h"
#include "env.h"
#include "carve.h"
#include "world.h"

#include <string.h>

/* provideChunk's Block[] and byte[] before the Chunk constructor copies them
 * into sections, plus the 16x16 biome ids the surface pass and the carvers
 * read. One set is enough: chunks are generated one at a time. */
#define ids (nw_scratch->chunkgen_ids)
#define metas (nw_scratch->chunkgen_metas)
#define biomes (nw_scratch->chunkgen_biomes)

void chunkgen_init(struct chunkgen *g, int64_t seed)
{
    g->seed = seed;
    g->owed = 0;
    terrain_init(&g->terrain, seed);
    surface_init(&g->surface, seed);
}

void chunkgen_raw(struct chunkgen *g, int cx, int cz, uint16_t *ids_, uint8_t *metas_, int *biomes_)
{
    /* this.rand.setSeed(cx * 341873128712 + cz * 132897987541) */
    jr_seed(&g->terrain.rand, (int64_t)((uint64_t)(int64_t)cx * 341873128712ULL +
                                        (uint64_t)(int64_t)cz * 132897987541ULL));

    /* biomes_gen's area for the terrain and biomes_full's for the chunk, from
     * one run of the layers (biomes_chunk) */
    int gen[100];
    biomes_chunk(&g->terrain.layers, cx, cz, gen, biomes_);
    terrain_density_biomes(&g->terrain, cx, cz, gen, ids_);
    memset(metas_, 0, CHUNK_CELLS);
    surface_pass(&g->terrain, &g->surface, cx, cz, biomes_, ids_, metas_);
    caves_pass(g->seed, cx, cz, biomes_, g->surface.top, ids_);
    ravines_pass(g->seed, cx, cz, biomes_, g->surface.top, ids_);
}

void chunkgen_construct(struct chunk *c, int cx, int cz, const uint16_t *ids_, const uint8_t *metas_,
                        const int *biomes_)
{
    /* new Chunk(worldObj, var3, var4, cx, cz): the constructor copies
     * biomesForGeneration into blockBiomeArray, index z << 4 | x, then
     * generateSkylightMap(); the populate flags start false. */
    chunk_construct(c, cx, cz, ids_, metas_);

    for (int i = 0; i < 256; ++i) c->biome[i] = (uint8_t)biomes_[i];
    c->terrain_populated = 0;
    c->light_populated = 0;
    generate_skylight_map(c);
}

void provide_chunk(struct chunkgen *g, int cx, int cz, struct chunk *c)
{
    chunkgen_raw(g, cx, cz, ids, metas, biomes);
    chunkgen_construct(c, cx, cz, ids, metas, biomes);
}
