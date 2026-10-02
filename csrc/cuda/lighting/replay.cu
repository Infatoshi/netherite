/* The light engine on the device and the capture's consumer (lighting.h): the
 * replay kernels and their comparison with C's results (the host side:
 * host.cu). */
#include "dev.cuh"

#include <limits.h>

/* the first difference in n bytes (a is the device's, b is C's, NULL is
 * zeros): its index, or -1 */
static __device__ int first_diff(const uint8_t *a, const uint8_t *b, int n, int *at)
{
    if (threadIdx.x == 0) *at = INT_MAX;
    __syncthreads();
    for (int i = threadIdx.x; i < n; i += blockDim.x)
    {
        int x = a ? a[i] : 0, y = b ? b[i] : 0;

        if (x != y) atomicMin(at, i);
    }
    __syncthreads();

    int r = *at == INT_MAX ? -1 : *at;

    __syncthreads();
    return r;
}

static __device__ void differ(cl_call &S, cl_scratch *d, uint64_t index, int what, int chunk, int cell, int64_t dev,
                              int64_t c)
{
    if (threadIdx.x != 0) return;

    struct lighting_result *r = &d->res;

    r->status = what >= CL_ERR_WINDOW ? 2 : 1;
    r->what = what;
    r->index = index;
    r->type = S.type;
    r->x = S.x;
    r->y = S.y;
    r->z = S.z;
    r->chunk = chunk;
    r->cell = cell;
    r->wcx = S.wcx;
    r->what_rec = S.what;
    r->wcz = S.wcz;
    r->dev = dev;
    r->c = c;
    S.stop = 1;
}

