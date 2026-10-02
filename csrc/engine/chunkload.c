/* ChunkProviderServer, Chunk.populateChunk's four-neighbor rule and
 * PlayerManager, the order the 1.7.10 server loads, populates and unloads
 * chunks in. Every event is reported through the installed emit callback, in
 * the exact order the hooks in oracle/src/world/gen/ChunkProviderServer.java see
 * theirs: the unload-set removal (unmark) before the load, the populate enter
 * before its loads, the provider's load before Chunk.populateChunk's
 * four-neighbor rule runs. */
#include "chunkload.h"
#include "chunkset.h"
#include "arena.h"
#include "env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PlayerManager.xzDirectionsConst: east, south, west, north. */
static const int DIRS[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};

/* The provider's chunksToUnload is a chunkset (chunkset.h). The player
 * manager's fixed lists: the send queue, the chunk watchers, the popped
 * keys. */
#define CL_MAX_QUEUE (1 << 14)
#define CL_MAX_WATCH (1 << 16)
#define CL_WATCH_INDEX (CL_MAX_WATCH * 2)
#define CL_MAX_POP (1 << 18)
#define CL_POP_INDEX (CL_MAX_POP * 2)   /* the popped keys' hash index, half full at most */

/* ------------------------------------------------------------- the state */

/* One PlayerInstance. */
struct watcher
{
    int cx, cz;
    int refs;           /* playersWatchingChunk.size() */
    int inst;           /* 1 while the instance is in playerInstances */
    int64_t prev;       /* previousWorldTime: increaseInhabitedTime's mark */
};

struct cl
{
    struct world *w;
    int spawn_x, spawn_z;
    struct chunkset unload;
    int64_t *ro;                        /* a restored queue's capture order */
    int nro, capro, ro_at;              /* the drain walks it before the bins */
    int radius;                         /* playerViewRadius */
    double px, pz;                      /* managedPosX / managedPosZ */
    int logged;                         /* the player has joined */

    struct watcher *watch;              /* playerInstances */
    int nwatch, capwatch;
    /* watch_of's index: each chunk position's newest watcher (the only one
     * that can still be an instance) as its watch[] position plus one (0:
     * empty), open addressing */
    int32_t *watchidx;

    /* EntityPlayerMP.loadedChunks: the chunks the player watches and has not
     * been sent yet, in the order the per-tick send walks them */
    int (*queue)[2];
    int nqueue, capqueue;

    /* the S21 a removePlayer sends for a sent-ready chunk: the client drops it */
    void (*client)(void *ctx, int t, int cx, int cz);
    void *cctx;

    /* filterChunkLoadQueue's walk: the (dx, dz) offsets in call order. */
    int (*spiral)[2];
    int nspiral;

    /* the populated set: chunk keys whose pop has run */
    int64_t *popped;
    int npop, cappop;
    /* is_popped's index over them: open addressing, a slot holds a
     * popped[] position plus one (0: empty) */
    int32_t *popidx;
    /* scratch the drain, the capture flush and the queue filter fill and
     * read within one call */
    int *ord_scratch;
    /* the drain's order (ord_scratch), good while the set's gen is ord_gen:
     * only takes happened since, and a take leaves the rest in place */
    int nord, ord_at;
    uint64_t ord_gen;
    int64_t *keys_scratch;
    int (*queue_old)[2];

    void (*emit)(void *ctx, const struct cl_event *e);
    void *ectx;
    struct cl_feed feed;

    /* ChunkProviderServer.loadChunkOnProvideRequest off: the provider never
     * loads a chunk the player manager asks for, it reads an EmptyChunk. The
     * tape replay sets it (MobFree does the same to the oracle's worlds), so
     * nothing the engine asks for is generated into the snapshot's world. */
    int no_load;

    /* WorldProvider.canRespawnHere: the Nether and the End queue every
     * chunk, the overworld keeps the 128-block box around its spawn */
    int no_respawn;

    long counts[5];                     /* load, pop, mark, unmark, unload */

    /* the world's getTotalWorldTime, which the inhabitedTime clock reads;
     * NULL freezes the clock (nothing is added to any chunk) */
    const int64_t *clock;

    cl_load_fn load_cb;
    cl_unload_fn unload_cb;
    void *io_ctx;
};

/* The spiral walk of filterChunkLoadQueue. The loop runs radius*2 rings from
 * the center: east 1, south 1, west 2, north 2, east 3, ... Each ring leg is
 * one direction taken leg-length steps. After the loop, var3 %= 4 and the last
 * radius*2 steps continue in that direction. */
static void build_spiral(struct cl *c)
{
    int r = c->radius;
    c->nspiral = 0;
    c->spiral = malloc(sizeof(int[2]) * (size_t)(r * r * 4 + r * 4 + 8));
    int x = 0, z = 0;
    int d = 0;

    for (int len = 1; len <= r * 2; ++len)
    {
        for (int rep = 0; rep < 2; ++rep)
        {
            for (int i = 0; i < len; ++i)
            {
                x += DIRS[d & 3][0];
                z += DIRS[d & 3][1];
                c->spiral[c->nspiral][0] = x;
                c->spiral[c->nspiral][1] = z;
                ++c->nspiral;
            }

            ++d;
        }
    }

    d &= 3;

    for (int i = 0; i < r * 2; ++i)
    {
        x += DIRS[d][0];
        z += DIRS[d][1];
        c->spiral[c->nspiral][0] = x;
        c->spiral[c->nspiral][1] = z;
        ++c->nspiral;
    }
}

