/* The CUDA chunk generators (worldgen for the overworld, worldgen_dim for the Nether
 * and the End) as genahead's batch generator (engine/genahead.h): a batch's
 * requests split by dimension, each part one device batch, the results
 * scattered back in request order. Linked only into the CUDA build
 * (out/native/cuda/worldgen/test_snapshots). */
#define _GNU_SOURCE
#include "worldgen.h"
#include "../../engine/genahead.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define GA_CUDA_BATCH 256
#define GA_CUDA_SEEDS 8

struct ga_cuda {
    struct worldgen *og;
    struct worldgen_dim *dg;
    int device, max_batch, max_seeds, nseeds;
    int low;                    /* the stream at the device's lowest priority */
    int64_t *seeds;
    struct worldgen_req *oreq;
    struct worldgen_dim_req *dreq;
    int *oat, *dat;             /* each part's request indices */
    uint8_t *ids, *metas, *biome, *gen100, *tops;
    uint64_t *rands;
    struct worldgen_built *built;   /* run_built's, a mixed batch's overworld part */
    uint8_t *bands;
};

/* worldgen_fetch_built's records are genahead's struct ga_built */
_Static_assert(sizeof(struct worldgen_built) == sizeof(struct ga_built) &&
               offsetof(struct worldgen_built, ticking) == offsetof(struct ga_built, ticking) &&
               offsetof(struct worldgen_built, height_min) == offsetof(struct ga_built, height_min) &&
               offsetof(struct worldgen_built, height) == offsetof(struct ga_built, height) &&
               WORLDGEN_BAND_BYTES == GA_BAND_BYTES, "the built stage's record is genahead's");

static int seed_find(const struct ga_cuda *c, int64_t seed)
{
    for (int i = 0; i < c->nseeds; ++i)
        if (c->seeds[i] == seed) return i;
    return -1;
}

/* the batch's seeds the generators lack, built on the device in one call */
static int seeds_add(struct ga_cuda *c, const struct ga_req *req, int n)
{
    int had = c->nseeds;

    for (int k = 0; k < n; ++k)
        if (seed_find(c, req[k].seed) < 0)
        {
            if (c->nseeds == c->max_seeds) return -1;
            c->seeds[c->nseeds++] = req[k].seed;
        }
    if (c->nseeds > had && (worldgen_set_seeds(c->og, c->seeds, c->nseeds) || worldgen_dim_set_seeds(c->dg, c->seeds, c->nseeds)))
    {
        c->nseeds = had;
        return -1;
    }
    return 0;
}

static int ga_cuda_run(void *ctx, const struct ga_req *req, int n, uint8_t *ids, uint8_t *metas, uint8_t *biome,
                       uint8_t *tops, uint64_t *rand)
{
    struct ga_cuda *c = ctx;
    int no = 0, nd = 0;

    if (n > c->max_batch || worldgen_device_use(c->device) || seeds_add(c, req, n)) return -1;
    for (int k = 0; k < n; ++k)
    {
        int s = seed_find(c, req[k].seed);

        if (req[k].dim == 0)
        {
            c->oreq[no] = (struct worldgen_req){s, req[k].cx, req[k].cz};
            c->oat[no++] = k;
        }
        else
        {
            c->dreq[nd] = (struct worldgen_dim_req){s, req[k].dim, req[k].cx, req[k].cz};
            c->dat[nd++] = k;
        }
    }
    if (no > 0 && nd == 0)
    {
        /* all overworld: the results straight into the caller's slots */
        if (worldgen_submit(c->og, c->oreq, no) || worldgen_generate(c->og) || worldgen_fetch_ids(c->og, ids) ||
            worldgen_fetch_metas(c->og, metas) || worldgen_fetch_biomes(c->og, c->gen100, biome) ||
            worldgen_fetch_tops(c->og, tops, rand))
            return -1;
        if (worldgen_errors(c->og))
        {
            fprintf(stderr, "gen-ahead cuda: device error bits %x\n", worldgen_errors(c->og));
            return -1;
        }
        return 0;
    }
    if (no > 0)
    {
        if (worldgen_submit(c->og, c->oreq, no) || worldgen_generate(c->og) || worldgen_fetch_ids(c->og, c->ids) ||
            worldgen_fetch_metas(c->og, c->metas) || worldgen_fetch_biomes(c->og, c->gen100, c->biome) ||
            worldgen_fetch_tops(c->og, c->tops, c->rands))
            return -1;
        if (worldgen_errors(c->og))
        {
            fprintf(stderr, "gen-ahead cuda: device error bits %x\n", worldgen_errors(c->og));
            return -1;
        }
        for (int i = 0; i < no; ++i)
        {
            size_t k = (size_t)c->oat[i];

            memcpy(ids + k * GA_CELLS, c->ids + (size_t)i * GA_CELLS, GA_CELLS);
            memcpy(metas + k * GA_CELLS, c->metas + (size_t)i * GA_CELLS, GA_CELLS);
            memcpy(biome + k * 256, c->biome + (size_t)i * 256, 256);
            memcpy(tops + k * 256, c->tops + (size_t)i * 256, 256);
            rand[k] = c->rands[i];
        }
    }
    if (nd > 0)
    {
        if (worldgen_dim_submit(c->dg, c->dreq, nd) || worldgen_dim_generate(c->dg) || worldgen_dim_fetch_raw(c->dg, c->ids) ||
            worldgen_dim_fetch_rand(c->dg, c->rands))
            return -1;
        if (worldgen_dim_errors(c->dg))
        {
            fprintf(stderr, "gen-ahead cuda: device error bits %x\n", worldgen_dim_errors(c->dg));
            return -1;
        }
        for (int i = 0; i < nd; ++i)
        {
            size_t k = (size_t)c->dat[i];

            memcpy(ids + k * GA_CELLS, c->ids + (size_t)i * WORLDGEN_DIM_RAW, WORLDGEN_DIM_RAW);
            rand[k] = c->rands[i];
        }
    }
    return 0;
}

