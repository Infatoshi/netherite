/* The light engine's drivers (func_150809_p's columns, func_150801_a's edge,
 * the relight checks) and cl_run_record (dev.cuh), a unit of its own
 * (-rdc). */
#include "dev.cuh"

/* Chunk.func_150808_b's block: the chunk's own cell, air in a missing section */
static __device__ __forceinline__ int chunk_block(uint8_t *c, int lx, int y, int lz)
{
    if (!present(c, y)) return 0;

    uint8_t *s = sec_at(c, y >> 4);

    return s ? ((volatile uint8_t *)s)[dL.sec_ids + cell_in_sec(lx, y, lz)] : 0;
}

/* The heads of calls list[0..m), m = min(n, blockDim), a thread each on its
 * own view of S: hd[i] 0 when the guard refuses the call, 1 when it has
 * nothing to do, 2 when its queue starts (or a read failed). A call with
 * nothing to do changes nothing, so every head up to the first 2 is what
 * the call gives in order. Returns that first index, or m. */
static __device__ int heads(cl_call &S, const int4 *list, int n, int8_t *hd, int *first)
{
    int tid = threadIdx.x, m = min(n, (int)blockDim.x);

    if (tid == 0) *first = m;
    __syncthreads();
    if (tid < m)
    {
        cl_call T;
        int4 c = list[tid];

        for (int w = 0; w < LC_WIN_N; ++w) T.win[w] = S.win[w];
        T.wcx = S.wcx;
        T.wcz = S.wcz;
        T.no_generate = S.no_generate;
        T.remote = S.remote;
        T.err = 0;
        T.type = c.x;
        T.x = c.y;
        T.y = c.z;
        T.z = c.w;

        int h = update_head(T);

        if (T.err) h = 2;
        hd[tid] = (int8_t)h;
        if (h == 2) atomicMin(first, tid);
    }
    __syncthreads();

    int f = *first;

    __syncthreads();
    return f;
}

/* Run the calls list[0..n) in order, those with nothing to do a thread
 * each (heads) and the rest block-wide. per > 0 groups them into checks of
 * per calls (func_147451_t's sky and block passes) and stops after the first
 * check whose calls were all refused, returning 0 (func_150811_f's first
 * loop); otherwise returns 1. */
static __device__ int run_list(cl_call &S, cl_scratch *d, cl_scan &ts, int n, int per, int8_t *hd, int *sh)
{
    int tid = threadIdx.x, acc = 0;

    for (int pos = 0; pos < n;)
    {
        int m = min(n - pos, (int)blockDim.x);
        int f = heads(S, d->list + pos, n - pos, hd, sh);
        int stop = -1;

        /* the checks the calls before f complete */
        if (per > 0)
        {
            for (int i = 0; i < f; ++i)
            {
                acc |= hd[i] != 0;
                if ((pos + i) % per == per - 1)
                {
                    if (!acc)
                    {
                        stop = i;
                        break;
                    }
                    acc = 0;
                }
            }
        }
        if (tid == 0) S.calls += (uint32_t)(stop >= 0 ? stop + 1 : f);
        __syncthreads();
        if (stop >= 0) return 0;
        if (f == m)
        {
            pos += m;
            continue;
        }

        int4 c = d->list[pos + f];
        int r = cl_run_update(S, d, ts, c.x, c.y, c.z, c.w);

        if (per > 0)
        {
            acc |= r;
            if ((pos + f) % per == per - 1)
            {
                if (!acc) return 0;
                acc = 0;
            }
        }
        pos += f + 1;
    }
    return 1;
}

/* Chunk.func_150811_f (world.c chunk_populate_column). Its loops read the
 * chunk's block ids, which light never changes (a section a light write
 * makes holds air, as the missing one read), so each loop's calls are known
 * before the first runs: the first loop's cells, then the second's. */
static __device__ int populate_column(cl_call &S, cl_scratch *d, cl_scan &ts, uint8_t *c, int x, int z)
{
    __shared__ uint8_t op[256], lt[256];
    __shared__ int8_t hd[CL_THREADS];
    __shared__ int sh, n1, n2;
    int tid = threadIdx.x;
    int cx = *(int32_t *)(c + dL.cx), cz = *(int32_t *)(c + dL.cz);
    int top = 0, per = S.dim == 0 ? 2 : 1;

    for (int s = 15; s >= 0; --s)
        if (present(c, s << 4))
        {
            top = s << 4;
            break;
        }
    if (tid < 256)
    {
        int id = chunk_block(c, x, tid, z);

        op[tid] = dOp[id];
        lt[tid] = dLt[id];
    }
    __syncthreads();
    if (tid == 0)
    {
        int reached = 0, sealed = 0, y, k = 0;

        for (y = top + 16 - 1; y > 63 || (y > 0 && !sealed); --y)
        {
            int o = op[y];

            if (o == 255 && y < 63) sealed = 1;
            if (!reached && o > 0) reached = 1;
            else if (reached && o == 0)
            {
                if (per == 2) d->list[k++] = make_int4(SKY, cx * 16 + x, y, cz * 16 + z);
                d->list[k++] = make_int4(BLOCK, cx * 16 + x, y, cz * 16 + z);
            }
        }
        n1 = k;
        for (; y > 0; --y)
            if (lt[y] > 0)
            {
                if (per == 2) d->list[k++] = make_int4(SKY, cx * 16 + x, y, cz * 16 + z);
                d->list[k++] = make_int4(BLOCK, cx * 16 + x, y, cz * 16 + z);
            }
        n2 = k - n1;
    }
    __syncthreads();

    int a = n1, b = n2;

    __syncthreads();
    if (!run_list(S, d, ts, a, per, hd, &sh)) return 0;
    if (b > 0)
    {
        /* the second loop's calls, moved to the list's head */
        for (int i = tid; i < b; i += blockDim.x) d->list[CL_LIST - b + i] = d->list[a + i];
        __syncthreads();
        for (int i = tid; i < b; i += blockDim.x) d->list[i] = d->list[CL_LIST - b + i];
        __syncthreads();
        run_list(S, d, ts, b, 0, hd, &sh);
    }
    return 1;
}