struct cl *cl_init(struct world *w, int spawn_x, int spawn_z)
{
    struct cl *c = calloc(1, sizeof *c);
    c->w = w;
    c->spawn_x = spawn_x;
    c->spawn_z = spawn_z;
    c->radius = 10;         /* IntegratedPlayerList's initial view distance */
    cs_init(&c->unload);
    c->queue = fixed_array(CL_MAX_QUEUE, sizeof *c->queue);
    c->capqueue = CL_MAX_QUEUE;
    c->watch = fixed_array(CL_MAX_WATCH, sizeof *c->watch);
    c->capwatch = CL_MAX_WATCH;
    c->watchidx = fixed_array(CL_WATCH_INDEX, sizeof *c->watchidx);
    c->popped = fixed_array(CL_MAX_POP, sizeof *c->popped);
    c->cappop = CL_MAX_POP;
    c->popidx = fixed_array(CL_POP_INDEX, sizeof *c->popidx);
    c->ord_scratch = fixed_array(CU_MAX_NODES, sizeof *c->ord_scratch);
    c->keys_scratch = fixed_array(CU_MAX_NODES, sizeof *c->keys_scratch);
    c->queue_old = fixed_array(CL_MAX_QUEUE, sizeof *c->queue_old);
    build_spiral(c);
    return c;
}

size_t cl_bytes(const struct cl *c)
{
    if (c == NULL) return 0;
    size_t n = sizeof *c + (size_t)c->capro * sizeof *c->ro + (size_t)c->nspiral * sizeof *c->spiral;
    n += fixed_array_resident(c->queue, CL_MAX_QUEUE, sizeof *c->queue)
       + fixed_array_resident(c->watch, CL_MAX_WATCH, sizeof *c->watch)
       + fixed_array_resident(c->watchidx, CL_WATCH_INDEX, sizeof *c->watchidx)
       + fixed_array_resident(c->popped, CL_MAX_POP, sizeof *c->popped)
       + fixed_array_resident(c->popidx, CL_POP_INDEX, sizeof *c->popidx)
       + fixed_array_resident(c->ord_scratch, CU_MAX_NODES, sizeof *c->ord_scratch)
       + fixed_array_resident(c->keys_scratch, CU_MAX_NODES, sizeof *c->keys_scratch)
       + fixed_array_resident(c->queue_old, CL_MAX_QUEUE, sizeof *c->queue_old);
    n += cs_bytes(&c->unload);
    return n;
}

void cl_free(struct cl *c)
{
    cs_free(&c->unload);
    free(c->ro);
    free(c->spiral);
    fixed_array_free(c->queue, CL_MAX_QUEUE, sizeof *c->queue);
    fixed_array_free(c->watch, CL_MAX_WATCH, sizeof *c->watch);
    fixed_array_free(c->watchidx, CL_WATCH_INDEX, sizeof *c->watchidx);
    fixed_array_free(c->popped, CL_MAX_POP, sizeof *c->popped);
    fixed_array_free(c->popidx, CL_POP_INDEX, sizeof *c->popidx);
    fixed_array_free(c->ord_scratch, CU_MAX_NODES, sizeof *c->ord_scratch);
    fixed_array_free(c->keys_scratch, CU_MAX_NODES, sizeof *c->keys_scratch);
    fixed_array_free(c->queue_old, CL_MAX_QUEUE, sizeof *c->queue_old);
    free(c);
}

void cl_on_event(struct cl *c, void (*emit)(void *ctx, const struct cl_event *e), void *ctx)
{
    c->emit = emit;
    c->ectx = ctx;
}

void cl_on_client_unload(struct cl *c, void (*fn)(void *ctx, int t, int cx, int cz), void *ctx)
{
    c->client = fn;
    c->cctx = ctx;
}

void cl_set_no_load(struct cl *c, int on)
{
    c->no_load = on;
}

/* ChunkProviderServer.chunksToUnload.clear(): the marks already queued are
 * dropped without touching the world. MobFree does the same to the oracle's
 * worlds before the snapshot, so the ring the login's radius change marked
 * (all of it beyond unloadChunksIfNotNearSpawn's 128 blocks) never unloads. */
void cl_clear_unload(struct cl *c)
{
    cs_init(&c->unload);
    c->nro = c->ro_at = 0;
}

/* Snapshot of ChunkProviderServer.chunksToUnload, in its iteration order.
 * bins: the Java table's length (unloadBins), 0 when the snapshot has none.
 * With it the keys go back in their captured order into a table of that
 * size, where each lands in its Java bin at its list place, so the set's own
 * order is Java's from here on, keys the run adds later included. */
