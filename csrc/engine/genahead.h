/* Chunk generation ahead of the step (GPU plan L15).
 *
 * Raw generation (ChunkProvider.provideChunk) is a pure function of (seed,
 * dimension, cx, cz), so it can run before the tick that asks for it. Before
 * each step the host collects every environment's predicted requests (the
 * player manager's square one ring out, the spawner's 17x17 window, the
 * pathfinder's window around every living, a portal's destination) and runs
 * them as one batch through a generator (the C engine, or csrc/cuda's worldgen
 * and worldgen_dim); each result is constructed into a chunk (the Chunk constructor
 * the C provider runs) and kept in its environment's cache. Inside the tick
 * the worlds' chunk_backend (world.h) hands a cached chunk to
 * world_chunk_request and applies the provider state its generation leaves
 * (world_gen_state), so the provider holds what it would hold had it generated
 * the chunk itself; a request outside the batch falls back to the C provider
 * and is counted. An owed generation (world.c pay_owed) takes its state from a
 * cache entry that keeps only that state.
 *
 * The cache is per environment and reached from its worlds (backend_ctx); the
 * step runs between ticks, never inside one. */
#ifndef NETHERITE_GENAHEAD_H
#define NETHERITE_GENAHEAD_H

#include <stdint.h>
#include <stdio.h>

struct serverreplay;

/* One generation: a chunk of a world seed's dimension (0, -1 or 1). */
struct ga_req {
    int64_t seed;
    int32_t dim, cx, cz;
};

/* A batch generator. run() generates n requests (n <= max_batch) into
 * per-request slots: ids + i * GA_CELLS (the overworld's ids after the Chunk
 * constructor's metadata rule, index x << 12 | z << 8 | y; a Nether or End
 * request's raw array, GA_DIM_CELLS, index x << 11 | z << 7 | y), metas + i *
 * GA_CELLS, biome + i * 256 and tops + i * 256 (the overworld's
 * blockBiomeArray and the surface pass's topBlock table after the chunk), and
 * rand[i], the provider Random's internal state after the generation. 0 on
 * success; nonzero drops the batch (its requests then miss). */
#define GA_CELLS 65536
#define GA_DIM_CELLS 32768

/* An overworld chunk as the Chunk constructor and Chunk.generateSkylightMap
 * leave it (chunkgen_construct), made where it was generated
 * (csrc/cuda/worldgen's built stage, worldgen_fetch_built: the same layout):
 * the bands a block not air gives storage (mask), each band's random-tick
 * count, the height map and heightMapMinimum; beside it band k's bytes at k *
 * GA_BAND_BYTES for each band in the mask: its ids (4096, the band's index x
 * << 8 | z << 4 | y), metadata and sky light (2048 bytes of nibbles each, as
 * the band stores them). */
#define GA_BAND_BYTES 8192
struct ga_built {
    uint16_t mask;              /* bands with storage */
    uint16_t metas_any;         /* bands whose metadata is not all 0 */
    uint16_t ticking[16];
    int32_t height_min;
    int32_t height[256];
};

struct ga_gen {
    const char *name;
    void *ctx;
    int max_batch;
    int (*run)(void *ctx, const struct ga_req *req, int n, uint8_t *ids, uint8_t *metas, uint8_t *biome,
               uint8_t *tops, uint64_t *rand);
    void (*free)(void *ctx);
    /* optional: host memory run() writes into fastest (the CUDA
     * generator's pinned pages), for a caller that keeps its own output
     * buffers; NULL: malloc */
    void *(*host_alloc)(void *ctx, size_t bytes);
    void (*host_free)(void *ctx, void *p);
    /* optional: another generator like this one, independent of it (its own
     * state; the CUDA one's own stream), for a second caller thread; NULL
     * when it cannot be made */
    struct ga_gen *(*another)(const struct ga_gen *g);
    /* optional: run() with the overworld's requests built (struct ga_built:
     * the chunk as the constructor and generateSkylightMap leave it) into
     * built[i] and bands + i * 16 * GA_BAND_BYTES (band k at k *
     * GA_BAND_BYTES, bands 0 to *top_band written: the highest in any
     * request's mask, -1 none), instead of ids and metas; a Nether or End
     * request's raw array still goes to ids + i * GA_CELLS */
    int (*run_built)(void *ctx, const struct ga_req *req, int n, struct ga_built *built, uint8_t *bands,
                     int *top_band, uint8_t *ids, uint8_t *biome, uint8_t *tops, uint64_t *rand);
};

/* The C engine as a batch generator, one request at a time on shadow
 * providers (the stand-in for the CUDA one: the same seam, no device). */
struct ga_gen *ga_gen_c_create(void);
/* csrc/cuda's worldgen as one batch generator on the current CUDA
 * device (csrc/cuda/worldgen/ahead.c, linked only into the CUDA build); NULL
 * with a message when there is no device. */
struct ga_gen *ga_gen_cuda_create(void);
/* the same generators in this process, on a stream of their own, for batches
 * of max_batch over up to max_seeds world seeds (csrc/runtime/pool.c's
 * device generation; the process's grave must scan images only) */
struct ga_gen *ga_gen_cuda_local_create(int max_batch, int max_seeds);
/* the same, its stream at the device's lowest priority when low (behind the
 * renderer's; the default is its highest) */