/* the device's result for the call against C's (the record's tail) */
static __device__ void compare(cl_call &S, cl_scratch *d, const lc_call *r, int ret, const uint8_t *after, int *at)
{
    int tid = threadIdx.x;

    if (tid == 0)
    {
        uint64_t idx = r->index;
        int cur = r->what == LC_O_RELIGHT ? *(volatile int32_t *)(S.win[LC_WIN_N / 2] + dL.queued_light_checks) : 0;

        if (S.err) differ(S, d, idx, S.err, -1, -1, 0, 0);
        else if (ret != r->ret) differ(S, d, idx, CL_RET, -1, -1, ret, r->ret);
        else if (S.calls != r->calls) differ(S, d, idx, CL_CALLS, -1, -1, S.calls, r->calls);
        else if (S.tail_sum != r->tail_sum) differ(S, d, idx, CL_TAIL, -1, -1, (int64_t)S.tail_sum, (int64_t)r->tail_sum);
        else if (S.ovf_any != r->overflow) differ(S, d, idx, CL_OVERFLOW, -1, -1, S.ovf_any, r->overflow);
        else if (S.epoch != r->epoch1) differ(S, d, idx, CL_EPOCH, -1, -1, (int64_t)S.epoch, (int64_t)r->epoch1);
        else if (S.wseq != r->wseq1) differ(S, d, idx, CL_WSEQ, -1, -1, (int64_t)S.wseq, (int64_t)r->wseq1);
        else if (S.touched != r->written) differ(S, d, idx, CL_WRITTEN, -1, -1, S.touched, r->written);
        else if (r->what == LC_O_RELIGHT && cur != r->cursor1) differ(S, d, idx, CL_CURSOR, LC_WIN_N / 2, -1, cur, r->cursor1);
    }
    __syncthreads();
    if (S.stop) return;

    const uint8_t *p = after;
    size_t hdr = (dL.chunk_bytes + 7) & ~(size_t)7;
    uint64_t bytes = 0;

    for (int k = 0; k < LC_WIN_N; ++k)
    {
        if (!(r->written >> k & 1)) continue;

        const lc_after *a = (const lc_after *)p;
        const uint8_t *cc = p + sizeof *a;
        const uint8_t *cb = cc + hdr;
        uint8_t *c = S.win[k];

        if (tid == 0)
        {
            uint16_t cm = *(const uint16_t *)(cc + dL.mask);
            int32_t chm = *(const int32_t *)(cc + dL.height_min), dhm = *(const int32_t *)(c + dL.height_min);

            if (cmask(c) != cm) differ(S, d, r->index, CL_MASK, k, -1, cmask(c), cm);
            else if (dhm != chm) differ(S, d, r->index, CL_HEIGHT_MIN, k, -1, dhm, chm);
            else if (*(uint64_t *)(c + dL.stamp) != *(const uint64_t *)(cc + dL.stamp))
                differ(S, d, r->index, CL_STAMP, k, -1, (int64_t) * (uint64_t *)(c + dL.stamp), (int64_t) * (const uint64_t *)(cc + dL.stamp));
            else if (*(uint64_t *)(c + dL.wseq) != *(const uint64_t *)(cc + dL.wseq))
                differ(S, d, r->index, CL_CWSEQ, k, -1, (int64_t) * (uint64_t *)(c + dL.wseq), (int64_t) * (const uint64_t *)(cc + dL.wseq));
            else
                for (int j = 0; j < 4; ++j)
                {
                    uint64_t de = ((uint64_t *)(c + dL.edge_stamp))[j], ce = ((const uint64_t *)(cc + dL.edge_stamp))[j];

                    if (de != ce)
                    {
                        differ(S, d, r->index, CL_EDGE, k, j, (int64_t)de, (int64_t)ce);
                        break;
                    }
                }
            for (int s = 0; s < 16 && !S.stop; ++s)
            {
                int dp = bands(c)[s] != 0, cp = a->present >> s & 1;

                if (dp != cp) differ(S, d, r->index, CL_PRESENT, k, s, dp, cp);
            }
        }
        __syncthreads();
        if (S.stop) return;

        int j = first_diff(c + dL.height, cc + dL.height, 1024, at);

        if (j >= 0)
        {
            differ(S, d, r->index, CL_HEIGHT, k, j / 4, ((int32_t *)(c + dL.height))[j / 4], ((const int32_t *)(cc + dL.height))[j / 4]);
            __syncthreads();
            return;
        }
        j = first_diff(c + dL.precip, cc + dL.precip, 1024, at);
        if (j >= 0)
        {
            differ(S, d, r->index, CL_PRECIP, k, j / 4, ((int32_t *)(c + dL.precip))[j / 4], ((const int32_t *)(cc + dL.precip))[j / 4]);
            __syncthreads();
            return;
        }

        const uint8_t *q = cb;

        for (int s = 0; s < 16; ++s)
        {
            uint8_t *sec = sec_at(c, s);
            int cp = a->present >> s & 1;

            for (int f = 0; f < 2; ++f)
            {
                const uint8_t *mine = sec ? sec + (f ? dL.sec_blocklight : dL.sec_sky) : NULL;
                const uint8_t *theirs = cp ? q + f * 2048 : NULL;

                j = first_diff(mine, theirs, 2048, at);
                if (j >= 0)
                {
                    int cell = s << 12 | j << 1;

                    differ(S, d, r->index, f ? CL_BLOCK : CL_SKY, k, cell, mine ? mine[j] : 0, theirs ? theirs[j] : 0);
                    __syncthreads();
                    return;
                }
            }
            if (cp) q += 4096;
        }
        bytes += 16 * 4096 + 2 * 1024 + 4 + 2 + 6 * 8;
        p = q;
    }
    if (tid == 0) d->res.bytes += bytes;
}

static __device__ void put(uint8_t *mirror, const lc_put *r, int64_t *oldb)
{
    int tid = threadIdx.x;
    uint8_t *c = mirror + (size_t)r->slot * ((size_t)dL.chunk_slot + 16 * (size_t)dL.sec_bytes);
    const uint8_t *p = (const uint8_t *)(r + 1);
    size_t hdr = (dL.chunk_bytes + 7) & ~(size_t)7;

    if (tid < 16) oldb[tid] = bands(c)[tid];
    __syncthreads();
    for (int i = tid; i < (int)(dL.chunk_bytes / 4); i += blockDim.x) ((uint32_t *)c)[i] = ((const uint32_t *)p)[i];
    __syncthreads();
    if (tid < 16)
    {
        int s = tid;
        uint8_t *sec = c + home(s);

        bands(c)[s] = r->present >> s & 1 ? home(s) : 0;
        *(uint32_t *)(sec + dL.sec_shared) = r->shared >> s & 1;
        *(uint64_t *)(sec + dL.sec_ids_hi) = 0;
    }
    p += hdr;
    for (int s = 0; s < 16; ++s)
    {
        uint8_t *sec = c + home(s);

        if (r->sent >> s & 1)
        {
            for (int i = tid; i < 1024; i += blockDim.x) ((uint32_t *)(sec + dL.sec_ids))[i] = ((const uint32_t *)p)[i];
            for (int i = tid; i < 512; i += blockDim.x) ((uint32_t *)(sec + dL.sec_sky))[i] = ((const uint32_t *)(p + 4096))[i];
            for (int i = tid; i < 512; i += blockDim.x) ((uint32_t *)(sec + dL.sec_blocklight))[i] = ((const uint32_t *)(p + 6144))[i];
            p += 8192;
        }
        else if (!(r->present >> s & 1) && oldb[s] != 0)
        {
            for (int i = tid; i < 1024; i += blockDim.x) ((uint32_t *)(sec + dL.sec_ids))[i] = 0;
            for (int i = tid; i < 512; i += blockDim.x) ((uint32_t *)(sec + dL.sec_sky))[i] = 0;
            for (int i = tid; i < 512; i += blockDim.x) ((uint32_t *)(sec + dL.sec_blocklight))[i] = 0;
        }
    }
    __syncthreads();
}

