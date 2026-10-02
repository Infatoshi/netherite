/* Raw chunk generation shared by the environments of one process (gencache.h). */
#define _POSIX_C_SOURCE 200809L
#include "gencache.h"

#include "chunkgen.h"
#include "env.h"
#include "nether.h"
#include "serverreplay.h"
#include "terrain.h"
#include "world.h"

#include <pthread.h>
#include <string.h>

enum { GC_SHARDS = 64 };

enum { GC_PENDING, GC_READY, GC_NONE };

/* A generation's result as the constructors take it: the overworld's ids
 * then its metadata, its biomes and the surface pass's topBlock table after
 * it; the Nether's or the End's raw ids. rand: the provider
 * Random's state after the generation. An entry is claimed PENDING by the
 * first thread to miss it (the others wait for it), then READY, or NONE
 * when its result is not kept (each thread generates it itself). */
struct gc_entry {
    int64_t seed;
    int32_t cx, cz, dim, state;
    uint64_t rand;
    uint8_t biome[256], tops[256];
    uint8_t *data;
};

struct gc_shard {
    pthread_mutex_t mu;
    pthread_cond_t cv;          /* an entry of the shard left PENDING */
    struct gc_entry **t;        /* open addressing, NULL empty */
    size_t cap, used;
};

struct gencache {
    struct gc_shard sh[GC_SHARDS];
    uint64_t max_bytes;
    uint64_t bytes, hits, misses, stored, owed_hits, full, entries;   /* atomic */
};

static uint64_t gc_hash(int64_t seed, int dim, int cx, int cz)
{
    uint64_t h = (uint64_t)seed * 0x9e3779b97f4a7c15ull;
    h ^= (uint64_t)(uint32_t)cx * 0xc2b2ae3d27d4eb4full;
    h ^= (uint64_t)(uint32_t)cz * 0x165667b19e3779f9ull;
    h ^= (uint64_t)(uint32_t)(dim + 1) * 0x27d4eb2f165667c5ull;
    h ^= h >> 32;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 29;
    return h;
}

static size_t gc_data_bytes(int dim)
{
    return dim == 0 ? CHUNK_CELLS * (sizeof(uint16_t) + 1) : DIM_CELLS * sizeof(uint16_t);
}

struct gencache *gencache_new(size_t max_bytes)
{
    struct gencache *gc = proc_calloc(1, sizeof *gc);
    if (gc == NULL) return NULL;
    for (int i = 0; i < GC_SHARDS; ++i)
    {
        pthread_mutex_init(&gc->sh[i].mu, NULL);
        pthread_cond_init(&gc->sh[i].cv, NULL);
    }
    gc->max_bytes = max_bytes;
    return gc;
}

void gencache_free(struct gencache *gc)
{
    if (gc == NULL) return;
    for (int i = 0; i < GC_SHARDS; ++i)
    {
        struct gc_shard *s = &gc->sh[i];
        for (size_t k = 0; k < s->cap; ++k)
            if (s->t[k])
            {
                proc_free(s->t[k]->data);
                proc_free(s->t[k]);
            }
        proc_free(s->t);
        pthread_mutex_destroy(&s->mu);
        pthread_cond_destroy(&s->cv);
    }
    proc_free(gc);
}

static void gc_put(struct gc_shard *s, struct gc_entry *e)
{
    uint64_t h = gc_hash(e->seed, e->dim, e->cx, e->cz);
    size_t i = (h / GC_SHARDS) & (s->cap - 1);
    while (s->t[i]) i = (i + 1) & (s->cap - 1);
    s->t[i] = e;
    ++s->used;
}

/* The entry of (seed, dim, cx, cz) once it is READY, else NULL. A missing
 * entry is claimed for the caller (*claim, PENDING: the caller generates
 * and publishes it) when claim is not NULL and the cache has room; one
 * PENDING under another thread is waited for. */