void cl_restore_unload(struct cl *c, const int *coords, int ncoords, int bins)
{
    cl_clear_unload(c);
    if (bins > 0)
    {
        cs_init_bins(&c->unload, bins);
        for (int i = 0; i + 1 < ncoords; i += 2) cs_put(&c->unload, chunk_key(coords[i], coords[i + 1]));
        /* a bin's list order is its history's (a TreeBin prepends), which
         * the puts above do not rebuild: the captured order sets it */
        if (ncoords / 2 > c->capro)
        {
            c->capro = ncoords / 2;
            c->ro = realloc(c->ro, (size_t)c->capro * sizeof *c->ro);
        }
        for (int i = 0; i + 1 < ncoords; i += 2) c->ro[i / 2] = chunk_key(coords[i], coords[i + 1]);
        cs_restamp(&c->unload, c->ro, ncoords / 2);
        return;
    }
    /* The capture is Java's table iteration; a fresh re-hash of the keys
     * lands in different bins (the live table grew through its own
     * history and never shrank), so the drain must walk the captured
     * order, not the rebuilt set's. */
    if (ncoords > c->capro)
    {
        c->capro = ncoords;
        c->ro = realloc(c->ro, (size_t)c->capro * sizeof *c->ro);
    }
    for (int i = 0; i + 1 < ncoords; i += 2)
    {
        c->ro[c->nro++] = chunk_key(coords[i], coords[i + 1]);
        cs_put(&c->unload, chunk_key(coords[i], coords[i + 1]));
    }
}

int cl_unload_order(struct cl *c, int *out, int nmax)
{
    int *ord = c->ord_scratch;
    int n = cs_order(&c->unload, ord);
    c->ord_gen = 0;     /* the drain's order was in this scratch */
    int ncopy = n < nmax ? n : nmax;
    for (int k = 0; k < ncopy; ++k)
    {
        int64_t key = c->unload.e[ord[k]].key;
        out[k * 2] = (int)(uint32_t)key;
        out[k * 2 + 1] = (int)(uint32_t)(key >> 32);
    }
    return n;
}

void cl_set_chunk_io(struct cl *c, cl_load_fn load_cb, cl_unload_fn unload_cb, void *ctx)
{
    c->load_cb = load_cb;
    c->unload_cb = unload_cb;
    c->io_ctx = ctx;
}

void cl_set_feed(struct cl *c, const struct cl_feed *feed)
{
    c->feed = *feed;
}

void cl_counts(struct cl *c, long counts[5])
{
    for (int i = 0; i < 5; ++i) counts[i] = c->counts[i];
}

static void emit(struct cl *c, int t, char kind, int cx, int cz, int pop, int id, const char *site)
{
    if (!c->emit) return;

    struct cl_event e;
    e.t = t;
    e.kind = kind;
    e.cx = cx;
    e.cz = cz;
    e.pop = pop;
    e.id = id;
    e.site = site;
    c->emit(c->ectx, &e);
}

static void count(struct cl *c, char kind)
{
    switch (kind)
    {
        case 'l': ++c->counts[0]; break;
        case 'p': ++c->counts[1]; break;
        case 'm': ++c->counts[2]; break;
        case 'n': ++c->counts[3]; break;
        case 'u': ++c->counts[4]; break;
    }
}

/* ChunkProviderServer.loadChunk's body. */
static void load_chunk(struct cl *c, int t, int cx, int cz, int pop, const char *site);

/* --------------------------------------------------------- the pop set */

static uint32_t pop_slot(int64_t key)
{
    uint64_t h = (uint64_t)key * 0x9E3779B97F4A7C15ull;
    return (uint32_t)(h >> 40) & (CL_POP_INDEX - 1);
}

static int is_popped(struct cl *c, int64_t key)
{
    for (uint32_t i = pop_slot(key);; i = (i + 1) & (CL_POP_INDEX - 1))
    {
        int32_t at = c->popidx[i];
        if (at == 0) return 0;
        if (c->popped[at - 1] == key) return 1;
    }
}

static void set_popped(struct cl *c, int64_t key)
{
    if (c->npop == CL_MAX_POP) abort();

    uint32_t i = pop_slot(key);
    while (c->popidx[i] != 0) i = (i + 1) & (CL_POP_INDEX - 1);
    c->popped[c->npop++] = key;
    c->popidx[i] = c->npop;
}

/* Chunk.isTerrainPopulated as the four-neighbor rule reads it: populated by
 * this run, or loaded already populated (a snapshot's chunk, a saved one). */
static int is_populated(struct cl *c, int cx, int cz)
{
    if (is_popped(c, chunk_key(cx, cz))) return 1;

    const struct chunk *ch = world_chunk(c->w, cx, cz);

    return ch != NULL && ch->terrain_populated;
}

/* --------------------------------------------------------- the provider */

/* The restored queue's capture order is java's live table's iteration, not
 * what a rebuild of the keys would land in. A mark while part of the capture
 * is still queued ends the sidecar: the rest of the capture is re-put in its
 * own order (a plain bin appends, so each bin keeps the capture's order) and
 * the normal put takes the new key. */
