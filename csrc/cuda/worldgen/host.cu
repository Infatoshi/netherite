/* ChunkProviderGenerate.provideChunk (overworld) on the GPU, ported from the C
 * engine that is its oracle: csrc/engine/layers.c, noise.c, terrain.c,
 * surface.c, carve.c and chunkgen.c. Function and variable roles follow those
 * files so the two can be read side by side; the comments here only say what
 * the GPU layout changes.
 *
 * Layout. One seed table per world seed (the provider's noise generators, the
 * layer seeds, the Mesa bands, the carver longs), built on the device. Per
 * chunk, in request order: generation biomes (100), block biomes (256), the
 * density field (825 doubles), the surface noise (256 doubles), block ids and
 * metadata (65536 bytes each: every id the raw pipeline makes is below 256),
 * and the biome topBlock table the surface pass leaves for the carvers.
 *
 * Kernels (each in its own translation unit, see dev.cuh):
 *  - layers: a block per chunk, a thread per layer cell (layers.cu).
 *  - density: a block per chunk, a thread per noise sample, then a thread per
 *    4x4x8 cell block of the trilinear fill (the same incremental sums as C).
 *  - surface: a thread per chunk. It draws one Random sequentially and
 *    reads its own writes, so the order is the C order.
 *  - caves, ravines: each source chunk's tunnels walked once, a thread a
 *    tunnel, then a block per chunk carves a thread a column
 *    (carve.cuh).
 *  - construct: the Chunk constructor keeps metadata only under blocks.
 *  - built (built.cu, worldgen_fetch_built's): the constructor's bands and
 *    the sky light over them, a block per chunk, a thread per column.
 */
#include "dev.cuh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- tables */

__device__ struct worldgen_biome d_biomes[256];
__device__ float d_sin[65536];
__device__ float d_parabolic[25];
__device__ unsigned d_err;
__constant__ int c_negative;

static const struct lnode h_graph[NODES] = LAYER_GRAPH;
__constant__ struct lnode c_graph[NODES] = LAYER_GRAPH;
__constant__ int c_off[NODES], c_maxw[NODES], c_maxh[NODES];

__global__ void tables(void)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < 65536) d_sin[i] = (float)sin((double)i * 3.141592653589793 * 2.0 / 65536.0);
    if (i < 25)
    {
        int a = i % 5 - 2, b = i / 5 - 2;
        d_parabolic[i] = 10.0f / (float)sqrt((double)((float)(a * a + b * b) + 0.2f));
    }
}

/* new Chunk(world, Block[], byte[], cx, cz) stores metadata only for blocks
 * that are not air */
__global__ void construct(const uint8_t *ids, uint8_t *metas, size_t cells)
{
    size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < cells && ids[i] == BLK_AIR) metas[i] = 0;
}

/* ---------------------------------------------------------------- host */

struct worldgen {
    int max_batch, max_seeds, n, negative, layer_shared;
    struct cg_seed *seeds;
    int64_t *seed_values;
    struct worldgen_req *req;
    uint8_t *gen100, *full256, *ids, *metas, *tops;
    uint64_t *rands;
    double *field, *stone;
    struct worldgen_built *bhdr;    /* worldgen_fetch_built's headers and bands, made at its first call */
    uint8_t *bands;
    struct cv_work cw;
    cudaStream_t st;            /* worldgen_set_stream's, 0 the legacy default stream */
    cudaEvent_t ev;             /* a waiting host thread sleeps on it (set_stream's stream only) */
};

/* the stream's work so far, waited for: on a set stream by a blocking
 * event, so the waiting thread sleeps (the context was made before
 * worldgen_blocking_sync could take, in a process that renders too, and a
 * spinning wait took a worker's core), else the stream's own wait */
static cudaError_t worldgen_wait(struct worldgen *g)
{
    if (g->ev == NULL) return cudaStreamSynchronize(g->st);
    cudaError_t e = cudaEventRecord(g->ev, g->st);
    return e != cudaSuccess ? e : cudaEventSynchronize(g->ev);
}