/* run_built: the overworld part built on the device (worldgen_fetch_built),
 * the Nether's and the End's raw as run's */
static int ga_cuda_run_built(void *ctx, const struct ga_req *req, int n, struct ga_built *built, uint8_t *bands,
                             int *top_band, uint8_t *ids, uint8_t *biome, uint8_t *tops, uint64_t *rand)
{
    struct ga_cuda *c = ctx;
    int no = 0, nd = 0, top = -1;
    const size_t chunk_bands = (size_t)16 * GA_BAND_BYTES;

    if (n > c->max_batch || worldgen_device_use(c->device) || seeds_add(c, req, n)) return -1;
    for (int k = 0; k < n; ++k)
    {
        int s = seed_find(c, req[k].seed);

        if (req[k].dim == 0)
        {
            c->oreq[no] = (struct worldgen_req){s, req[k].cx, req[k].cz};
            c->oat[no++] = k;
        }
        else
        {
            c->dreq[nd] = (struct worldgen_dim_req){s, req[k].dim, req[k].cx, req[k].cz};
            c->dat[nd++] = k;
        }
    }
    if (no > 0)
    {
        /* all overworld: straight into the caller's slots */
        int direct = nd == 0;
        struct worldgen_built *hb = direct ? (struct worldgen_built *)(void *)built : c->built;
        uint8_t *bb = direct ? bands : c->bands, *bio = direct ? biome : c->biome, *tp = direct ? tops : c->tops;
        uint64_t *rd = direct ? rand : c->rands;

        if (!direct && c->built == NULL)
        {
            c->built = malloc(sizeof *c->built * (size_t)c->max_batch);
            c->bands = malloc(chunk_bands * (size_t)c->max_batch);
            if (c->built == NULL || c->bands == NULL) return -1;
        }
        if (worldgen_submit(c->og, c->oreq, no) || worldgen_generate(c->og) || worldgen_fetch_built(c->og, hb, bb, &top) ||
            worldgen_fetch_biomes(c->og, c->gen100, bio) || worldgen_fetch_tops(c->og, tp, rd))
            return -1;
        if (worldgen_errors(c->og))
        {
            fprintf(stderr, "gen-ahead cuda: device error bits %x\n", worldgen_errors(c->og));
            return -1;
        }
        for (int i = 0; !direct && i < no; ++i)
        {
            size_t k = (size_t)c->oat[i];

            memcpy(&built[k], &c->built[i], sizeof built[k]);
            if (top >= 0) memcpy(bands + k * chunk_bands, c->bands + (size_t)i * chunk_bands, (size_t)(top + 1) * GA_BAND_BYTES);
            memcpy(biome + k * 256, c->biome + (size_t)i * 256, 256);
            memcpy(tops + k * 256, c->tops + (size_t)i * 256, 256);
            rand[k] = c->rands[i];
        }
    }
    *top_band = top;
    if (nd > 0)
    {
        if (worldgen_dim_submit(c->dg, c->dreq, nd) || worldgen_dim_generate(c->dg) || worldgen_dim_fetch_raw(c->dg, c->ids) ||
            worldgen_dim_fetch_rand(c->dg, c->rands))
            return -1;
        if (worldgen_dim_errors(c->dg))
        {
            fprintf(stderr, "gen-ahead cuda: device error bits %x\n", worldgen_dim_errors(c->dg));
            return -1;
        }
        for (int i = 0; i < nd; ++i)
        {
            size_t k = (size_t)c->dat[i];

            memcpy(ids + k * GA_CELLS, c->ids + (size_t)i * WORLDGEN_DIM_RAW, WORLDGEN_DIM_RAW);
            rand[k] = c->rands[i];
        }
    }
    return 0;
}