static __device__ __forceinline__ uint8_t *slot_ptr(uint8_t *mirror, uint32_t v)
{
    size_t slot_bytes = (size_t)dL.chunk_slot + 16 * (size_t)dL.sec_bytes;

    return v == 0 ? NULL : v == LC_STALE ? STALE : mirror + (size_t)(v - 1) * slot_bytes;
}

/* one record run block-wide, then compared */
static __device__ void full_record(cl_call &S, cl_scratch *d, cl_scan &ts, uint8_t *mirror, const lc_call *r,
                                   int *at)
{
    int tid = threadIdx.x;

    if (tid == 0)
    {
        S.what = r->what;
        S.type = r->type;
        S.x = r->x;
        S.y = r->y;
        S.z = r->z;
        S.no_generate = r->no_generate;
        S.remote = r->remote;
        S.wcx = r->cx;
        S.wcz = r->cz;
        S.dim = r->dim;
        /* the deferred mode keeps the world's counters itself unless the
         * host moved them */
        int own = (r->flags & LC_F_AUTH) && !(r->flags & LC_F_CTR);

        S.epoch = own ? d->aepoch : r->epoch0;
        S.wseq = own ? d->awseq : r->wseq0;
        S.regen = 0;
        S.err = 0;
        S.stop = 0;
        S.touched = 0;
        for (int j = 0; j < 4; ++j) S.etouched[j] = 0;
        S.windows = 0;
        S.calls = 0;
        S.tail_sum = 0;
        S.ovf_any = 0;
    }
    if (tid < LC_WIN_N) S.win[tid] = slot_ptr(mirror, r->slot[tid]);
    __syncthreads();

    int ret = cl_run_record(S, d, ts, r);

    if (r->flags & LC_F_AUTH)
    {
        /* the device's result stands: the slots it wrote (and the relight
         * cursor's chunk) go back at the next collect */
        if (tid == 0)
        {
            d->aepoch = S.epoch;
            d->awseq = S.wseq;
            if (S.err) differ(S, d, r->index, S.err, -1, -1, 0, 0);
        }
        __syncthreads();
        if (tid < LC_WIN_N && ((S.touched >> tid & 1) || (r->what == LC_O_RELIGHT && tid == LC_WIN_N / 2)))
        {
            uint32_t v = r->slot[tid];

            if (v != 0 && v != LC_STALE)
            {
                atomicOr(&d->dirty[(v - 1) >> 5], 1u << ((v - 1) & 31));
                if (S.regen >> tid & 1) atomicOr(&d->regen[(v - 1) >> 5], 1u << ((v - 1) & 31));
            }
        }
        __syncthreads();
    }
    else compare(S, d, r, ret, (const uint8_t *)(r + 1), at);
    if (tid == 0)
    {
        struct lighting_result *res = &d->res;

        res->records++;
        res->drivers += r->what != LC_O_UPDATE;
        res->calls += S.calls;
        res->refused += r->what == LC_O_UPDATE && ret == 0;
        res->wrote += S.epoch != r->epoch0;
        res->writes += S.epoch - r->epoch0;
        res->windows += S.windows;
        if (S.windows > res->win_max) res->win_max = S.windows;
    }
    __syncthreads();
}

