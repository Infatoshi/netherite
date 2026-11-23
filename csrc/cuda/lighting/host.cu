/* The light engine's host side (lighting.h): the device tables the units
 * read (dev.cuh), the mirror, the stream and the replay's launch. */
#include "dev.cuh"

#include <cuda_runtime.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__constant__ struct lc_layout dL;
__constant__ uint8_t dOp[4096], dLt[4096], dAir[4096];
__constant__ int FX[6] = {0, 0, 0, 0, -1, 1}, FY[6] = {-1, 1, 0, 0, 0, 0}, FZ[6] = {0, 0, -1, 1, 0, 0};
__constant__ int SX[6] = {-1, 1, 0, 0, 0, 0}, SY[6] = {0, 0, -1, 1, 0, 0}, SZ[6] = {0, 0, 0, 0, -1, 1};

struct lighting {
    uint8_t *mirror, *stream;
    uint32_t *offs, *hoffs;     /* the stream's record offsets, device and host */
    size_t cap;
    cl_scratch *d;
    uint32_t nslots;            /* the mirror's slots */
    int dev;                    /* the device (-1: the current one, as made) */
    cudaStream_t st;
    uint8_t *back;              /* the deferred mode's collect buffer (back_cap bytes) */
    size_t back_cap;
};

extern "C" const char *lighting_what(int what)
{
    switch (what)
    {
    case CL_RET: return "return value";
    case CL_TAIL: return "queue length (summed over the record's calls)";
    case CL_CALLS: return "calls the driver made";
    case CL_CURSOR: return "relight cursor (queuedLightChecks)";
    case CL_OVERFLOW: return "queue cap hit";
    case CL_EPOCH: return "world epoch after";
    case CL_WSEQ: return "world write sequence after";
    case CL_WRITTEN: return "chunks written (bits of the 5x5)";
    case CL_MASK: return "section mask";
    case CL_HEIGHT: return "height map";
    case CL_HEIGHT_MIN: return "height map minimum";
    case CL_PRECIP: return "precipitation height map";
    case CL_STAMP: return "chunk stamp";
    case CL_EDGE: return "chunk edge stamp";
    case CL_CWSEQ: return "chunk write sequence";
    case CL_PRESENT: return "band storage";
    case CL_SKY: return "sky light";
    case CL_BLOCK: return "block light";
    case CL_ERR_WINDOW: return "a read outside the 5x5 chunks";
    case CL_ERR_LOAD: return "a block read in a chunk that is not loaded";
    case CL_ERR_STALE: return "a read of a chunk the capture did not send";
    case CL_ERR_RECORD: return "a malformed record";
    }
    return "?";
}

static int ck(cudaError_t e, const char *what)
{
    if (e == cudaSuccess) return 0;
    fprintf(stderr, "lighting: %s: %s\n", what, cudaGetErrorString(e));
    return -1;
}

extern "C" struct lighting *lighting_create(const struct lc_layout *L, const uint8_t opacity[4096], const uint8_t light[4096],
                                          const uint8_t air[4096], size_t cap)
{
    return lighting_create_n(L, opacity, light, air, cap, LC_SLOTS, -1);
}

