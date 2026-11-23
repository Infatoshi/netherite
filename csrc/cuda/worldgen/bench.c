/* worldgen_bench: device time of the CUDA chunk generators (cuda/worldgen, the
 * overworld; cuda/worldgen, the Nether and the End) per batch of N chunks, and
 * the C engine's raw generation per chunk on one host thread beside it (GPU
 * design review, lane/gpureview). Device time is CUDA events on the legacy
 * default stream around worldgen_generate / worldgen_dim_generate (the kernels only;
 * no submit or fetch), the median of REPS batches of fresh chunks.
 *
 *   worldgen_bench [--reps R] [--c N] [--seeds S [--per P]] N...
 * --seeds S: the batches as the env pool's device generation makes them with a
 * seed per env (pipe_bench --env-seeds): N chunks over S world seeds, P a seed
 * (default 5, a step's new chunks on the walk's frontier) in a row, each
 * seed's row elsewhere, so the carvers' sources are shared within a row only
 * (lane/gpuspec).
 * Built by hand (see out/report.md of lane/gpureview):
 *   gcc $(CFLAGS) -I/usr/local/cuda/include -c cuda/worldgen/bench.c
 *   nvcc -arch=sm_86 -rdc=true -o out/native/worldgen_bench worldgen_bench.o
 *        $(CU_OBJ) $(DIM_OBJ) worldgen_tables.o libnetherite.a -lz -lm */
#define _POSIX_C_SOURCE 200809L
#include "worldgen.h"
#include "../../engine/chunkgen.h"
#include "../../engine/jmath.h"
#include "../../engine/nether.h"
#include "../../engine/end.h"