static void cs_flush_capture(struct cl *c)
{
    if (c->ro_at >= c->nro) return;

    int n = c->nro - c->ro_at;
    int64_t *keys = c->keys_scratch;

    if (n > CU_MAX_NODES) abort();

    /* a key a load already unmarked is no longer queued */
    for (int i = 0; i < n; ++i)
        keys[i] = cs_has(&c->unload, c->ro[c->ro_at + i]) ? c->ro[c->ro_at + i] : INT64_MIN;

    cs_init(&c->unload);

    for (int i = 0; i < n; ++i)
        if (keys[i] != INT64_MIN) cs_put(&c->unload, keys[i]);

    c->ro_at = c->nro = 0;
}

/* ChunkProviderServer.unloadChunksIfNotNearSpawn: the chunk enters the unload
 * set unless its center sits inside the 128-block box around the spawn. */
static void mark(struct cl *c, int t, int cx, int cz, int pop)
{
    int dx = cx * 16 + 8 - c->spawn_x;
    int dz = cz * 16 + 8 - c->spawn_z;

    if (!c->no_respawn && dx >= -128 && dx <= 128 && dz >= -128 && dz <= 128) return;

    cs_flush_capture(c);
    cs_put(&c->unload, chunk_key(cx, cz));
    emit(c, t, 'm', cx, cz, pop, 0, 0);
    count(c, 'm');
}

/* ChunkProviderServer.populate, on entry: the event, then the populate body's
 * own chunk loads from the recording. */
static void populate(struct cl *c, int t, int cx, int cz, int pop_id)
{
    emit(c, t, 'p', cx, cz, -1, pop_id, 0);
    count(c, 'p');
    set_popped(c, chunk_key(cx, cz));

    /* the populate body's own chunk loads, from the feed: a feature wrote
     * into an unloaded chunk and the provider loaded it mid-populate */
    if (c->feed.pop_loads)
    {
        int n = 0;
        const struct cl_load *ls = c->feed.pop_loads(c->ectx, pop_id, &n);

        for (int i = 0; i < n; ++i) load_chunk(c, t, ls[i].cx, ls[i].cz, pop_id, ls[i].site);
    }
}

/* Chunk.populateChunk's four-neighbor rule: the chunk itself, then west,
 * north and northwest when their neighbor triples are loaded and the target is
 * still unpopped. */
static void populate_chunk(struct cl *c, int t, int cx, int cz)
{
    if (!is_populated(c, cx, cz)
        && world_chunk_loaded(c->w, cx + 1, cz + 1)
        && world_chunk_loaded(c->w, cx, cz + 1)
        && world_chunk_loaded(c->w, cx + 1, cz))
        populate(c, t, cx, cz, c->counts[1]);

    /* Each test: the west/north/northwest chunk loaded, its plus-one
     * companions loaded ((-1,0) checks (-1,+1) and (0,0)... exactly as the
     * decompiled rule spells them), the target unpopped. */
    if (world_chunk_loaded(c->w, cx - 1, cz) && !is_populated(c, cx - 1, cz)
        && world_chunk_loaded(c->w, cx - 1, cz + 1) && world_chunk_loaded(c->w, cx, cz + 1))
        populate(c, t, cx - 1, cz, c->counts[1]);

    if (world_chunk_loaded(c->w, cx, cz - 1) && !is_populated(c, cx, cz - 1)
        && world_chunk_loaded(c->w, cx + 1, cz - 1) && world_chunk_loaded(c->w, cx + 1, cz))
        populate(c, t, cx, cz - 1, c->counts[1]);

    if (world_chunk_loaded(c->w, cx - 1, cz - 1) && !is_populated(c, cx - 1, cz - 1)
        && world_chunk_loaded(c->w, cx, cz - 1) && world_chunk_loaded(c->w, cx - 1, cz))
        populate(c, t, cx - 1, cz - 1, c->counts[1]);
}

/* ChunkProviderServer.loadChunk's body: the unload-set removal, the provide,
 * the load event, then the populate rule when the call is top-level. */
static void load_chunk(struct cl *c, int t, int cx, int cz, int pop, const char *site)
{
    if (cs_take(&c->unload, chunk_key(cx, cz)))
    {
        emit(c, t, 'n', cx, cz, pop, 0, 0);
        count(c, 'n');
    }

    if (world_chunk_loaded(c->w, cx, cz)) return;
    if (c->no_load) return;

    /* the profiler's chunk provision: the load and its populate rule */
    if (PHASE_PROF_ON()) phase_sub_begin_(PS_LOAD);
    if (c->load_cb) c->load_cb(c->io_ctx, cx, cz); else world_load_chunk(c->w, cx, cz);
    emit(c, t, 'l', cx, cz, pop, 0, site);
    count(c, 'l');

    if (pop == -1) populate_chunk(c, t, cx, cz);
    if (PHASE_PROF_ON()) phase_sub_end_(PS_LOAD);
}