#define CK(x) do { cudaError_t e_ = (x); if (e_ != cudaSuccess) { \
    fprintf(stderr, "worldgen: %s: %s (%s:%d)\n", #x, cudaGetErrorString(e_), __FILE__, __LINE__); return -1; } } while (0)
#define CKN(x) do { cudaError_t e_ = (x); if (e_ != cudaSuccess) { \
    fprintf(stderr, "worldgen: %s: %s (%s:%d)\n", #x, cudaGetErrorString(e_), __FILE__, __LINE__); worldgen_free(g); return NULL; } } while (0)

extern "C" int worldgen_blocking_sync(void)
{
    return cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync) == cudaSuccess ? 0 : -1;
}

extern "C" struct worldgen *worldgen_create(int max_batch, int max_seeds, int negative)
{
    struct worldgen *g = (struct worldgen *)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->max_batch = max_batch;
    g->max_seeds = max_seeds;
    g->negative = negative;

    /* Size each layer node's area: its width depends only on cx and its
     * height only on cz, through the same shifts, and repeats with period at
     * most 256 chunks (the island layer is 1:4096 blocks), so a sweep over
     * 4096 values finds the largest area any chunk asks for. */
    int maxw[NODES] = {0}, maxh[NODES] = {0}, off[NODES];
    for (int c = -2048; c < 2048; ++c)
    {
        int b[NODES][4];
        layer_boxes(h_graph, c, c, b);
        for (int i = 0; i < NODES; ++i)
        {
            if (b[i][2] - b[i][0] > maxw[i]) maxw[i] = b[i][2] - b[i][0];
            if (b[i][3] - b[i][1] > maxh[i]) maxh[i] = b[i][3] - b[i][1];
        }
    }
    int total = 0;
    for (int i = 0; i < NODES; ++i)
    {
        off[i] = total;
        total += maxw[i] * maxh[i];
    }
    g->layer_shared = total * (int)sizeof(int);

    struct worldgen_biome rows[256];
    worldgen_biome_rows(rows);
    CKN(cudaMemcpyToSymbol(d_biomes, rows, sizeof rows));
    CKN(cudaMemcpyToSymbol(c_off, off, sizeof off));
    CKN(cudaMemcpyToSymbol(c_maxw, maxw, sizeof maxw));
    CKN(cudaMemcpyToSymbol(c_maxh, maxh, sizeof maxh));
    CKN(cudaMemcpyToSymbol(c_negative, &negative, sizeof negative));
    uint8_t opacity[256], tick[256];
    worldgen_block_rows(opacity, tick);
    CKN(cudaMemcpyToSymbol(c_opacity, opacity, sizeof opacity));
    CKN(cudaMemcpyToSymbol(c_tick, tick, sizeof tick));
    unsigned zero = 0;
    CKN(cudaMemcpyToSymbol(d_err, &zero, sizeof zero));
    if (g->layer_shared > 48 * 1024)
        CKN(cudaFuncSetAttribute(layers, cudaFuncAttributeMaxDynamicSharedMemorySize, g->layer_shared));
    tables<<<65536 / 256, 256>>>();
    CKN(cudaGetLastError());

    size_t B = (size_t)max_batch;
    CKN(cudaMalloc(&g->seeds, sizeof(struct cg_seed) * (size_t)max_seeds));
    CKN(cudaMalloc(&g->seed_values, sizeof(int64_t) * (size_t)max_seeds));
    CKN(cudaMalloc(&g->req, sizeof(struct worldgen_req) * B));
    CKN(cudaMalloc(&g->gen100, 100 * B));
    CKN(cudaMalloc(&g->full256, 256 * B));
    CKN(cudaMalloc(&g->tops, 256 * B));
    CKN(cudaMalloc(&g->rands, sizeof(uint64_t) * B));
    CKN(cudaMalloc(&g->ids, (size_t)CELLS * B));
    CKN(cudaMalloc(&g->metas, (size_t)CELLS * B));
    CKN(cudaMalloc(&g->field, sizeof(double) * 825 * B));
    CKN(cudaMalloc(&g->stone, sizeof(double) * 256 * B));
    if (cv_work_alloc(&g->cw, max_batch))
    {
        worldgen_free(g);
        return NULL;
    }
    CKN(cudaDeviceSynchronize());
    return g;
}

extern "C" void worldgen_free(struct worldgen *g)
{
    if (!g) return;
    cudaFree(g->seeds);
    cudaFree(g->seed_values);
    cudaFree(g->req);
    cudaFree(g->gen100);
    cudaFree(g->full256);
    cudaFree(g->tops);
    cudaFree(g->rands);
    cudaFree(g->ids);
    cudaFree(g->metas);
    cudaFree(g->field);
    cudaFree(g->stone);
    cudaFree(g->bhdr);
    cudaFree(g->bands);
    cv_work_free(&g->cw);
    if (g->ev) cudaEventDestroy(g->ev);
    free(g);
}

extern "C" int worldgen_device_current(void)
{
    int d = 0;
    return cudaGetDevice(&d) == cudaSuccess ? d : 0;
}

extern "C" int worldgen_device_use(int dev)
{
    return cudaSetDevice(dev) == cudaSuccess ? 0 : -1;
}

extern "C" void *worldgen_host_alloc(size_t bytes)
{
    void *p = NULL;
    return cudaHostAlloc(&p, bytes, cudaHostAllocDefault) == cudaSuccess ? p : NULL;
}

extern "C" void worldgen_host_free(void *p)
{
    if (p) cudaFreeHost(p);
}

extern "C" void *worldgen_stream_new(int low)
{
    /* the device's highest priority: a round's kernels go ahead of the
     * renderer's and the mesher's blocks, whose frames do not wait on it
     * (low: its lowest, behind them) */
    int lo = 0, hi = 0;
    cudaStream_t s;
    cudaDeviceGetStreamPriorityRange(&lo, &hi);
    return cudaStreamCreateWithPriority(&s, cudaStreamNonBlocking, low ? lo : hi) == cudaSuccess ? (void *)s : NULL;
}

extern "C" void worldgen_set_stream(struct worldgen *g, void *stream)
{
    g->st = (cudaStream_t)stream;
    if (g->ev == NULL && stream != NULL &&
        cudaEventCreateWithFlags(&g->ev, cudaEventBlockingSync | cudaEventDisableTiming) != cudaSuccess)
        g->ev = NULL;
}

extern "C" int worldgen_set_seeds(struct worldgen *g, const int64_t *seeds, int n)
{
    if (n > g->max_seeds) return -1;
    CK(cudaMemcpyAsync(g->seed_values, seeds, sizeof(int64_t) * (size_t)n, cudaMemcpyHostToDevice, g->st));
    gen_seeds<<<(n + 31) / 32, 32, 0, g->st>>>(g->seeds, g->seed_values, n);
    CK(cudaGetLastError());
    CK(worldgen_wait(g));
    return 0;
}

extern "C" int worldgen_submit(struct worldgen *g, const struct worldgen_req *req, int n)
{
    if (n > g->max_batch) return -1;
    g->n = n;
    CK(cudaMemcpyAsync(g->req, req, sizeof(struct worldgen_req) * (size_t)n, cudaMemcpyHostToDevice, g->st));
    CK(worldgen_wait(g));
    return 0;
}

extern "C" int worldgen_run(struct worldgen *g, int stage)
{
    int n = g->n;
    if (n == 0) return 0;
    switch (stage)
    {
    case WORLDGEN_LAYERS:
        layers<<<n, 128, g->layer_shared, g->st>>>(g->seeds, g->req, n, g->gen100, g->full256);
        break;
    case WORLDGEN_DENSITY:
        density<<<n, 256, 0, g->st>>>(g->seeds, g->req, n, g->gen100, g->field, g->stone, g->ids);
        break;
    case WORLDGEN_SURFACE:
        CK(cudaMemsetAsync(g->metas, 0, (size_t)CELLS * (size_t)n, g->st));
        surface<<<n, 256, 0, g->st>>>(g->seeds, g->req, n, g->full256, g->stone, g->ids, g->metas, g->tops,
                                                  g->rands);
        break;
    case WORLDGEN_CAVES:
    case WORLDGEN_RAVINES:
        if (worldgen_carve_run(&g->cw, stage == WORLDGEN_RAVINES, g->seeds, g->req, n, g->full256, g->tops, g->ids, g->st))
            return -1;
        break;
    case WORLDGEN_CONSTRUCT:
    {
        size_t cells = (size_t)CELLS * (size_t)n;
        construct<<<(unsigned)((cells + 255) / 256), 256, 0, g->st>>>(g->ids, g->metas, cells);
        break;
    }
    default:
        return -1;
    }
    CK(cudaGetLastError());
    return 0;
}

extern "C" int worldgen_generate(struct worldgen *g)
{
    for (int s = 0; s < WORLDGEN_STAGES; ++s)
        if (worldgen_run(g, s)) return -1;
    CK(worldgen_wait(g));
    return 0;
}

extern "C" int worldgen_fetch_ids(struct worldgen *g, uint8_t *ids)
{
    CK(cudaMemcpyAsync(ids, g->ids, (size_t)CELLS * (size_t)g->n, cudaMemcpyDeviceToHost, g->st));
    CK(worldgen_wait(g));
    return 0;
}

extern "C" int worldgen_fetch_metas(struct worldgen *g, uint8_t *metas)
{
    CK(cudaMemcpyAsync(metas, g->metas, (size_t)CELLS * (size_t)g->n, cudaMemcpyDeviceToHost, g->st));
    CK(worldgen_wait(g));
    return 0;
}

extern "C" int worldgen_fetch_built(struct worldgen *g, struct worldgen_built *hdr, uint8_t *bands, int *top_band)
{
    int n = g->n, top = -1;
    if (g->bands == NULL)
    {
        CK(cudaMalloc(&g->bhdr, sizeof(struct worldgen_built) * (size_t)g->max_batch));
        CK(cudaMalloc(&g->bands, (size_t)16 * WORLDGEN_BAND_BYTES * (size_t)g->max_batch));
    }
    if (n > 0)
    {
        built<<<n, 256, 0, g->st>>>(g->ids, g->metas, n, g->bhdr, g->bands);
        CK(cudaGetLastError());
        CK(cudaMemcpyAsync(hdr, g->bhdr, sizeof *hdr * (size_t)n, cudaMemcpyDeviceToHost, g->st));
        CK(worldgen_wait(g));
        unsigned all = 0;
        for (int i = 0; i < n; ++i) all |= hdr[i].mask;
        top = all != 0 ? 31 - __builtin_clz(all) : -1;
        /* each chunk's bands up to the batch's top one: a 2D copy, rows of
         * the chunks' 16-band pitch */
        size_t pitch = (size_t)16 * WORLDGEN_BAND_BYTES;
        if (top >= 0)
        {
            CK(cudaMemcpy2DAsync(bands, pitch, g->bands, pitch, (size_t)(top + 1) * WORLDGEN_BAND_BYTES, (size_t)n,
                                 cudaMemcpyDeviceToHost, g->st));
            CK(worldgen_wait(g));
        }
    }
    if (top_band) *top_band = top;
    return 0;
}

extern "C" int worldgen_fetch_biomes(struct worldgen *g, uint8_t *gen100, uint8_t *full256)
{
    CK(cudaMemcpyAsync(gen100, g->gen100, 100 * (size_t)g->n, cudaMemcpyDeviceToHost, g->st));
    CK(cudaMemcpyAsync(full256, g->full256, 256 * (size_t)g->n, cudaMemcpyDeviceToHost, g->st));
    CK(worldgen_wait(g));
    return 0;
}

extern "C" int worldgen_fetch_tops(struct worldgen *g, uint8_t *tops, uint64_t *rands)
{
    CK(cudaMemcpyAsync(tops, g->tops, 256 * (size_t)g->n, cudaMemcpyDeviceToHost, g->st));
    CK(cudaMemcpyAsync(rands, g->rands, sizeof(uint64_t) * (size_t)g->n, cudaMemcpyDeviceToHost, g->st));
    CK(worldgen_wait(g));
    return 0;
}

extern "C" int worldgen_fetch_field(struct worldgen *g, double *field)
{
    CK(cudaMemcpyAsync(field, g->field, sizeof(double) * 825 * (size_t)g->n, cudaMemcpyDeviceToHost, g->st));
    CK(worldgen_wait(g));
    return 0;
}

extern "C" int worldgen_fetch_sin(struct worldgen *g, float *table)
{
    (void)g;
    CK(cudaMemcpyFromSymbol(table, d_sin, sizeof(float) * 65536));
    return 0;
}

extern "C" unsigned worldgen_errors(struct worldgen *g)
{
    (void)g;
    unsigned e = 0;
    if (cudaMemcpyFromSymbol(&e, d_err, sizeof e) != cudaSuccess) return ~0u;
    return e;
}

extern "C" int worldgen_layer_shared_bytes(struct worldgen *g)
{
    return g->layer_shared;
}