/* Chunk.enqueueRelightChecks (world.c chunk_enqueue_relight_checks). A
 * cursor step's calls follow from its column's section flag, read when the
 * step starts, and from block ids, which light never changes; so the calls
 * of the steps from `from` on are listed at once from list[base] (a thread
 * per step, y and cell) and the step starts noted. A block-wide call that
 * makes a section of the chunk changes the later steps' flags: they are
 * listed again. */
#define CL_RSTEP (16 * 7)

static __device__ int relight_list(cl_call &S, cl_scratch *d, cl_scan &ts, uint8_t *c, int q0, int steps,
                                   int from, int base, int *start)
{
    int tid = threadIdx.x, per = S.dim == 0 ? 2 : 1;
    int cx = *(int32_t *)(c + dL.cx), cz = *(int32_t *)(c + dL.cz);
    int j = from + tid / CL_RSTEP, t = tid % CL_RSTEP, in = 0;
    int px = 0, py = 0, pz = 0;

    if (j < steps)
    {
        int q = q0 + j, s = q % 16, x = q / 16 % 16, z = q / 256;
        int y = t / 7, k = t % 7, wx = (cx << 4) + x, wz = (cz << 4) + z, wy = (s << 4) + y;
        const int KX[7] = {0, 0, -1, 1, 0, 0, 0}, KY[7] = {-1, 1, 0, 0, 0, 0, 0}, KZ[7] = {0, 0, 0, 0, -1, 1, 0};

        if (present(c, s << 4)) in = dAir[chunk_block(c, x, wy, z)];
        else in = y == 0 || y == 15 || x == 0 || x == 15 || z == 0 || z == 15;
        px = wx + KX[k];
        py = wy + KY[k];
        pz = wz + KZ[k];
        if (in && k < 6) in = dLt[get_block(S, px, py, pz)] > 0;
    }

    int at, total;

    cl_exclusive_sum(ts, in ? per : 0, at, total);
    if (j < steps && t == 0) start[j] = base + at;
    if (in)
    {
        if (per == 2) d->list[base + at++] = make_int4(SKY, px, py, pz);
        d->list[base + at] = make_int4(BLOCK, px, py, pz);
    }
    if (tid == 0) start[steps] = base + total;
    __syncthreads();
    return base + total;
}

static __device__ void relight_checks(cl_call &S, cl_scratch *d, cl_scan &ts, uint8_t *c)
{
    __shared__ int8_t hd[CL_THREADS];
    __shared__ int sh, start[9];
    int tid = threadIdx.x;
    volatile int32_t *cur = (volatile int32_t *)(c + dL.queued_light_checks);
    int q0 = *cur, steps = q0 >= 4096 ? 0 : min(8, 4096 - q0);

    __syncthreads();
    if (steps == 0) return;
    if (tid == 0) *cur = q0 + steps;

    unsigned mask = cmask(c);
    int n = relight_list(S, d, ts, c, q0, steps, 0, 0, start);

    for (int pos = 0; pos < n;)
    {
        int m = min(n - pos, (int)blockDim.x);
        int f = heads(S, d->list + pos, n - pos, hd, &sh);

        if (tid == 0) S.calls += (uint32_t)f;
        __syncthreads();
        if (f == m)
        {
            pos += m;
            continue;
        }

        int4 k = d->list[pos + f];

        cl_run_update(S, d, ts, k.x, k.y, k.z, k.w);
        pos += f + 1;
        if (cmask(c) != mask)
        {
            /* the steps after the one this call belongs to read the new
             * flags */
            int j = 0;

            while (j + 1 < steps && start[j + 1] <= pos - 1) ++j;
            mask = cmask(c);
            if (j + 1 < steps)
            {
                int base = start[j + 1];

                __syncthreads();
                n = relight_list(S, d, ts, c, q0, steps, j + 1, base, start);
            }
        }
    }
}

/* the record's work: one update, or a driver over the window's centre chunk;
 * its return value */
__device__ int cl_run_record(cl_call &S, cl_scratch *d, cl_scan &ts, const lc_call *r)
{
    uint8_t *c = S.win[LC_WIN_N / 2];
    int ret = 0;

    switch (r->what)
    {
    case LC_O_UPDATE:
        return cl_run_update(S, d, ts, r->type, r->x, r->y, r->z);
    case LC_O_COLUMNS:
        /* func_150809_p's loops: a failed column ends its row only */
        ret = 1;
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                if (!populate_column(S, d, ts, c, x, z))
                {
                    ret = 0;
                    break;
                }
        return ret;
    case LC_O_SIDE:
        /* Chunk.func_150801_a */
        if (!c[dL.terrain_populated]) return 0;
        for (int i = 0; i < 16; ++i)
        {
            if (r->side == 3) populate_column(S, d, ts, c, 15, i);
            else if (r->side == 1) populate_column(S, d, ts, c, 0, i);
            else if (r->side == 0) populate_column(S, d, ts, c, i, 15);
            else if (r->side == 2) populate_column(S, d, ts, c, i, 0);
        }
        return 0;
    case LC_O_RELIGHT:
        relight_checks(S, d, ts, c);
        return 0;
    }
    if (threadIdx.x == 0) S.err = CL_ERR_RECORD;
    return 0;
}