struct ga_gen *ga_gen_cuda_local_create_prio(int max_batch, int max_seeds, int low);
void ga_gen_free(struct ga_gen *g);

/* What an environment's cache did, per dimension index (0 the overworld, 1
 * the Nether, 2 the End). */
struct ga_stats {
    uint64_t hits[3], misses[3];      /* CREQ_GENERATE served from the cache, or by C */
    uint64_t owed_hits, owed_misses;  /* owed generations (their state only) */
    uint64_t generated[3];            /* chunks the generator made for the cache */
    uint64_t owed_generated;          /* state-only generations */
    uint64_t wasted[3];               /* made, then dropped unused (left the prediction) */
    uint64_t batches, steps;
    int max_batch_seen, peak_entries;
    uint64_t peak_bytes;              /* the cached chunks' storage at the largest */
    uint64_t miss_ring[3][8];         /* misses by Chebyshev distance from the
                                       * player's chunk at the step start: 0-4 ... 29+ */
    uint64_t miss_other_dim;          /* misses in a dimension the player was not in */
    /* user instructions of the steps, when the phase profiler counts them:
     * the prediction (and the sweep), the generator's calls (the CUDA
     * generator's device work is another process's), the construction */
    uint64_t ins_predict, ins_generate, ins_construct;
    uint64_t wants, due_peeked;       /* chunks the prediction looked up, due ticks it read */
    uint64_t ns_generate, ns_construct;  /* wall time of the generator's calls and of the construction */
};

struct genahead;

struct genahead *genahead_create(void);
/* The prediction's reach, rings chunks wider than the next tick's (the
 * player's square and the pathfinders' windows): a batch asked for one step
 * ahead is not needed at once (default 0; csrc/runtime/pool.c
 * gen_ahead, lane/gpuspec). What the ticks compute is the same. */
void genahead_set_ahead(struct genahead *g, int rings);
void genahead_free(struct genahead *g);

/* The step, before an environment's tick: attach the cache to the replay's
 * worlds, predict, drop what left the prediction, generate the rest in one
 * batch per generator call and construct it. n environments share the
 * batches (a request two environments of one seed share is generated once
 * per environment: each owns its chunk). */
void genahead_step(struct genahead **g, struct serverreplay **sr, int n, const struct ga_gen *gen);

/* genahead_step's two halves, for a caller that batches the requests of
 * environments stepping on other threads (csrc/runtime/pool.c): predict
 * runs one environment's prediction (attach, predict, sweep; the environment
 * current, between ticks) and returns its new requests (*req, g's own, valid
 * until its next predict); fill hands one request's result to the cache and
 * constructs its chunk (ids NULL: the generator dropped it, and the next step
 * asks again), the environment current, between ticks. */
int genahead_predict(struct genahead *g, struct serverreplay *sr, const struct ga_req **req);
void genahead_fill(struct genahead *g, const struct ga_req *r, const uint8_t *ids, const uint8_t *metas,
                   const uint8_t *biome, const uint8_t *tops, uint64_t rand);
/* fill without constructing: the result stays where it is (the caller's,
 * holding a reference for it: owner) and its chunk is constructed when the
 * tick takes it, which releases the reference (release(owner)); a result
 * swept unused, or one the cache has no request for, is released too.
 * csrc/runtime/pool.c: the envs keep only the chunks their ticks use.
 * metas NULL: every metadata value is 0. */
void genahead_fill_raw(struct genahead *g, const struct ga_req *r, const uint8_t *ids, const uint8_t *metas,
                       const uint8_t *biome, const uint8_t *tops, uint64_t rand, void *owner,
                       void (*release)(void *owner));
/* fill_raw for a built result (ga_gen run_built's): b and bands held by
 * reference the same way, the chunk made from them when the tick takes it */
void genahead_fill_built(struct genahead *g, const struct ga_req *r, const struct ga_built *b, const uint8_t *bands,
                         const uint8_t *biome, const uint8_t *tops, uint64_t rand, void *owner,
                         void (*release)(void *owner));
/* c (a new chunk) made from a built result and the chunk's biome array, as
 * chunkgen_construct makes it from the raw arrays */
struct chunk;
void genahead_chunk_built(struct chunk *c, int cx, int cz, const struct ga_built *b, const uint8_t *bands,
                          const uint8_t *biome);
/* every reference the cache holds released (the caller drops the cache
 * without freeing it: an env's image copied over) */
void genahead_release_raw(struct genahead *g);
/* the replay's worlds back on the C provider (from genahead's backend) */
void genahead_detach(struct serverreplay *sr);

const struct ga_stats *genahead_stats(const struct genahead *g);

/* A place the next step moves the player to within its dimension, which the
 * replay knows ahead and the world does not (a Dev tp entry): the player's
 * windows around it are predicted too. */
void genahead_hint(struct genahead *g, double x, double z);

/* One line per miss to f (NULL: none): the chunk, the player's chunk at the
 * step start, the phase that asked (when the phase tracer runs). */
void genahead_log_misses(struct genahead *g, FILE *f);

/* One line: hits, misses and the hit rate per dimension, owed, generated,
 * wasted, batches, the cache's peak. */
void genahead_report(const struct genahead *g, const char *gen_name, FILE *f);

#endif