/* A call whose head leaves the queue empty writes nothing, so the mirror
 * is the same before and after it: a run of single updates is evaluated a
 * call per thread against one state, up to the first call whose queue
 * starts (or that cannot be read), which then runs block-wide in order. The
 * record index (offs) lets each thread reach its record. */
__global__ void __launch_bounds__(CL_THREADS, 1) light_replay(const uint8_t *stream, const uint32_t *offs, uint32_t nrec,
                                                           uint8_t *mirror, cl_scratch *d)
{
    __shared__ cl_call S;
    __shared__ cl_scan ts;
    __shared__ int64_t oldb[16];
    __shared__ int at, run, first, bad;
    int tid = threadIdx.x;

    if (d->res.status != 0) return;
    for (uint32_t i = 0; i < nrec;)
    {
        const uint32_t *k = (const uint32_t *)(stream + offs[i]);

        if (k[0] == LC_PUT)
        {
            put(mirror, (const lc_put *)k, oldb);
            if (tid == 0) d->res.puts++;
            ++i;
            continue;
        }
        if (k[0] != LC_CALL)
        {
            if (tid == 0)
            {
                d->res.status = 2;
                d->res.what = CL_ERR_RECORD;
            }
            return;
        }
        if (((const lc_call *)k)->what != LC_O_UPDATE)
        {
            full_record(S, d, ts, mirror, (const lc_call *)k, &at);
            if (S.stop) return;
            ++i;
            continue;
        }

        /* the run: consecutive single updates from i */
        if (tid == 0)
        {
            run = CL_THREADS;
            first = CL_THREADS;
            bad = CL_THREADS;
        }
        __syncthreads();

        uint32_t me = i + (uint32_t)tid;
        const lc_call *r = NULL;

        if (me < nrec)
        {
            const lc_call *c = (const lc_call *)(stream + offs[me]);

            if (c->kind == LC_CALL && c->what == LC_O_UPDATE) r = c;
        }
        if (r == NULL) atomicMin(&run, tid);
        __syncthreads();

        int head = -1;
        cl_call T;

        if (tid < run)
        {
            T.what = LC_O_UPDATE;
            T.type = r->type;
            T.x = r->x;
            T.y = r->y;
            T.z = r->z;
            T.no_generate = r->no_generate;
            T.remote = r->remote;
            T.wcx = r->cx;
            T.wcz = r->cz;
            T.err = 0;
            for (int w = 0; w < LC_WIN_N; ++w) T.win[w] = slot_ptr(mirror, r->slot[w]);
            head = update_head(T);
            if (head == 2 || T.err) atomicMin(&first, tid);
        }
        __syncthreads();

        /* the calls before the first that needs the block: C must have
         * written nothing and left the queue empty */
        if (tid < first && tid < run && !(r->flags & LC_F_AUTH))
        {
            int ok = r->ret == (head != 0) && r->tail_sum == 0 && r->overflow == 0 && r->calls == 1 &&
                     r->epoch1 == r->epoch0 && r->wseq1 == r->wseq0 && r->written == 0;

            if (!ok) atomicMin(&bad, tid);
        }
        __syncthreads();
        if (bad < first && bad < run)
        {
            if (tid == bad)
            {
                struct lighting_result *res = &d->res;
                int what = r->ret != (head != 0) ? CL_RET : r->calls != 1 ? CL_CALLS : r->tail_sum != 0 ? CL_TAIL
                           : r->overflow != 0 ? CL_OVERFLOW : r->epoch1 != r->epoch0 ? CL_EPOCH
                           : r->wseq1 != r->wseq0 ? CL_WSEQ : CL_WRITTEN;

                res->status = 1;
                res->what = what;
                res->index = r->index;
                res->what_rec = LC_O_UPDATE;
                res->type = r->type;
                res->x = r->x;
                res->y = r->y;
                res->z = r->z;
                res->chunk = -1;
                res->cell = -1;
                res->wcx = r->cx;
                res->wcz = r->cz;
                res->dev = what == CL_RET ? head != 0 : 0;
                res->c = what == CL_RET ? r->ret : what == CL_TAIL ? (int64_t)r->tail_sum : what == CL_WRITTEN ? r->written : 1;
            }
            return;
        }

        int done = min(first, run);

        if (tid == 0)
        {
            struct lighting_result *res = &d->res;

            /* the deferred mode: the run writes nothing, so the world's
             * counters after it are the last ones a record brought */
            for (int j = done - 1; j >= 0; --j)
            {
                const lc_call *c = (const lc_call *)(stream + offs[i + j]);

                if ((c->flags & LC_F_AUTH) && (c->flags & LC_F_CTR))
                {
                    d->aepoch = c->epoch0;
                    d->awseq = c->wseq0;
                    break;
                }
            }

            res->records += (uint64_t)done;
            res->calls += (uint64_t)done;
            res->runs++;
            if ((uint64_t)done > res->run_max) res->run_max = (uint64_t)done;
        }
        if (tid < done && head == 0) atomicAdd((unsigned long long *)&d->res.refused, 1ull);
        __syncthreads();
        i += (uint32_t)done;
        if (first < run)
        {
            full_record(S, d, ts, mirror, (const lc_call *)(stream + offs[i]), &at);
            if (S.stop) return;
            ++i;
        }
    }
}