/* --------------------------------------------------------- the sites */

void cl_spawn_area(struct cl *c, int t)
{
    for (int x = -192; x <= 192; x += 16)
        for (int z = -192; z <= 192; z += 16)
            load_chunk(c, t, (c->spawn_x + x) >> 4, (c->spawn_z + z) >> 4, -1, "spawn");
}

void cl_replay_load(struct cl *c, int t, int cx, int cz, const char *site)
{
    load_chunk(c, t, cx, cz, -1, site);
}

void cl_pop_load(struct cl *c, int t, int cx, int cz, int pop_id, const char *site)
{
    load_chunk(c, t, cx, cz, pop_id, site);
}

void cl_unload_step(struct cl *c, int t)
{
    /* The loop body is iterator().next() then a remove, 100 times: the set's
     * first key in bin order each time. A queue restored from a snapshot
     * drains in its captured order (see cl_restore_unload): the live
     * table's bins aren't reproducible, and a key a load already removed
     * never comes up. */
    for (int i = 0; i < 100 && c->unload.size > 0; ++i)
    {
        int64_t key;

        while (c->ro_at < c->nro && !cs_has(&c->unload, c->ro[c->ro_at])) ++c->ro_at;

        if (c->ro_at < c->nro)
        {
            key = c->ro[c->ro_at++];
        }
        else
        {
            /* the set's first key in its order: the order computed once,
             * then walked past the keys taken since, until a key is added */
            if (c->ord_gen != c->unload.gen)
            {
                c->nord = cs_order(&c->unload, c->ord_scratch);
                c->ord_at = 0;
                c->ord_gen = c->unload.gen;
            }

            while (c->ord_at < c->nord && !c->unload.e[c->ord_scratch[c->ord_at]].live) ++c->ord_at;

            if (c->ord_at == c->nord) abort();

            key = c->unload.e[c->ord_scratch[c->ord_at]].key;
        }

        int cx = (int)(uint32_t)key, cz = (int)(uint32_t)(key >> 32);

        if (c->unload_cb ? c->unload_cb(c->io_ctx, cx, cz) : world_unload_chunk(c->w, cx, cz))
        {
            emit(c, t, 'u', cx, cz, -1, 0, 0);
            count(c, 'u');
        }

        cs_take(&c->unload, key);
    }
}

/* --------------------------------------------------------- the send queue */

static int queue_index(const struct cl *c, int cx, int cz)
{
    for (int i = 0; i < c->nqueue; ++i)
        if (c->queue[i][0] == cx && c->queue[i][1] == cz) return i;

    return -1;
}

static void queue_add(struct cl *c, int cx, int cz)
{
    if (c->nqueue == CL_MAX_QUEUE) abort();

    c->queue[c->nqueue][0] = cx;
    c->queue[c->nqueue][1] = cz;
    ++c->nqueue;
}

/* LinkedList.remove(Object): the first equal entry. */
static void queue_remove_at(struct cl *c, int i)
{
    memmove(&c->queue[i], &c->queue[i + 1], sizeof *c->queue * (size_t)(c->nqueue - i - 1));
    --c->nqueue;
}

void cl_set_send_queue(struct cl *c, const int *coords, int n)
{
    c->nqueue = 0;
    for (int i = 0; i < n; ++i) queue_add(c, coords[i * 2], coords[i * 2 + 1]);
}

/* Chunk.func_150802_k: ticked by the server once (field_150815_m), terrain
 * populated and light populated. */
static int chunk_ready(struct cl *c, int cx, int cz)
{
    const struct chunk *ch = world_chunk(c->w, cx, cz);
    return ch != NULL && ch->populated && ch->terrain_populated && ch->light_populated;
}

/* PlayerManager.filterChunkLoadQueue: the queue again in the spiral order
 * around the player's chunk (the center, then filterChunkLoadQueue's legs at
 * the current view radius), keeping only what it held. */
static void filter_queue(struct cl *c, double x, double z)
{
    int n = c->nqueue;
    int (*old)[2] = c->queue_old;
    memcpy(old, c->queue, sizeof *old * (size_t)n);
    c->nqueue = 0;

    int cx = (int)x >> 4, cz = (int)z >> 4;
    int r = c->radius, d = 0, ox = 0, oz = 0;

#define KEEP(X, Z) do { for (int k_ = 0; k_ < n; ++k_) \
        if (old[k_][0] == (X) && old[k_][1] == (Z)) { queue_add(c, (X), (Z)); break; } } while (0)

    KEEP(cx, cz);

    for (int len = 1; len <= r * 2; ++len)
        for (int rep = 0; rep < 2; ++rep)
        {
            const int *dir = DIRS[d++ % 4];

            for (int i = 0; i < len; ++i)
            {
                ox += dir[0];
                oz += dir[1];
                KEEP(cx + ox, cz + oz);
            }
        }

    d %= 4;

    for (int i = 0; i < r * 2; ++i)
    {
        ox += DIRS[d][0];
        oz += DIRS[d][1];
        KEEP(cx + ox, cz + oz);
    }
#undef KEEP

}

