/* ChunkProviderHell.provideChunk and ChunkProviderEnd.provideChunk on the GPU,
 * ported from the C engine that is their oracle: csrc/engine/nether.c, end.c
 * and world.c's load_dim_chunk. Function and variable roles follow those
 * files; the comments here only say what the GPU layout changes.
 *
 * Layout. One cd_seed per world seed (both providers' noise generators and
 * the cave longs), built on the device. Per chunk, in request order: the
 * density field (425 doubles), the Nether's three surface noises (768
 * doubles), the providers' raw array (32768 bytes: every id they make is below
 * 256), and the constructed chunk's ids (65536) and biome array (256).
 *
 * Kernels (dev.cuh lists the units):
 *  - density: a block per chunk, a thread per noise sample, then a thread per
 *    cell block of the trilinear fill (C's incremental sums).
 *  - surface, caves: a thread per chunk, one sequential Random each.
 *  - construct: a thread per cell of the 256-high chunk. */
#include "dev.cuh"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

__constant__ int c_dneg;
__constant__ double c_curve[17];

__global__ void dim_seeds(struct cd_seed *seeds, const int64_t *values, int n)
{
    int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    struct cd_seed *s = &seeds[k];
    int64_t seed = values[k];
    s->seed = seed;

    /* nether_init: the construction order consumes one rand */
    jrand r;
    jr_seed(&r, seed);
#pragma unroll 1
    for (int i = 0; i < 16; ++i) improved_init(&s->n_lo[i], &r);
#pragma unroll 1
    for (int i = 0; i < 16; ++i) improved_init(&s->n_hi[i], &r);
#pragma unroll 1
    for (int i = 0; i < 8; ++i) improved_init(&s->n_main[i], &r);
#pragma unroll 1
    for (int i = 0; i < 4; ++i) improved_init(&s->n_sand[i], &r);
#pragma unroll 1
    for (int i = 0; i < 4; ++i) improved_init(&s->n_excl[i], &r);

    /* end_init */
    jr_seed(&r, seed);
#pragma unroll 1
    for (int i = 0; i < 16; ++i) improved_init(&s->e_lo[i], &r);
#pragma unroll 1
    for (int i = 0; i < 16; ++i) improved_init(&s->e_hi[i], &r);
#pragma unroll 1
    for (int i = 0; i < 8; ++i) improved_init(&s->e_main[i], &r);
#pragma unroll 1
    for (int i = 0; i < 10; ++i) improved_init(&s->e_island[i], &r);

    /* MapGenBase.func_151539_a's two longs */
    jr_seed(&r, seed);
    s->carve_kx = jr_long(&r);
    s->carve_kz = jr_long(&r);
}

/* load_dim_chunk: the raw array into the 256-high chunk (every cell above 127
 * stays air), and the provider's biome: BiomeGenBase.hell or sky everywhere */
__global__ void dim_construct(const struct worldgen_dim_req *req, int n, const uint8_t *raw_all, uint8_t *ids_all,
                                uint8_t *biomes_all)
{
    size_t t = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
    size_t k = t / CELLS;
    if (k >= (size_t)n) return;
    int i = (int)(t % CELLS), y = i & 255;
    ids_all[t] = y < 128 ? raw_all[k * WORLDGEN_DIM_RAW + (size_t)((i >> 12) << 11 | ((i >> 8) & 15) << 7 | y)] : BLK_AIR;
    if (i < 256)
    {
        int nether = req[k].dim == -1;
        if (c_dneg == WORLDGEN_DIM_NEG_CONSTRUCT) nether = !nether;
        biomes_all[k * 256 + (size_t)i] = nether ? BIOME_HELL : BIOME_SKY;
    }
}

/* ---------------------------------------------------------------- host */

struct worldgen_dim {
    int max_batch, max_seeds, n;
    struct cd_seed *seeds;
    int64_t *seed_values;
    struct worldgen_dim_req *req;
    double *field, *noise;
    uint8_t *raw, *ids, *biomes;
    uint64_t *rands;
    struct cv_work cw;
    cudaStream_t st;            /* worldgen_dim_set_stream's, 0 the legacy default stream */
    cudaEvent_t ev;             /* a waiting host thread sleeps on it (set_stream's stream only) */
};