static void ga_cuda_free(void *ctx);

static void *ga_cuda_host_alloc(void *ctx, size_t bytes)
{
    (void)ctx;
    return worldgen_host_alloc(bytes);
}

static void ga_cuda_host_free(void *ctx, void *p)
{
    (void)ctx;
    worldgen_host_free(p);
}

/* the generators and their host buffers for batches of max_batch over up to
 * max_seeds seeds, on stream (NULL the legacy default stream) */
static struct ga_cuda *ga_cuda_new(int max_batch, int max_seeds, void *stream)
{
    struct ga_cuda *c = calloc(1, sizeof *c);
    size_t B = (size_t)max_batch;

    c->device = worldgen_device_current();
    c->max_batch = max_batch;
    c->max_seeds = max_seeds;
    c->og = worldgen_create(max_batch, max_seeds, WORLDGEN_NEG_NONE);
    c->dg = c->og != NULL ? worldgen_dim_create(max_batch, max_seeds, WORLDGEN_DIM_NEG_NONE) : NULL;
    if (c->dg == NULL)
    {
        ga_cuda_free(c);
        return NULL;
    }
    worldgen_set_stream(c->og, stream);
    worldgen_dim_set_stream(c->dg, stream);
    c->seeds = malloc(sizeof *c->seeds * (size_t)max_seeds);
    c->oreq = malloc(sizeof *c->oreq * B);
    c->dreq = malloc(sizeof *c->dreq * B);
    c->oat = malloc(sizeof *c->oat * B);
    c->dat = malloc(sizeof *c->dat * B);
    c->ids = malloc((size_t)GA_CELLS * B);
    c->metas = malloc((size_t)GA_CELLS * B);
    c->biome = malloc(256 * B);
    c->gen100 = malloc(100 * B);
    c->tops = malloc(256 * B);
    c->rands = malloc(sizeof *c->rands * B);
    return c;
}

static void ga_cuda_free(void *ctx)
{
    struct ga_cuda *c = ctx;
    int previous = worldgen_device_current();

    worldgen_device_use(c->device);
    worldgen_free(c->og);
    worldgen_dim_free(c->dg);
    worldgen_device_use(previous);
    free(c->seeds);
    free(c->oreq);
    free(c->dreq);
    free(c->oat);
    free(c->dat);
    free(c->rands);
    free(c->ids);
    free(c->metas);
    free(c->biome);
    free(c->gen100);
    free(c->tops);
    free(c->built);
    free(c->bands);
    free(c);
}

/* ------------------------------------------------------------ the server
 *
 * The generators run in a child process forked before the device is touched:
 * the replay's process then holds no driver threads or driver mappings (the
 * grave's conservative scan, grave.c, reads every private anonymous mapping
 * of its own process, and a driver thread's can fault). Requests and results
 * pass through one shared mapping, the turns through a socket pair. */

struct ga_shm {
    int n, rc;
    struct ga_req req[GA_CUDA_BATCH];
    uint64_t rand[GA_CUDA_BATCH];
    uint8_t biome[256 * GA_CUDA_BATCH], tops[256 * GA_CUDA_BATCH];
    uint8_t ids[(size_t)GA_CELLS * GA_CUDA_BATCH], metas[(size_t)GA_CELLS * GA_CUDA_BATCH];
};

struct ga_client {
    struct ga_shm *shm;
    int fd;
    pid_t pid;
};

static int full_read(int fd, void *b, size_t n)
{
    for (size_t k = 0; k < n;)
    {
        ssize_t r = read(fd, (char *)b + k, n - k);

        if (r <= 0)
        {
            if (r < 0 && errno == EINTR) continue;
            return -1;
        }
        k += (size_t)r;
    }
    return 0;
}

static int full_write(int fd, const void *b, size_t n)
{
    for (size_t k = 0; k < n;)
    {
        ssize_t r = write(fd, (const char *)b + k, n - k);

        if (r <= 0)
        {
            if (r < 0 && errno == EINTR) continue;
            return -1;
        }
        k += (size_t)r;
    }
    return 0;
}

