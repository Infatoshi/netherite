/* The light engine as block-level device functions (GPU plan L16), for any
 * kernel that holds a world's chunks in the env image's layout (world.h
 * struct chunk, bands by bandoff): cl_update (World.updateLightByType),
 * cl_run_update, cl_check_light_at (World.func_147451_t), populate_column
 * (Chunk.func_150811_f) and relight_checks (Chunk.enqueueRelightChecks);
 * cl_run_record runs one record of the capture (engine/lightcap.h). Every thread
 * of a block of CL_THREADS calls them together. A call reads the chunks of
 * S.win, the 5x5 around (S.wcx, S.wcz); a chunk write that needs storage
 * takes the chunk's home band (home(): the mirror's allocator, the one piece
 * a kernel on the env image replaces with the image heap's).
 *
 * The engine is compiled as separate units (-rdc=true, device-linked at the
 * end), so an edit recompiles only its own: this header keeps the layout,
 * the small helpers every unit inlines and the units' entry points;
 * update.cu the update, drivers.cu the drivers and
 * cl_run_record, replay.cu the replay kernels and their comparison,
 * host.cu the host API and the tables (dL, dOp, dLt, dAir and the
 * facings, declared here, defined and filled there). */
#ifndef NETHERITE_LIGHTING_DEV_CUH
#define NETHERITE_LIGHTING_DEV_CUH

#include "lighting.h"

#include <limits.h>
#include <stdint.h>

#define CL_THREADS 1024
#define CL_QUEUE 32768
/* the writer map: one entry per cell of the queue's 64^3 coordinate box */
#define CL_MAP (64 * 64 * 64)
#define CL_NONE INT_MAX

extern __constant__ struct lc_layout dL;
extern __constant__ uint8_t dOp[4096], dLt[4096], dAir[4096];

/* Facing.offsetsXForSide / Y / Z (the decrease's order) and the spread's six
 * ifs (x-1, x+1, y-1, y+1, z-1, z+1) */
extern __constant__ int FX[6], FY[6], FZ[6];
extern __constant__ int SX[6], SY[6], SZ[6];

enum { SKY = 0, BLOCK = 1 };
#define DEF(t) ((t) == SKY ? 15 : 0)
#define STALE ((uint8_t *)1)

/* a driver's list of calls: (type, x, y, z) */
#define CL_LIST 2048

struct cl_scratch {
    struct lighting_result res;
    int32_t queue[CL_QUEUE];
    int32_t writer[CL_MAP];
    int4 list[CL_LIST];
    /* the deferred mode (lightcap.h LC_F_AUTH): the world's epoch and write
     * sequence after the last record, the slots written since the last
     * collect and those whose sky map was regenerated, and the collect's
     * list */
    uint64_t aepoch, awseq;
    uint32_t dirty[LC_SLOTS / 32], regen[LC_SLOTS / 32];
    uint32_t coll_n;
    uint32_t coll_slot[LC_SLOTS];
    uint64_t coll_off[LC_SLOTS];
};

/* one call's view: the 5x5 chunks, the cell, the run's counters */
struct cl_call {
    uint8_t *win[LC_WIN_N];
    int what, type, x, y, z, no_generate, remote, wcx, wcz, dim;
    /* the world's epoch and write sequence, advanced by each update */
    uint64_t epoch, wseq;
    int ret, head, tail, spread, overflow, jstar, jcreate, acc;
    uint32_t nt, touched;
    /* the record's chunks (bits of the 5x5) whose stamp and write sequence,
     * and each edge stamp, a write of this record has set; the window's new
     * ones (see update: C's chunk_touch sets them, the device takes the
     * largest of the record's writes) */
    uint32_t etouched[4], wnew, wenew[4];
    int err;
    uint64_t windows;
    /* the record's updates: how many, their queue lengths, any cap hit */
    uint32_t calls;
    uint64_t tail_sum;
    int ovf_any;
    /* the entry that makes a section, committed last in its window: its
     * cell, value and epoch, and whether it goes on to its neighbours (its
     * level, for the decrease) */
    int cr, cr_x, cr_y, cr_z, cr_v, cr_k, cr_exp, cr_lvl;
    int stop;
    /* the record's chunks whose sky map a new section regenerated */
    uint32_t regen;
};

static __device__ __forceinline__ int xz_ok(int v)
{
    return v >= -30000000 && v < 30000000;
}

static __device__ __forceinline__ uint8_t *win_at(cl_call &S, int cx, int cz)
{
    int dx = cx - S.wcx + 2, dz = cz - S.wcz + 2;

    if ((unsigned)dx >= LC_WIN || (unsigned)dz >= LC_WIN)
    {
        S.err = CL_ERR_WINDOW;
        return NULL;
    }

    uint8_t *c = S.win[dz * LC_WIN + dx];

    if (c == STALE)
    {
        S.err = CL_ERR_STALE;
        return NULL;
    }
    return c;
}