#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    int reps = 5, cn = 64, ns[32], nn = 0, nseeds = 0, per = 5;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seeds") && i + 1 < argc) nseeds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--per") && i + 1 < argc) per = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--c") && i + 1 < argc) cn = atoi(argv[++i]);
        else if (nn < 32) ns[nn++] = atoi(argv[i]);
    }
    if (!nn) { fprintf(stderr, "usage: worldgen_bench [--reps R] [--c N] N...\n"); return 2; }
    int maxn = 0;
    for (int i = 0; i < nn; ++i) if (ns[i] > maxn) maxn = ns[i];
    jmath_init();
    if (worldgen_blocking_sync()) return 2;
    int64_t seed = 1, seeds[1024];
    int ns_ = nseeds > 0 ? nseeds : 1;
    if (ns_ > 1024 || per < 1) return 2;
    for (int i = 0; i < ns_; ++i) seeds[i] = seed + i;
    struct worldgen *g = worldgen_create(maxn, ns_, 0);
    struct worldgen_dim *d = worldgen_dim_create(maxn, ns_, 0);
    if (!g || !d || worldgen_set_seeds(g, seeds, ns_) || worldgen_dim_set_seeds(d, seeds, ns_)) return 2;
    struct worldgen_req *rq = malloc(sizeof *rq * maxn);
    struct worldgen_dim_req *dq = malloc(sizeof *dq * maxn);
    cudaEvent_t e0, e1;
    cudaEventCreate(&e0);
    cudaEventCreate(&e1);
    float *ms = malloc(sizeof(float) * reps);
    int base = 1000;   /* fresh coordinates for every batch: no chunk twice */
    /* each stage's device time at the largest N (worldgen_run / worldgen_dim_run in order) */
    for (int dim = 0; dim < 3; ++dim)
    {
        int n = maxn, nst = dim == 0 ? WORLDGEN_STAGES : WORLDGEN_DIM_STAGES;
        for (int i = 0; i < n; ++i)
        {
            rq[i] = (struct worldgen_req){0, base + i % 64, base + i / 64};
            dq[i] = (struct worldgen_dim_req){0, dim == 1 ? -1 : 1, base + i % 64, base + i / 64};
        }
        base += 97;
        if (dim == 0 ? worldgen_submit(g, rq, n) : worldgen_dim_submit(d, dq, n)) return 2;
        printf("stages %s N=%d:", dim == 0 ? "overworld" : dim == 1 ? "nether" : "end", n);
        for (int st = 0; st < nst; ++st)
        {
            float t;
            cudaEventRecord(e0, 0);
            if (dim == 0 ? worldgen_run(g, st) : worldgen_dim_run(d, st)) return 2;
            cudaEventRecord(e1, 0);
            cudaEventSynchronize(e1);
            cudaEventElapsedTime(&t, e0, e1);
            printf(" %.2f", t);
        }
        if (dim == 0)
        {
            /* the built stage over the same batch (the pool's device
             * generation serves it): its kernel and the copies back */
            static struct worldgen_built *hb;
            static uint8_t *bb;
            float t;
            if (hb == NULL) hb = worldgen_host_alloc(sizeof *hb * (size_t)n), bb = worldgen_host_alloc((size_t)n * 16 * WORLDGEN_BAND_BYTES);
            cudaEventRecord(e0, 0);
            if (hb == NULL || bb == NULL || worldgen_fetch_built(g, hb, bb, NULL)) return 2;
            cudaEventRecord(e1, 0);
            cudaEventSynchronize(e1);
            cudaEventElapsedTime(&t, e0, e1);
            printf(" (built and its copy %.2f)", t);
        }
        printf(" ms\n");
    }
    printf("dim\tN\tdevice_ms_median\tdevice_ms_min\tus_per_chunk\n");
    for (int dim = 0; dim < 3; ++dim)
        for (int k = 0; k < nn; ++k)
        {
            int n = ns[k];
            for (int r = -1; r < reps; ++r)   /* r = -1 warms up */
            {
                for (int i = 0; i < n; ++i)
                {
                    int cx = base + i % 64, cz = base + i / 64, s = 0;
                    if (nseeds > 0)
                    {
                        /* seed s's row of per chunks, rows 37 chunks apart */
                        int row = i / per;
                        s = row % nseeds;
                        cx = base + 37 * row + i % per, cz = base + 11 * (row % 7);
                    }
                    rq[i] = (struct worldgen_req){s, cx, cz};
                    dq[i] = (struct worldgen_dim_req){s, dim == 1 ? -1 : 1, cx, cz};
                }
                base += 97;
                if (dim == 0 ? worldgen_submit(g, rq, n) : worldgen_dim_submit(d, dq, n)) return 2;
                cudaEventRecord(e0, 0);
                if (dim == 0 ? worldgen_generate(g) : worldgen_dim_generate(d)) return 2;
                cudaEventRecord(e1, 0);
                cudaEventSynchronize(e1);
                if (r >= 0) cudaEventElapsedTime(&ms[r], e0, e1);
            }
            qsort(ms, reps, sizeof *ms, cmpf);
            printf("%s\t%d\t%.3f\t%.3f\t%.1f\n", dim == 0 ? "overworld" : dim == 1 ? "nether" : "end", n,
                   ms[reps / 2], ms[0], 1000.0 * ms[reps / 2] / n);
        }
    if (worldgen_errors(g) || worldgen_dim_errors(d)) { fprintf(stderr, "device error bits set\n"); return 1; }
    /* the C engine on this thread: raw generation (chunkgen_raw; the Nether's
     * terrain, surface and caves; the End's terrain) per chunk */
    static struct chunkgen cg;
    static struct nether ne;
    static struct end en;
    chunkgen_init(&cg, seed);
    nether_init(&ne, seed);
    end_init(&en, seed);
    uint16_t *ids = malloc(sizeof(uint16_t) * 65536);
    uint8_t *metas = malloc(65536);
    int *bio = malloc(sizeof(int) * 256);
    for (int dim = 0; dim < 3; ++dim)
    {
        double t0 = now();
        for (int i = 0; i < cn; ++i)
        {
            int cx = -5000 + i % 64, cz = -5000 + i / 64;
            if (dim == 0) chunkgen_raw(&cg, cx, cz, ids, metas, bio);
            else if (dim == 1)
            {
                nether_terrain(&ne, cx, cz, ids);
                nether_surface(&ne, cx, cz, ids);
                nether_caves(seed, cx, cz, ids);
            }
            else { end_terrain(&en, cx, cz, ids); end_surface(cx, cz, ids); }
        }
        printf("C %s\t%d chunks\t%.1f us per chunk (host wall, one thread)\n",
               dim == 0 ? "overworld" : dim == 1 ? "nether" : "end", cn, 1e6 * (now() - t0) / cn);
    }
    worldgen_free(g);
    worldgen_dim_free(d);
    return 0;
}