/* The deferred mode's collect: the slots written since the last collect,
 * as lightcap.h's lc_back stream (at most cap bytes), their marks cleared. */
__global__ void __launch_bounds__(256, 1) light_collect(uint8_t *mirror, uint32_t nslots, cl_scratch *d, uint8_t *out,
                                                     uint64_t cap)
{
    int tid = threadIdx.x;
    size_t slot_bytes = (size_t)dL.chunk_slot + 16 * (size_t)dL.sec_bytes;
    size_t hdr = (dL.chunk_bytes + 7) & ~(size_t)7;
    __shared__ uint64_t total;
    __shared__ int32_t status;

    if (tid == 0)
    {
        uint32_t n = 0;
        uint64_t off = sizeof(lc_back);

        for (uint32_t s = 0; s < nslots; ++s)
        {
            if (!(d->dirty[s >> 5] >> (s & 31) & 1)) continue;

            uint8_t *c = mirror + (size_t)s * slot_bytes;
            int nb = 0;

            for (int k = 0; k < 16; ++k) nb += bands(c)[k] != 0;
            d->coll_slot[n] = s;
            d->coll_off[n] = off;
            off += sizeof(lc_after) + hdr + (uint64_t)nb * 4096;
            ++n;
        }
        d->coll_n = n;
        total = off;
        status = off > cap ? -2 : d->res.status;
    }
    __syncthreads();
    if (total > cap)
    {
        if (tid == 0)
        {
            lc_back h = {0, -2, sizeof(lc_back), d->aepoch, d->awseq};
            *(lc_back *)out = h;
        }
        return;
    }
    for (uint32_t i = 0; i < d->coll_n; ++i)
    {
        uint32_t s = d->coll_slot[i];
        uint8_t *c = mirror + (size_t)s * slot_bytes, *p = out + d->coll_off[i];
        uint16_t present = 0, shared = 0;

        for (int k = 0; k < 16; ++k)
            if (bands(c)[k] != 0)
            {
                present |= (uint16_t)(1u << k);
                if (*(const uint32_t *)(c + bands(c)[k] + dL.sec_shared) != 0) shared |= (uint16_t)(1u << k);
            }
        if (tid == 0)
        {
            uint32_t rg = d->regen[s >> 5] >> (s & 31) & 1;
            lc_after a = {s | rg << 31, present, shared};
            *(lc_after *)p = a;
        }
        p += sizeof(lc_after);
        for (int j = tid; j < (int)(dL.chunk_bytes / 4); j += blockDim.x) ((uint32_t *)p)[j] = ((const uint32_t *)c)[j];
        p += hdr;
        for (int k = 0; k < 16; ++k)
        {
            if (!(present >> k & 1)) continue;

            const uint8_t *sec = c + bands(c)[k];

            for (int j = tid; j < 512; j += blockDim.x)
            {
                ((uint32_t *)p)[j] = ((const uint32_t *)(sec + dL.sec_sky))[j];
                ((uint32_t *)(p + 2048))[j] = ((const uint32_t *)(sec + dL.sec_blocklight))[j];
            }
            p += 4096;
        }
    }
    __syncthreads();
    for (uint32_t k = tid; k < (nslots + 31) / 32; k += blockDim.x)
    {
        d->dirty[k] = 0;
        d->regen[k] = 0;
    }
    if (tid == 0)
    {
        lc_back h = {d->coll_n, status, total, d->aepoch, d->awseq};
        *(lc_back *)out = h;
    }
}