/* the window index of chunk (cx, cz), -1 outside the 5x5 */
static __device__ __forceinline__ int win_index(const cl_call &S, int cx, int cz)
{
    int dx = cx - S.wcx + 2, dz = cz - S.wcz + 2;

    return (unsigned)dx < LC_WIN && (unsigned)dz < LC_WIN ? dz * LC_WIN + dx : -1;
}

static __device__ __forceinline__ unsigned cmask(const uint8_t *c)
{
    return *(const volatile uint16_t *)(c + dL.mask);
}

static __device__ __forceinline__ int present(const uint8_t *c, int y)
{
    return cmask(c) >> (y >> 4) & 1;
}

static __device__ __forceinline__ int32_t *heights(uint8_t *c)
{
    return (int32_t *)(c + dL.height);
}

static __device__ __forceinline__ int64_t *bands(uint8_t *c)
{
    return (int64_t *)(c + dL.band);
}

static __device__ __forceinline__ uint8_t *sec_at(uint8_t *c, int s)
{
    int64_t o = ((volatile int64_t *)bands(c))[s];
    return o ? c + o : NULL;
}

/* the mirror's band s of a chunk: the storage a write makes */
static __device__ __forceinline__ int64_t home(int s)
{
    return (int64_t)dL.chunk_slot + (int64_t)s * dL.sec_bytes;
}

static __device__ __forceinline__ int cell_in_sec(int lx, int y, int lz)
{
    return lx << 8 | lz << 4 | (y & 15);
}

static __device__ __forceinline__ int nib(const uint8_t *a, int i)
{
    return (((const volatile uint8_t *)a)[i >> 1] >> ((i & 1) << 2)) & 15;
}

static __device__ __forceinline__ void nib_set(uint8_t *a, int i, int v)
{
    uintptr_t b = (uintptr_t)(a + (i >> 1));
    uint32_t *w = (uint32_t *)(b & ~(uintptr_t)3);
    int sh = (int)(b & 3) * 8 + ((i & 1) << 2);

    atomicAnd(w, ~(15u << sh));
    atomicOr(w, (uint32_t)v << sh);
}

/* Chunk.getSavedLightValue for y in the world */
static __device__ int saved_light(uint8_t *c, int type, int lx, int y, int lz)
{
    if (!present(c, y)) return y >= heights(c)[lz << 4 | lx] ? DEF(type) : 0;
    if (type == SKY && c[dL.no_sky]) return 0;

    uint8_t *s = sec_at(c, y >> 4);

    if (s == NULL) return 0;
    return nib(s + (type == SKY ? dL.sec_sky : dL.sec_blocklight), cell_in_sec(lx, y, lz));
}

/* World.getSavedLightValue */
static __device__ int get_light(cl_call &S, int type, int x, int y, int z)
{
    if (y < 0) y = 0;
    if (y >= 256) y = 255;
    if (!(xz_ok(x) && xz_ok(z))) return DEF(type);

    uint8_t *c = win_at(S, x >> 4, z >> 4);

    if (c == NULL) return S.no_generate ? 0 : DEF(type);
    return saved_light(c, type, x & 15, y, z & 15);
}

/* World.getBlock, within loaded chunks (C would load a missing one) */
static __device__ int get_block(cl_call &S, int x, int y, int z)
{
    if (!(xz_ok(x) && xz_ok(z) && y >= 0 && y < 256)) return 0;

    uint8_t *c = win_at(S, x >> 4, z >> 4);

    if (c == NULL)
    {
        /* chunk_for: a world that generates would load it here */
        if (!S.no_generate) S.err = CL_ERR_LOAD;
        return 0;
    }
    if (!present(c, y)) return 0;

    uint8_t *s = sec_at(c, y >> 4);

    return s ? ((volatile uint8_t *)s)[dL.sec_ids + cell_in_sec(x & 15, y, z & 15)] : 0;
}

/* World.computeLightValue. Its reads (the column height, the block, the six
 * neighbours' light) are issued together before the early exits, so their
 * latencies overlap; a cell the engine computes has its chunk loaded and its
 * neighbours inside the 5x5, so the reads the sequential code would skip
 * fail nothing. */