extern "C" struct lighting *lighting_create_n(const struct lc_layout *L, const uint8_t opacity[4096],
                                            const uint8_t light[4096], const uint8_t air[4096], size_t cap, int nslots,
                                            int dev)
{
    if (nslots < 1 || nslots > LC_SLOTS) return NULL;
    if (dev >= 0 && ck(cudaSetDevice(dev), "device")) return NULL;
    /* blocking waits; a context that is already running keeps its flags */
    if (cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync) != cudaSuccess) (void)cudaGetLastError();

    struct lighting *g = (struct lighting *)calloc(1, sizeof *g);
    size_t mirror = (size_t)nslots * LC_SLOT_BYTES(L);
    int32_t *none = (int32_t *)malloc(sizeof(int32_t) * CL_MAP);

    for (int i = 0; i < CL_MAP; ++i) none[i] = CL_NONE;
    g->cap = cap;
    g->nslots = (uint32_t)nslots;
    g->dev = dev;
    /* the collect's worst case: the slots one sync's records may write
     * (lightcap.c syncs before they take half the mirror), every band
     * stored; made at the first collect */
    int nback = nslots / 2 + LC_WIN_N < nslots ? nslots / 2 + LC_WIN_N : nslots;

    g->back_cap = sizeof(lc_back) + (size_t)nback * (sizeof(lc_after) + ((L->chunk_bytes + 7) & ~(size_t)7) + 16 * 4096);
    if (ck(cudaStreamCreateWithFlags(&g->st, cudaStreamNonBlocking), "stream") ||
        ck(cudaMalloc(&g->mirror, mirror), "mirror") || ck(cudaMemset(g->mirror, 0, mirror), "mirror zero") ||
        ck(cudaMalloc(&g->stream, cap), "stream") || ck(cudaMalloc(&g->offs, cap / 8 * sizeof(uint32_t)), "index") ||
        ck(cudaMallocHost(&g->hoffs, cap / 8 * sizeof(uint32_t)), "host index") ||
        ck(cudaMalloc(&g->d, sizeof *g->d), "scratch") ||
        ck(cudaMemset(g->d, 0, sizeof *g->d), "scratch zero") ||
        ck(cudaMemcpy(g->d->writer, none, sizeof(int32_t) * CL_MAP, cudaMemcpyHostToDevice), "writer map") ||
        ck(cudaMemcpyToSymbol(dL, L, sizeof *L), "layout") || ck(cudaMemcpyToSymbol(dOp, opacity, 4096), "opacity") ||
        ck(cudaMemcpyToSymbol(dLt, light, 4096), "light") || ck(cudaMemcpyToSymbol(dAir, air, 4096), "air"))
    {
        free(none);
        lighting_free(g);
        return NULL;
    }
    free(none);
    return g;
}

extern "C" int lighting_run(struct lighting *g, const uint8_t *stream, size_t n)
{
    uint32_t nrec = 0;

    if (n > g->cap) return -1;
    for (size_t o = 0; o < n; ++nrec)
    {
        uint32_t kind, bytes;

        memcpy(&kind, stream + o, 4);
        memcpy(&bytes, stream + o + (kind == LC_PUT ? offsetof(struct lc_put, bytes) : offsetof(struct lc_call, bytes)), 4);
        if ((kind != LC_PUT && kind != LC_CALL) || bytes == 0 || nrec >= g->cap / 8)
        {
            fprintf(stderr, "lighting: a malformed record at %zu\n", o);
            return -1;
        }
        g->hoffs[nrec] = (uint32_t)o;
        o += bytes;
    }
    if (g->dev >= 0 && ck(cudaSetDevice(g->dev), "device")) return -1;
    if (ck(cudaMemcpyAsync(g->stream, stream, n, cudaMemcpyHostToDevice, g->st), "stream copy") ||
        ck(cudaMemcpyAsync(g->offs, g->hoffs, nrec * sizeof(uint32_t), cudaMemcpyHostToDevice, g->st), "index copy"))
        return -1;
    light_replay<<<1, CL_THREADS, 0, g->st>>>(g->stream, g->offs, nrec, g->mirror, g->d);

    int32_t status;

    if (ck(cudaGetLastError(), "launch") ||
        ck(cudaMemcpyAsync(&status, &g->d->res.status, sizeof status, cudaMemcpyDeviceToHost, g->st), "status") ||
        ck(cudaStreamSynchronize(g->st), "replay"))
        return -1;
    return status == 0 ? 0 : 1;
}