/* the stream's work so far, waited for: on a set stream by a blocking
 * event, so the waiting thread sleeps (the context was made before
 * worldgen_blocking_sync could take, in a process that renders too, and a
 * spinning wait took a worker's core), else the stream's own wait */
static cudaError_t worldgen_dim_wait(struct worldgen_dim *g)
{
    if (g->ev == NULL) return cudaStreamSynchronize(g->st);
    cudaError_t e = cudaEventRecord(g->ev, g->st);
    return e != cudaSuccess ? e : cudaEventSynchronize(g->ev);
}

#define CK(x) do { cudaError_t e_ = (x); if (e_ != cudaSuccess) { \
    fprintf(stderr, "worldgen_dim: %s: %s (%s:%d)\n", #x, cudaGetErrorString(e_), __FILE__, __LINE__); return -1; } } while (0)
#define CKN(x) do { cudaError_t e_ = (x); if (e_ != cudaSuccess) { \
    fprintf(stderr, "worldgen_dim: %s: %s (%s:%d)\n", #x, cudaGetErrorString(e_), __FILE__, __LINE__); worldgen_dim_free(g); return NULL; } } while (0)

extern "C" struct worldgen_dim *worldgen_dim_create(int max_batch, int max_seeds, int negative)
{
    struct worldgen_dim *g = (struct worldgen_dim *)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->max_batch = max_batch;
    g->max_seeds = max_seeds;

    /* nether.c's curve, computed on the host with the same libm cos as the C
     * engine: the device cos is not glibc's */
    double curve[17];
    for (int i = 0; i < 17; ++i)
    {
        curve[i] = cos((double)i * 3.141592653589793 * 6.0 / 17.0) * 2.0;
        double t = (double)i;
        if (i > 17 / 2)
            t = (double)(17 - 1 - i);
        if (t < 4.0)
        {
            t = 4.0 - t;
            curve[i] -= t * t * t * 10.0;
        }
    }
    CKN(cudaMemcpyToSymbol(c_curve, curve, sizeof curve));
    CKN(cudaMemcpyToSymbol(c_dneg, &negative, sizeof negative));
    unsigned zero = 0;
    CKN(cudaMemcpyToSymbol(d_err, &zero, sizeof zero));
    tables<<<65536 / 256, 256>>>();
    CKN(cudaGetLastError());

    size_t B = (size_t)max_batch;
    CKN(cudaMalloc(&g->seeds, sizeof(struct cd_seed) * (size_t)max_seeds));
    CKN(cudaMalloc(&g->seed_values, sizeof(int64_t) * (size_t)max_seeds));
    CKN(cudaMalloc(&g->req, sizeof(struct worldgen_dim_req) * B));
    CKN(cudaMalloc(&g->field, sizeof(double) * 425 * B));
    CKN(cudaMalloc(&g->noise, sizeof(double) * 768 * B));
    CKN(cudaMalloc(&g->raw, (size_t)WORLDGEN_DIM_RAW * B));
    CKN(cudaMalloc(&g->ids, (size_t)CELLS * B));
    CKN(cudaMalloc(&g->biomes, 256 * B));
    CKN(cudaMalloc(&g->rands, sizeof(uint64_t) * B));
    if (cv_work_alloc(&g->cw, max_batch))
    {
        worldgen_dim_free(g);
        return NULL;
    }
    CKN(cudaDeviceSynchronize());
    return g;
}

extern "C" void worldgen_dim_free(struct worldgen_dim *g)
{
    if (!g) return;
    cudaFree(g->seeds);
    cudaFree(g->seed_values);
    cudaFree(g->req);
    cudaFree(g->field);
    cudaFree(g->noise);
    cudaFree(g->raw);
    cudaFree(g->ids);
    cudaFree(g->biomes);
    cudaFree(g->rands);
    cv_work_free(&g->cw);
    if (g->ev) cudaEventDestroy(g->ev);
    free(g);
}

extern "C" void worldgen_dim_set_stream(struct worldgen_dim *g, void *stream)
{
    g->st = (cudaStream_t)stream;
    if (g->ev == NULL && stream != NULL &&
        cudaEventCreateWithFlags(&g->ev, cudaEventBlockingSync | cudaEventDisableTiming) != cudaSuccess)
        g->ev = NULL;
}

extern "C" int worldgen_dim_set_seeds(struct worldgen_dim *g, const int64_t *seeds, int n)
{
    if (n > g->max_seeds) return -1;
    CK(cudaMemcpyAsync(g->seed_values, seeds, sizeof(int64_t) * (size_t)n, cudaMemcpyHostToDevice, g->st));
    dim_seeds<<<(n + 31) / 32, 32, 0, g->st>>>(g->seeds, g->seed_values, n);
    CK(cudaGetLastError());
    CK(worldgen_dim_wait(g));
    return 0;
}

extern "C" int worldgen_dim_submit(struct worldgen_dim *g, const struct worldgen_dim_req *req, int n)
{
    if (n > g->max_batch) return -1;
    for (int i = 0; i < n; ++i)
        if (req[i].dim != -1 && req[i].dim != 1) return -1;
    g->n = n;
    CK(cudaMemcpyAsync(g->req, req, sizeof(struct worldgen_dim_req) * (size_t)n, cudaMemcpyHostToDevice, g->st));
    CK(worldgen_dim_wait(g));
    return 0;
}

extern "C" int worldgen_dim_run(struct worldgen_dim *g, int stage)
{
    int n = g->n;
    if (n == 0) return 0;
    switch (stage)
    {
    case WORLDGEN_DIM_DENSITY:
        dim_density<<<n, 256, 0, g->st>>>(g->seeds, g->req, n, g->field, g->noise, g->raw);
        break;
    case WORLDGEN_DIM_SURFACE:
        dim_surface<<<n, 256, 0, g->st>>>(g->req, n, g->noise, g->raw, g->rands);
        break;
    case WORLDGEN_DIM_CAVES:
        if (worldgen_dim_carve_run(&g->cw, g->seeds, g->req, n, g->raw, g->st)) return -1;
        break;
    case WORLDGEN_DIM_CONSTRUCT:
    {
        size_t cells = (size_t)CELLS * (size_t)n;
        dim_construct<<<(unsigned)((cells + 255) / 256), 256, 0, g->st>>>(g->req, n, g->raw, g->ids, g->biomes);
        break;
    }
    default:
        return -1;
    }
    CK(cudaGetLastError());
    return 0;
}

extern "C" int worldgen_dim_generate(struct worldgen_dim *g)
{
    for (int s = 0; s < WORLDGEN_DIM_STAGES; ++s)
        if (worldgen_dim_run(g, s)) return -1;
    CK(worldgen_dim_wait(g));
    return 0;
}

extern "C" int worldgen_dim_fetch_raw(struct worldgen_dim *g, uint8_t *raw)
{
    CK(cudaMemcpyAsync(raw, g->raw, (size_t)WORLDGEN_DIM_RAW * (size_t)g->n, cudaMemcpyDeviceToHost, g->st));
    CK(worldgen_dim_wait(g));
    return 0;
}

extern "C" int worldgen_dim_fetch_rand(struct worldgen_dim *g, uint64_t *rands)
{
    CK(cudaMemcpyAsync(rands, g->rands, sizeof(uint64_t) * (size_t)g->n, cudaMemcpyDeviceToHost, g->st));
    CK(worldgen_dim_wait(g));
    return 0;
}

extern "C" int worldgen_dim_fetch_field(struct worldgen_dim *g, double *field)
{
    CK(cudaMemcpyAsync(field, g->field, sizeof(double) * 425 * (size_t)g->n, cudaMemcpyDeviceToHost, g->st));
    CK(worldgen_dim_wait(g));
    return 0;
}

extern "C" int worldgen_dim_fetch_chunk(struct worldgen_dim *g, uint8_t *ids, uint8_t *biomes)
{
    CK(cudaMemcpyAsync(ids, g->ids, (size_t)CELLS * (size_t)g->n, cudaMemcpyDeviceToHost, g->st));
    CK(cudaMemcpyAsync(biomes, g->biomes, 256 * (size_t)g->n, cudaMemcpyDeviceToHost, g->st));
    CK(worldgen_dim_wait(g));
    return 0;
}

extern "C" unsigned worldgen_dim_errors(struct worldgen_dim *g)
{
    (void)g;
    unsigned e = 0;
    if (cudaMemcpyFromSymbol(&e, d_err, sizeof e) != cudaSuccess) return ~0u;
    return e;
}
