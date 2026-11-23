/* Raw chunk generation shared by the environments of one process.
 *
 * ChunkProvider.provideChunk before the Chunk constructor is a pure function
 * of the world seed, the dimension and the chunk (genahead.h), and the
 * environments of a pool start from the same few worlds and walk the same
 * ground, episode after episode. A world attached to the cache generates
 * through it: a chunk made before, by any environment, is constructed from
 * the stored result and leaves the provider state its generation leaves
 * (world_gen_state); a new one is generated as the C provider does and its
 * result stored. Either way the world gets the chunk and the state the C
 * provider would have given it, so nothing an environment computes depends
 * on the cache.
 *
 * The cache is the process's (never an environment's image), read and
 * written from every worker thread under per-shard locks; its entries never
 * change once stored and live until gencache_free. Past max_bytes it stops
 * storing (the misses generate as before). */
#ifndef NETHERITE_GENCACHE_H
#define NETHERITE_GENCACHE_H

#include <stddef.h>
#include <stdint.h>

struct gencache;
struct world;
struct serverreplay;

struct gencache *gencache_new(size_t max_bytes);
void gencache_free(struct gencache *gc);

/* the replay's server worlds generate through gc (each world's backend; a
 * world made later keeps the C provider until the next attach); gc NULL
 * gives them back the C provider */
void gencache_attach(struct gencache *gc, struct serverreplay *sr);

struct gencache_stats {
    uint64_t hits, misses, stored, owed_hits;
    uint64_t entries, bytes;
    uint64_t full;              /* misses not stored: the cache was at max_bytes */
};
void gencache_stats(const struct gencache *gc, struct gencache_stats *s);

#endif