int cl_send_chunks(struct cl *c, int t, int (*out)[2], int max)
{
    (void)t;
    int n = 0;

    /* EntityPlayerMP.onUpdate: walk the queue until a bulk packet is full;
     * a chunk that exists and is ready leaves the queue into the packet */
    for (int i = 0; i < c->nqueue && n < max; )
    {
        int cx = c->queue[i][0], cz = c->queue[i][1];

        if (world_chunk_loaded(c->w, cx, cz) && chunk_ready(c, cx, cz))
        {
            out[n][0] = cx;
            out[n][1] = cz;
            ++n;
            queue_remove_at(c, i);
            continue;
        }

        ++i;
    }

    return n;
}

/* --------------------------------------------------------- the player manager */

/* The world's total time, as the instances read it. */
static int64_t cl_now(const struct cl *c)
{
    return c->clock ? *c->clock : 0;
}

/* PlayerInstance.increaseInhabitedTime: the ticks since the instance's mark
 * go onto its chunk (getChunkFromChunkCoords: a chunk the provider does not
 * hold is the EmptyChunk, which drops them), and the mark moves to now. */
static void increase_inhabited(struct cl *c, struct watcher *w)
{
    if (c->clock == NULL) return;

    int64_t now = cl_now(c);
    struct chunk *ch = world_chunk(c->w, w->cx, w->cz);

    if (ch != NULL) ch->inhabited_time += now - w->prev;
    w->prev = now;
}

/* PlayerInstance removal (PlayerInstance.removePlayer's tail): the chunk's
 * inhabitedTime, then the instance leaves the map and the chunk is queued for
 * unload. */
static void watcher_dead(struct cl *c, int t, struct watcher *w)
{
    increase_inhabited(c, w);
    w->inst = 0;
    mark(c, t, w->cx, w->cz, -1);
}

static uint32_t watch_slot(int cx, int cz)
{
    return (uint32_t)(((uint64_t)chunk_key(cx, cz) * 0x9E3779B97F4A7C15ull) >> 40) & (CL_WATCH_INDEX - 1);
}

/* the index slot of (cx, cz)'s newest watcher, or the empty slot that ends
 * its probe */
static uint32_t watch_probe(const struct cl *c, int cx, int cz)
{
    uint32_t i = watch_slot(cx, cz);

    while (c->watchidx[i] != 0 && (c->watch[c->watchidx[i] - 1].cx != cx || c->watch[c->watchidx[i] - 1].cz != cz))
        i = (i + 1) & (CL_WATCH_INDEX - 1);
    return i;
}

/* the instance at (cx, cz): a chunk has at most one (watch_make makes one
 * only when there is none), its newest watcher */
static struct watcher *watch_of(struct cl *c, int cx, int cz)
{
    int32_t at = c->watchidx[watch_probe(c, cx, cz)];

    return at != 0 && c->watch[at - 1].inst ? &c->watch[at - 1] : 0;
}

int cl_player_watching(struct cl *c, int cx, int cz)
{
    const struct watcher *w = watch_of(c, cx, cz);

    return w != NULL && w->refs > 0 && queue_index(c, cx, cz) < 0;
}

static struct watcher *watch_make(struct cl *c, int t, int cx, int cz)
{
    struct watcher *w = watch_of(c, cx, cz);

    if (w) return w;

    if (c->nwatch == CL_MAX_WATCH) abort();

    c->watchidx[watch_probe(c, cx, cz)] = c->nwatch + 1;
    w = &c->watch[c->nwatch++];
    w->cx = cx;
    w->cz = cz;
    w->refs = 0;
    w->inst = 1;
    w->prev = 0;    /* previousWorldTime until a player's add sets it */
    load_chunk(c, t, cx, cz, -1, "playermgr");
    return w;
}

/* PlayerInstance.addPlayer: one player, so a second add is the debug log;
 * the first queues the chunk for the client. */
static void watch_add(struct cl *c, int t, struct watcher *w)
{
    (void)t;

    if (w->refs > 0) return;

    /* the first player marks the inhabitedTime clock */
    w->prev = cl_now(c);
    ++w->refs;
    queue_add(c, w->cx, w->cz);
}

/* PlayerInstance.removePlayer: the S21 that drops a ready chunk from the
 * client, the queue entry, then the instance when nobody is left: its
 * increaseInhabitedTime, then the map removal and the unload mark. */
static void watch_drop(struct cl *c, int t, int cx, int cz)
{
    struct watcher *w = watch_of(c, cx, cz);

    if (!w || w->refs <= 0) return;

    if (chunk_ready(c, cx, cz) && c->client) c->client(c->cctx, t, cx, cz);

    int i = queue_index(c, cx, cz);
    if (i >= 0) queue_remove_at(c, i);

    if (--w->refs > 0) return;

    watcher_dead(c, t, w);
}

/* Two squares of radius r around (x1, z1) and (x2, z2) overlap. */
static int overlaps(int x1, int z1, int x2, int z2, int r)
{
    int dx = x1 - x2, dz = z1 - z2;
    return dx >= -r && dx <= r && dz >= -r && dz <= r;
}