static void serve(int fd, struct ga_shm *shm)
{
    struct ga_cuda *c;
    char ok;

    /* a waiting host thread sleeps instead of spinning */
    worldgen_blocking_sync();
    c = ga_cuda_new(GA_CUDA_BATCH, GA_CUDA_SEEDS, NULL);
    ok = c != NULL;
    if (full_write(fd, &ok, 1) || !ok) _exit(1);
    for (;;)
    {
        char turn;

        if (full_read(fd, &turn, 1)) break;
        shm->rc = ga_cuda_run(c, shm->req, shm->n, shm->ids, shm->metas, shm->biome, shm->tops, shm->rand);
        if (full_write(fd, &turn, 1)) break;
    }
    if (c) ga_cuda_free(c);
    _exit(0);
}

static int ga_client_run(void *ctx, const struct ga_req *req, int n, uint8_t *ids, uint8_t *metas, uint8_t *biome,
                         uint8_t *tops, uint64_t *rand)
{
    struct ga_client *k = ctx;
    struct ga_shm *m = k->shm;
    char turn = 't';

    if (n > GA_CUDA_BATCH) return -1;
    m->n = n;
    memcpy(m->req, req, sizeof *req * (size_t)n);
    if (full_write(k->fd, &turn, 1) || full_read(k->fd, &turn, 1) || m->rc != 0) return -1;
    memcpy(ids, m->ids, (size_t)GA_CELLS * (size_t)n);
    memcpy(metas, m->metas, (size_t)GA_CELLS * (size_t)n);
    memcpy(biome, m->biome, 256 * (size_t)n);
    memcpy(tops, m->tops, 256 * (size_t)n);
    memcpy(rand, m->rand, sizeof *rand * (size_t)n);
    return 0;
}

static void ga_client_free(void *ctx)
{
    struct ga_client *k = ctx;

    close(k->fd);
    waitpid(k->pid, NULL, 0);
    munmap(k->shm, sizeof *k->shm);
    free(k);
}

struct ga_gen *ga_gen_cuda_create(void)
{
    struct ga_shm *shm = mmap(NULL, sizeof *shm, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    int sv[2];

    if (shm == MAP_FAILED) return NULL;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv))
    {
        munmap(shm, sizeof *shm);
        return NULL;
    }
    fflush(NULL);

    pid_t pid = fork();

    if (pid == 0)
    {
        close(sv[0]);
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        serve(sv[1], shm);
    }
    close(sv[1]);

    char ok = 0;

    if (pid < 0 || full_read(sv[0], &ok, 1) || !ok)
    {
        fprintf(stderr, "gen-ahead cuda: the generator process did not start\n");
        close(sv[0]);
        if (pid > 0) waitpid(pid, NULL, 0);
        munmap(shm, sizeof *shm);
        return NULL;
    }

    struct ga_client *k = calloc(1, sizeof *k);

    k->shm = shm;
    k->fd = sv[0];
    k->pid = pid;

    struct ga_gen *g = calloc(1, sizeof *g);

    g->name = "cuda";
    g->ctx = k;
    g->max_batch = GA_CUDA_BATCH;
    g->run = ga_client_run;
    g->free = ga_client_free;
    return g;
}

/* ------------------------------------------------------------ in this process
 *
 * For a process whose grave (grave.c) scans only its environments' images
 * (csrc/runtime/pool.c), the generators run in the process itself, on a
 * stream of their own, so their waits are not the renderer's. */

static struct ga_gen *ga_cuda_another(const struct ga_gen *g)
{
    const struct ga_cuda *c = g->ctx;
    int previous = worldgen_device_current();

    if (worldgen_device_use(c->device)) return NULL;
    struct ga_gen *next = ga_gen_cuda_local_create_prio(c->max_batch, c->max_seeds, c->low);
    worldgen_device_use(previous);
    return next;
}

struct ga_gen *ga_gen_cuda_local_create(int max_batch, int max_seeds)
{
    return ga_gen_cuda_local_create_prio(max_batch, max_seeds, 0);
}

struct ga_gen *ga_gen_cuda_local_create_prio(int max_batch, int max_seeds, int low)
{
    /* the flag takes only before the device's context exists; otherwise
     * waits spin, as the renderer's do */
    worldgen_blocking_sync();

    void *st = worldgen_stream_new(low);
    struct ga_cuda *c = st != NULL ? ga_cuda_new(max_batch, max_seeds, st) : NULL;

    if (c == NULL)
    {
        fprintf(stderr, "gen-ahead cuda: the generators do not start on this device\n");
        return NULL;
    }
    c->low = low;

    struct ga_gen *g = calloc(1, sizeof *g);

    g->name = "cuda";
    g->ctx = c;
    g->max_batch = max_batch;
    g->run = ga_cuda_run;
    g->run_built = ga_cuda_run_built;
    g->free = ga_cuda_free;
    g->host_alloc = ga_cuda_host_alloc;
    g->host_free = ga_cuda_host_free;
    g->another = ga_cuda_another;
    return g;
}