extern "C" int lighting_collect(struct lighting *g, uint8_t *out, size_t cap, size_t *n)
{
    lc_back h;

    if (g->dev >= 0 && ck(cudaSetDevice(g->dev), "device")) return -1;
    if (g->back == NULL && ck(cudaMalloc(&g->back, g->back_cap), "collect buffer")) return -1;
    light_collect<<<1, 256, 0, g->st>>>(g->mirror, g->nslots, g->d, g->back, g->back_cap);
    if (ck(cudaGetLastError(), "collect launch") ||
        ck(cudaMemcpyAsync(&h, g->back, sizeof h, cudaMemcpyDeviceToHost, g->st), "collect head") ||
        ck(cudaStreamSynchronize(g->st), "collect"))
        return -1;
    if (h.bytes > cap)
    {
        fprintf(stderr, "lighting: the collect's %llu bytes do not fit %zu\n", (unsigned long long)h.bytes, cap);
        return -1;
    }
    if (ck(cudaMemcpyAsync(out, g->back, h.bytes, cudaMemcpyDeviceToHost, g->st), "collect copy") ||
        ck(cudaStreamSynchronize(g->st), "collect copy"))
        return -1;
    *n = h.bytes;
    return 0;
}

extern "C" size_t lighting_collect_cap(const struct lighting *g)
{
    return g->back_cap;
}

extern "C" void lighting_result(struct lighting *g, struct lighting_result *r)
{
    if (g->dev >= 0) (void)cudaSetDevice(g->dev);
    if (ck(cudaMemcpyAsync(r, &g->d->res, sizeof *r, cudaMemcpyDeviceToHost, g->st), "result") ||
        ck(cudaStreamSynchronize(g->st), "result"))
        memset(r, 0, sizeof *r);
}

extern "C" void lighting_free(struct lighting *g)
{
    if (g == NULL) return;
    if (g->dev >= 0) (void)cudaSetDevice(g->dev);
    cudaFree(g->back);
    if (g->st != NULL) cudaStreamDestroy(g->st);
    cudaFree(g->mirror);
    cudaFree(g->stream);
    cudaFree(g->offs);
    cudaFreeHost(g->hoffs);
    cudaFree(g->d);
    free(g);
}

extern "C" int lighting_report(const struct lighting_result *r, int failed, double wait_s, FILE *out)
{
    if (r->status == 0 && !failed)
    {
        fprintf(out,
                "light device: %llu calls replayed, every one byte-equal to C (%llu records: %llu single calls, %llu "
                "drivers; %llu single calls refused by the 17-block box; %llu records wrote light, %llu writes); "
                "%llu bytes of written chunks compared; %llu chunk puts; %llu windows (at most %llu in one call); "
                "%llu runs of calls a thread each (at most %llu calls); %.1f s waiting on the device\n",
                (unsigned long long)r->calls, (unsigned long long)r->records,
                (unsigned long long)(r->records - r->drivers), (unsigned long long)r->drivers,
                (unsigned long long)r->refused, (unsigned long long)r->wrote, (unsigned long long)r->writes,
                (unsigned long long)r->bytes, (unsigned long long)r->puts, (unsigned long long)r->windows,
                (unsigned long long)r->win_max, (unsigned long long)r->runs, (unsigned long long)r->run_max, wait_s);
        return 0;
    }
    if (r->status == 0)
    {
        fprintf(out, "light device: the device failed (a CUDA error, above)\n");
        return -1;
    }

    int acx = r->wcx, acz = r->wcz;

    static const char *const kind[LC_O_KINDS] = {"an update", "func_150809_p's columns", "func_150801_a's edge",
                                                  "the relight checks"};

    fprintf(out, "light device: record %llu (%s of chunk (%d,%d); its last call %s light at %d %d %d) differs after %llu "
            "equal records: %s",
            (unsigned long long)r->index, kind[r->what_rec >= 0 && r->what_rec < LC_O_KINDS ? r->what_rec : 0], r->wcx,
            r->wcz, r->type == 0 ? "sky" : "block", r->x, r->y, r->z, (unsigned long long)r->records,
            lighting_what(r->what));
    if (r->chunk >= 0)
        fprintf(out, ", chunk (%d,%d)", acx + r->chunk % LC_WIN - 2, acz + r->chunk / LC_WIN - 2);
    if (r->cell >= 0) fprintf(out, ", at %d", r->cell);
    fprintf(out, ": device %lld, C %lld\n", (long long)r->dev, (long long)r->c);
    return -1;
}