/* PlayerManager.addPlayer: the radius square's instances, then
 * filterChunkLoadQueue's ordering walk (no engine effect; the queue only feeds
 * the client's packet order). */
void cl_login(struct cl *c, int t, double x, double z)
{
    int cx = (int)x >> 4, cz = (int)z >> 4;
    c->px = x;
    c->pz = z;
    c->logged = 1;

    for (int ix = cx - c->radius; ix <= cx + c->radius; ++ix)
        for (int iz = cz - c->radius; iz <= cz + c->radius; ++iz)
            watch_add(c, t, watch_make(c, t, ix, iz));

    filter_queue(c, x, z);
}

/* PlayerManager.updateMountedMovingPlayer: the moved square's new instances
 * (the ring that does not overlap the old square), the dropped instances (the
 * ring of the old square that does not overlap the new one), from the C03
 * packet position. */
void cl_c03(struct cl *c, int t, double x, double z)
{
    if (!c->logged) return;

    double dx = c->px - x;
    double dz = c->pz - z;

    if (dx * dx + dz * dz < 64.0) return;

    int cx = (int)x >> 4, cz = (int)z >> 4;
    int ox = (int)c->px >> 4, oz = (int)c->pz >> 4;
    int dcx = cx - ox, dcz = cz - oz;

    if (dcx != 0 || dcz != 0)
    {
        for (int ix = cx - c->radius; ix <= cx + c->radius; ++ix)
        {
            for (int iz = cz - c->radius; iz <= cz + c->radius; ++iz)
            {
                if (!overlaps(ix, iz, ox, oz, c->radius))
                    watch_add(c, t, watch_make(c, t, ix, iz));

                if (!overlaps(ix - dcx, iz - dcz, cx, cz, c->radius))
                    watch_drop(c, t, ix - dcx, iz - dcz);
            }
        }

        filter_queue(c, x, z);
        c->px = x;
        c->pz = z;
    }
}

/* ServerConfigurationManager.func_152611_a: every held instance of a player
 * outside the new radius is dropped, the new ring is added. One player, so the
 * walk is the two squares' difference, exactly like updateMountedMovingPlayer
 * with dcx = dcz = 0 in its radius. PlayerManager.func_152622_a centers both
 * walks on the player's own position (x, z), not the managed one, which it
 * leaves. */
void cl_view_radius_at(struct cl *c, int t, int radius, double x, double z)
{
    if (radius < 3) radius = 3;
    if (radius > 20) radius = 20;
    if (radius == c->radius) return;

    int cx = (int)x >> 4, cz = (int)z >> 4;

    if (radius > c->radius)
    {
        for (int ix = cx - radius; ix <= cx + radius; ++ix)
            for (int iz = cz - radius; iz <= cz + radius; ++iz)
                watch_add(c, t, watch_make(c, t, ix, iz));
    }
    else
    {
        /* getOrCreateChunkWatcher(x, z, true).removePlayer: a chunk the
         * player does not watch (the square is centered on the position,
         * not the managed one the watched square follows) gets an instance
         * anyway, its chunk loaded (and so off the unload queue), left in the
         * map with no player: saveAllChunks never queues it */
        for (int ix = cx - c->radius; ix <= cx + c->radius; ++ix)
            for (int iz = cz - c->radius; iz <= cz + c->radius; ++iz)
                if (!overlaps(ix, iz, cx, cz, radius))
                {
                    watch_make(c, t, ix, iz);
                    watch_drop(c, t, ix, iz);
                }
    }

    c->radius = radius;
}

/* The same at the login's position (the join's tick-1 tail). */
void cl_view_radius(struct cl *c, int t, int radius)
{
    cl_view_radius_at(c, t, radius, c->px, c->pz);
}

/* WorldServer.saveAllChunks at tick % 900 == 0: every loaded chunk without a
 * player instance, in the provider's load order. */
void cl_save_marks(struct cl *c, int t)
{
    for (size_t i = 0; i < c->w->lon; ++i)
    {
        int64_t key = c->w->load_order[i];
        int cx = (int)(uint32_t)key, cz = (int)(uint32_t)(key >> 32);

        if (!watch_of(c, cx, cz)) mark(c, t, cx, cz, -1);
    }
}

/* WorldProvider.canRespawnHere: off for the Nether and the End, whose
 * unloadChunksIfNotNearSpawn queues every chunk. */
void cl_set_respawn(struct cl *c, int on)
{
    c->no_respawn = !on;
}

/* PlayerManager.func_152622_a on a manager with no players: the radius. */
void cl_set_radius(struct cl *c, int radius)
{
    if (radius < 3) radius = 3;
    if (radius > 20) radius = 20;
    c->radius = radius;
}

/* PlayerManager.removePlayer: every instance of the managed square drops the
 * player, and an instance left without one leaves the map and queues its
 * chunk (PlayerInstance.removePlayer). */