static const struct gc_entry *gc_lookup(struct gencache *gc, int64_t seed, int dim, int cx, int cz,
                                        struct gc_entry **claim)
{
    uint64_t h = gc_hash(seed, dim, cx, cz);
    struct gc_shard *s = &gc->sh[h % GC_SHARDS];
    const struct gc_entry *got = NULL;
    if (claim) *claim = NULL;
    pthread_mutex_lock(&s->mu);
    struct gc_entry *e = NULL;
    for (size_t i = (h / GC_SHARDS) & (s->cap - 1); s->cap && s->t[i]; i = (i + 1) & (s->cap - 1))
    {
        struct gc_entry *x = s->t[i];
        if (x->seed == seed && x->dim == dim && x->cx == cx && x->cz == cz) { e = x; break; }
    }
    if (e != NULL)
    {
        while (e->state == GC_PENDING) pthread_cond_wait(&s->cv, &s->mu);
        if (e->state == GC_READY) got = e;
    }
    else if (claim != NULL)
    {
        size_t bytes = sizeof *e + gc_data_bytes(dim);
        if (__atomic_add_fetch(&gc->bytes, bytes, __ATOMIC_RELAXED) > gc->max_bytes)
        {
            __atomic_fetch_sub(&gc->bytes, bytes, __ATOMIC_RELAXED);
            __atomic_fetch_add(&gc->full, 1, __ATOMIC_RELAXED);
        }
        else if ((e = proc_calloc(1, sizeof *e)) != NULL)
        {
            e->seed = seed;
            e->dim = dim;
            e->cx = cx;
            e->cz = cz;
            e->state = GC_PENDING;
            if (2 * (s->used + 1) > s->cap)
            {
                size_t cap = s->cap ? 2 * s->cap : 256;
                struct gc_entry **old = s->t;
                size_t oldcap = s->cap;
                s->t = proc_calloc(cap, sizeof *s->t);
                if (s->t == NULL) abort();
                s->cap = cap;
                s->used = 0;
                for (size_t k = 0; k < oldcap; ++k)
                    if (old[k]) gc_put(s, old[k]);
                proc_free(old);
            }
            gc_put(s, e);
            __atomic_fetch_add(&gc->entries, 1, __ATOMIC_RELAXED);
            *claim = e;
        }
    }
    pthread_mutex_unlock(&s->mu);
    return got;
}

/* a claimed entry leaves PENDING (READY with its result, or NONE) */
static void gc_publish(struct gencache *gc, struct gc_entry *e, int state)
{
    uint64_t h = gc_hash(e->seed, e->dim, e->cx, e->cz);
    struct gc_shard *s = &gc->sh[h % GC_SHARDS];
    pthread_mutex_lock(&s->mu);
    e->state = state;
    pthread_cond_broadcast(&s->cv);
    pthread_mutex_unlock(&s->mu);
    if (state == GC_READY) __atomic_fetch_add(&gc->stored, 1, __ATOMIC_RELAXED);
}

/* The C provider's generation (chunk_backend_c); a claimed entry takes its
 * result. */
static void gc_generate_c(struct gencache *gc, struct gc_entry *e, struct world *w, int cx, int cz, struct chunk *c)
{
    int keep = e != NULL;
    if (w->dim == 0)
    {
        uint16_t *ids = nw_scratch->chunkgen_ids;
        uint8_t *metas = nw_scratch->chunkgen_metas;
        int *biomes = nw_scratch->chunkgen_biomes;
        chunkgen_raw(world_gen(w), cx, cz, ids, metas, biomes);
        chunkgen_construct(c, cx, cz, ids, metas, biomes);
        if (!keep) return;
        /* the biomes and the table as bytes: kept when they fit */
        for (int i = 0; i < 256 && keep; ++i) keep = biomes[i] >= 0 && biomes[i] < 256 && w->gen.surface.top[i] < 256;
        if (keep && (e->data = proc_malloc(gc_data_bytes(0))) != NULL)
        {
            e->rand = w->gen.terrain.rand.seed;
            for (int i = 0; i < 256; ++i)
            {
                e->biome[i] = (uint8_t)biomes[i];
                e->tops[i] = (uint8_t)w->gen.surface.top[i];
            }
            memcpy(e->data, ids, CHUNK_CELLS * sizeof *ids);
            memcpy(e->data + CHUNK_CELLS * sizeof *ids, metas, CHUNK_CELLS);
        }
    }
    else
    {
        uint16_t *blocks = nw_scratch->dim_blocks;
        world_dim_raw(w, cx, cz, w->dim, blocks);
        world_dim_construct(c, cx, cz, w->dim, blocks);
        if (!keep) return;
        if ((e->data = proc_malloc(gc_data_bytes(w->dim))) != NULL)
        {
            e->rand = w->dim == -1 ? w->nether.rand.seed : w->end.rand.seed;
            memcpy(e->data, blocks, DIM_CELLS * sizeof *blocks);
        }
    }
    gc_publish(gc, e, e->data != NULL ? GC_READY : GC_NONE);
}