static __device__ int compute(cl_call &S, int type, int x, int y, int z)
{
    uint8_t *c = xz_ok(x) && xz_ok(z) ? win_at(S, x >> 4, z >> 4) : NULL;
    int sky = 0, nb[6];

    if (type == SKY)
    {
        /* canBlockSeeTheSky: a world that generates would load the chunk;
         * one that does not (the client's) reads the empty chunk, false */
        if (c == NULL)
        {
            if (!S.no_generate) S.err = CL_ERR_LOAD;
        }
        else sky = y >= heights(c)[(z & 15) << 4 | (x & 15)];
    }

    int id = get_block(S, x, y, z);

#pragma unroll
    for (int i = 0; i < 6; ++i) nb[i] = get_light(S, type, x + FX[i], y + FY[i], z + FZ[i]);
    if (sky) return 15;

    int level = type == SKY ? 0 : dLt[id];
    int op = dOp[id];

    if (op >= 15 && dLt[id] > 0) op = 1;
    if (op < 1) op = 1;
    if (op >= 15) return 0;
    if (level >= 14) return level;

#pragma unroll
    for (int i = 0; i < 6; ++i)
    {
        int v = nb[i] - op;

        if (v > level) level = v;
        if (level >= 14) return level;
    }
    return level;
}

/* A block-wide exclusive sum over the CL_THREADS threads (every one calls
 * it), with the total: the one scan the engine needs, written out rather
 * than taken from cub, whose headers cost every unit about 4 s of compile
 * (lane/cudasplit). Integer sums, so any order gives the same values. */
struct cl_scan {
    int warp[CL_THREADS / 32];
};

static __device__ __forceinline__ void cl_exclusive_sum(cl_scan &t, int v, int &pre, int &total)
{
    int lane = threadIdx.x & 31, w = threadIdx.x >> 5, x = v;

#pragma unroll
    for (int o = 1; o < 32; o <<= 1)
    {
        int y = __shfl_up_sync(0xffffffffu, x, o);

        if (lane >= o) x += y;
    }
    __syncthreads();   /* the last call's readers of t are done */
    if (lane == 31) t.warp[w] = x;
    __syncthreads();
    if (w == 0)
    {
        int s = t.warp[lane];

#pragma unroll
        for (int o = 1; o < 32; o <<= 1)
        {
            int y = __shfl_up_sync(0xffffffffu, s, o);

            if (lane >= o) s += y;
        }
        t.warp[lane] = s;
    }
    __syncthreads();
    pre = (w ? t.warp[w - 1] : 0) + x - v;
    total = t.warp[CL_THREADS / 32 - 1];
}

/* World.doChunksNearChunkExist(x, y, z, 17), the update's guard; on the
 * client (world.c chunks_exist_box) only its y test, since
 * ChunkProviderClient.chunkExists is always true */
static __device__ int box_loaded(cl_call &S)
{
    int ok = S.y + 17 >= 0 && S.y - 17 < 256;

    if (S.remote) return ok;

    for (int cx = (S.x - 17) >> 4; ok && cx <= (S.x + 17) >> 4; ++cx)
        for (int cz = (S.z - 17) >> 4; ok && cz <= (S.z + 17) >> 4; ++cz)
        {
            int k = win_index(S, cx, cz);

            if (k < 0) S.err = CL_ERR_WINDOW;
            else if (S.win[k] == NULL) ok = 0;
        }
    return ok;
}

/* The update's head alone, by one thread on its own view: 0 when the guard
 * refuses it, 1 when the computed light equals the stored one (the queue
 * stays empty and nothing is written), 2 when the queue would start. */
static __device__ int update_head(cl_call &S)
{
    if (!box_loaded(S)) return 0;
    return get_light(S, S.type, S.x, S.y, S.z) == compute(S, S.type, S.x, S.y, S.z) ? 1 : 2;
}

/* the units' entry points (every thread of the block calls each) */
/* World.updateLightByType on S's window (update.cu) */
__device__ void cl_update(cl_call &S, cl_scratch *d, cl_scan &ts);
/* one update of type at (x, y, z); its return value */
__device__ int cl_run_update(cl_call &S, cl_scratch *d, cl_scan &ts, int type, int x, int y, int z);
/* World.func_147451_t: the sky pass where the world has a sky, then block light */
__device__ int cl_check_light_at(cl_call &S, cl_scratch *d, cl_scan &ts, int x, int y, int z);
/* the record's work: one update, or a driver over the window's centre chunk;
 * its return value (drivers.cu) */
__device__ int cl_run_record(cl_call &S, cl_scratch *d, cl_scan &ts, const lc_call *r);
/* the kernels (replay.cu) */
__global__ void __launch_bounds__(CL_THREADS, 1) light_replay(const uint8_t *stream, const uint32_t *offs, uint32_t nrec,
                                                           uint8_t *mirror, cl_scratch *d);
__global__ void __launch_bounds__(256, 1) light_collect(uint8_t *mirror, uint32_t nslots, cl_scratch *d, uint8_t *out,
                                                     uint64_t cap);

#endif