void cl_logout(struct cl *c, int t)
{
    if (!c->logged) return;

    int cx = (int)c->px >> 4, cz = (int)c->pz >> 4;

    for (int ix = cx - c->radius; ix <= cx + c->radius; ++ix)
        for (int iz = cz - c->radius; iz <= cz + c->radius; ++iz)
            watch_drop(c, t, ix, iz);

    c->logged = 0;
}

/* PlayerManager.filterChunkLoadQueue after addPlayer: the player's chunk,
 * then the spiral, as cx, cz pairs into out (room for (2r+1)^2 pairs). */
int cl_send_order(struct cl *c, double x, double z, int *out)
{
    int cx = (int)x >> 4, cz = (int)z >> 4;
    int n = 0;

    out[n * 2] = cx;
    out[n * 2 + 1] = cz;
    ++n;

    /* the spiral was built for the initial radius 10; its first
     * (2r+1)^2 - 1 steps are the smaller radius' whole walk, and the rest lie
     * outside the square, which the filter skips */
    int side = 2 * c->radius + 1;

    for (int i = 0; i < c->nspiral && n < side * side; ++i)
    {
        out[n * 2] = cx + c->spiral[i][0];
        out[n * 2 + 1] = cz + c->spiral[i][1];
        ++n;
    }

    return n;
}

int cl_radius(const struct cl *c)
{
    return c->radius;
}

/* The inhabitedTime clock: the world's total time (NULL freezes it). */
void cl_set_clock(struct cl *c, const int64_t *total_time)
{
    c->clock = total_time;
}

/* PlayerManager.updatePlayerInstances' 8,000-tick branch: once the world's
 * total time is more than 8,000 past the manager's previousTotalWorldTime
 * (*prev_total, kept by the caller per world), the mark moves and every
 * instance in playerInstanceList runs processChunk (increaseInhabitedTime).
 * Each instance touches only its own chunk, so the list order does not
 * matter. */
void cl_sweep(struct cl *c, int64_t *prev_total, int64_t now)
{
    if (now - *prev_total <= 8000) return;

    *prev_total = now;

    for (int i = 0; c != NULL && i < c->nwatch; ++i)
        if (c->watch[i].inst) increase_inhabited(c, &c->watch[i]);
}

/* A snapshot's instance marks: the instance at (cx, cz) was last marked
 * age ticks before now. 0 when there is no such instance. */
int cl_set_mark(struct cl *c, int cx, int cz, int age)
{
    struct watcher *w = watch_of(c, cx, cz);

    if (w == NULL) return 0;

    w->prev = cl_now(c) - age;
    return 1;
}

/* The phase digest's view of the provider and player manager (phase.h):
 * the unload set as keys and their list stamps (the order is computed from
 * them and the bins), a restored queue, the instances, the send queue, the
 * populated set and the counters. */
static uint64_t cl_mix(uint64_t h, uint64_t v)
{
    h ^= v;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return h;
}

uint64_t cl_digest(const struct cl *c)
{
    const struct chunkset *s = &c->unload;
    uint64_t h = 0x636c, acc = 0;
    int trees = 0;

    for (int i = 0; i < s->ne; ++i)
        if (s->e[i].live) acc += cl_mix(cl_mix(0x75, (uint64_t)s->e[i].key), (uint64_t)s->e[i].stamp);
    for (int b = 0; b < s->n; ++b) trees += s->root[b] >= 0;
    h = cl_mix(h, (uint32_t)s->size | (uint64_t)(uint32_t)s->n << 32);
    h = cl_mix(cl_mix(h, acc), (uint32_t)trees);

    h = cl_mix(h, (uint32_t)c->nro | (uint64_t)(uint32_t)c->ro_at << 32);
    for (int k = 0; k < c->nro; ++k) h = cl_mix(h, (uint64_t)c->ro[k]);

    uint64_t px, pz;
    memcpy(&px, &c->px, 8);
    memcpy(&pz, &c->pz, 8);
    h = cl_mix(cl_mix(h, px), pz);
    h = cl_mix(h, (uint32_t)c->radius | (uint64_t)(uint32_t)c->logged << 32);

    h = cl_mix(h, (uint32_t)c->nwatch);
    for (int k = 0; k < c->nwatch; ++k)
    {
        const struct watcher *w = &c->watch[k];
        h = cl_mix(h, (uint32_t)w->cx | (uint64_t)(uint32_t)w->cz << 32);
        h = cl_mix(h, (uint32_t)w->refs | (uint64_t)(uint32_t)w->inst << 32);
        h = cl_mix(h, (uint64_t)w->prev);
    }

    h = cl_mix(h, (uint32_t)c->nqueue);
    for (int k = 0; k < c->nqueue; ++k) h = cl_mix(h, (uint32_t)c->queue[k][0] | (uint64_t)(uint32_t)c->queue[k][1] << 32);
    h = cl_mix(h, (uint32_t)c->npop);
    for (int k = 0; k < c->npop; ++k) h = cl_mix(h, (uint64_t)c->popped[k]);
    for (int k = 0; k < 5; ++k) h = cl_mix(h, (uint64_t)c->counts[k]);
    return h;
}