/* The provider state e's generation left (genahead.c ga_apply). */
static void gc_apply(struct world *w, const struct gc_entry *e)
{
    world_gen_state(w, w->dim == 0 ? e->biome : NULL, w->dim == 0 ? e->tops : NULL, e->rand);
}

/* ChunkProvider.provideChunk through the cache */
static void gc_generate(struct world *w, int cx, int cz, struct chunk *c)
{
    struct gencache *gc = w->backend_ctx;
    struct gc_entry *claim;
    const struct gc_entry *e = gc_lookup(gc, w->seed, w->dim, cx, cz, &claim);
    if (e == NULL)
    {
        __atomic_fetch_add(&gc->misses, 1, __ATOMIC_RELAXED);
        gc_generate_c(gc, claim, w, cx, cz, c);
        return;
    }
    __atomic_fetch_add(&gc->hits, 1, __ATOMIC_RELAXED);
    gc_apply(w, e);
    if (w->dim == 0)
    {
        int *biomes = nw_scratch->chunkgen_biomes;
        for (int i = 0; i < 256; ++i) biomes[i] = e->biome[i];
        chunkgen_construct(c, cx, cz, (const uint16_t *)e->data, e->data + CHUNK_CELLS * sizeof(uint16_t), biomes);
    }
    else world_dim_construct(c, cx, cz, w->dim, (const uint16_t *)e->data);
}

/* an owed generation's state (world.c generate_owed): from the cache when
 * it holds the chunk, else the world generates it (into gc_generate) */
static int gc_owed(struct world *w, int cx, int cz)
{
    struct gencache *gc = w->backend_ctx;
    const struct gc_entry *e = gc_lookup(gc, w->seed, w->dim, cx, cz, NULL);
    if (e == NULL) return 0;
    __atomic_fetch_add(&gc->owed_hits, 1, __ATOMIC_RELAXED);
    gc_apply(w, e);
    return 1;
}

static const struct chunk_backend gencache_backend = {"gencache", gc_generate, NULL, gc_owed};

static struct world *gc_world(struct serverreplay *sr, int dim)
{
    struct world *w = dim == 0 ? &sr->pop.world : dim == -1 ? &sr->hell.world : &sr->sky.world;
    return w->slot != NULL ? w : NULL;
}

void gencache_attach(struct gencache *gc, struct serverreplay *sr)
{
    for (int dim = -1; dim <= 1; ++dim)
    {
        struct world *w = gc_world(sr, dim);
        if (w == NULL) continue;
        w->backend = gc != NULL ? &gencache_backend : NULL;
        w->backend_ctx = gc;
    }
}

void gencache_stats(const struct gencache *gc, struct gencache_stats *s)
{
    memset(s, 0, sizeof *s);
    if (gc == NULL) return;
    s->hits = __atomic_load_n(&gc->hits, __ATOMIC_RELAXED);
    s->misses = __atomic_load_n(&gc->misses, __ATOMIC_RELAXED);
    s->stored = __atomic_load_n(&gc->stored, __ATOMIC_RELAXED);
    s->owed_hits = __atomic_load_n(&gc->owed_hits, __ATOMIC_RELAXED);
    s->entries = __atomic_load_n(&gc->entries, __ATOMIC_RELAXED);
    s->bytes = __atomic_load_n(&gc->bytes, __ATOMIC_RELAXED);
    s->full = __atomic_load_n(&gc->full, __ATOMIC_RELAXED);
}
