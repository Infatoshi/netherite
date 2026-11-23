/* The ray cast on the device, the host's side (rcdev.h): the device memory,
 * the tables, the records' copies and the launches. */
#include "rcdev.h"

#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kern.h"

/* the records an env stages before its worker copies them in itself (a
 * reset's whole window, about 800 records, is about 33 such copies) */
#define RC_STAGE_RECS 128
/* launches in flight, each with its own lists (a slot's are reused once its event is done) */
#define RC_SLOTS 8

struct rc_slot {
    /* the launch's env ids: page-locked and mapped (ids_map), read in place by rc_head_get, which leaves the device's
     * copy in d_ids for the cast (lane/cuda-graph: no copy call a launch) */
    int *ids_pin, *ids_map, *d_ids;
    int2 *refs_pin, *refs_map; /* the launch's records (env, record), page-locked, read by the device in place */
    cudaEvent_t ev;
    int used;
};

/* A launch's inputs the workers wrote (each env's head and staged records) and its seen records are page-locked and
 * mapped, read and written by the device in place over the bus (lane/ntcast): the launch packs nothing on the calling
 * thread (lane/rcdev's pack of a group's records was 0.25 ms a launch of 128 envs at a reset's window) and copies only
 * each head's used part (rc_head_get: the camera, light and fog through its last entity box, against 8.4 KB a head) */
struct rcdev {
    int n, w, h, R, dev;
    size_t cap;                 /* a stage's bytes */
    struct rcmirror m;
    struct rcd_head *heads, *heads_map, *d_heads;
    struct rcd_col *d_cols;     /* [n][R * R] */
    struct rcd_sec *d_secs;     /* [n][R * R][16] */
    unsigned char *stage, *stage_map, *d_stage;   /* [n][cap] */
    uint8_t *d_frames;          /* [n][h][w][ch] */
    int ch;                     /* the frame's bytes a pixel (client.channels): 3, or 1 the luma */
    uint8_t *hframes;           /* the split's host frames [n][h][w][3], page-locked (rcdev_host_frames) */
    unsigned long long *d_seen_key;
    int *d_seen_n, *d_seen_xyz;
    struct rcd_seen *d_seen, *seen, *seen_map;
    int2 *wrefs_pin, *d_wrefs;  /* the workers' copies' lists, [n][RC_STAGE_RECS] */
    int *d_iota;                /* [n]: i at i (one env's launch list) */
    void *tabs[12];             /* the tables' device arrays */
    struct rcd_tabdev tabd;     /* the tables as the device reads them (rcdev_dump) */
    int32_t nspr;
    size_t nface;
    struct rc_slot slot[RC_SLOTS];
    int next;
    size_t bytes;
    char err[256];
};

#define RC_TRY(call)                                                                                              \
    do {                                                                                                          \
        cudaError_t e_ = (call);                                                                                  \
        if (e_ != cudaSuccess)                                                                                    \
        {                                                                                                         \
            snprintf(d->err, sizeof d->err, "%s: %s", #call, cudaGetErrorString(e_));                             \
            goto fail;                                                                                            \
        }                                                                                                         \
    } while (0)

static int dev_alloc(struct rcdev *d, void **p, size_t n)
{
    if (cudaMalloc(p, n ? n : 16) != cudaSuccess) return -1;
    d->bytes += n;
    return 0;
}

static int dev_copy(struct rcdev *d, void **p, const void *src, size_t n, int k)
{
    if (dev_alloc(d, p, n)) return -1;
    d->tabs[k] = *p;
    return cudaMemcpy(*p, src, n, cudaMemcpyHostToDevice) == cudaSuccess ? 0 : -1;
}

/* rcmirror's flush: env id's stage is full; its worker copies it in now */
static int worker_flush(void *ctx, int id)
{
    struct rcdev *d = (struct rcdev *)ctx;
    struct rcm_env *E = &d->m.env[id];
    if (cudaSetDevice(d->dev) != cudaSuccess) return -1;
    cudaStream_t s = cudaStreamPerThread;
    int2 *refs = d->wrefs_pin + (size_t)id * RC_STAGE_RECS;
    for (int r = 0; r < E->nrec; ++r) refs[r] = make_int2(id, r);
    if (cudaMemcpyAsync(d->d_stage + (size_t)id * d->cap, E->stage, E->used, cudaMemcpyHostToDevice, s) != cudaSuccess) return -1;
    if (cudaMemcpyAsync(d->d_wrefs + (size_t)id * RC_STAGE_RECS, refs, (size_t)E->nrec * sizeof *refs, cudaMemcpyHostToDevice, s) !=
        cudaSuccess)
        return -1;
    rc_launch_scatter(d->d_stage, d->cap, d->d_wrefs + (size_t)id * RC_STAGE_RECS, E->nrec, d->d_cols, d->d_secs, d->R, s);
    return cudaStreamSynchronize(s) == cudaSuccess ? 0 : -1;
}

extern "C" struct rcdev *rcdev_new(int n, int w, int h, int ch, int range, int dev, const struct rcd_tabsrc *t,
                                    char *err, size_t en)
{
    static_assert(sizeof(struct rcd_head) % 16 == 0, "the head is copied 16 bytes at a time");
    struct rcdev *d = (struct rcdev *)calloc(1, sizeof *d);
    if (d == NULL) return snprintf(err, en, "rcdev: out of memory"), (struct rcdev *)NULL;
    d->n = n, d->w = w, d->h = h, d->dev = dev, d->ch = ch == 1 ? 1 : 3;
    d->R = rcmirror_side(range);
    d->cap = (size_t)RC_STAGE_RECS * RCD_REC_BYTES;
    size_t RR = (size_t)d->R * d->R, npix = (size_t)w * h;
    struct rcd_tabdev tab;
    memset(&tab, 0, sizeof tab);
    if (n < 1 || w < 1 || h < 1 || d->R > RCD_RMAX)
    {
        snprintf(d->err, sizeof d->err, "%d envs at %dx%d, range %d (a ring of %d, at most %d)", n, w, h, range, d->R, RCD_RMAX);
        goto fail;
    }
    RC_TRY(cudaSetDevice(dev));
    if (dev_alloc(d, (void **)&d->d_heads, (size_t)n * sizeof *d->d_heads) ||
        dev_alloc(d, (void **)&d->d_cols, (size_t)n * RR * sizeof *d->d_cols) ||
        dev_alloc(d, (void **)&d->d_secs, (size_t)n * RR * 16 * sizeof *d->d_secs) ||
        dev_alloc(d, (void **)&d->d_stage, (size_t)n * d->cap) ||
        dev_alloc(d, (void **)&d->d_frames, (size_t)n * npix * (size_t)d->ch) ||
        dev_alloc(d, (void **)&d->d_seen_key, (size_t)n * RCD_SEEN * sizeof *d->d_seen_key) ||
        dev_alloc(d, (void **)&d->d_seen_n, (size_t)n * RCD_SEEN * sizeof *d->d_seen_n) ||
        dev_alloc(d, (void **)&d->d_seen_xyz, (size_t)n * npix * 3 * sizeof *d->d_seen_xyz) ||
        dev_alloc(d, (void **)&d->d_seen, (size_t)n * sizeof *d->d_seen) ||
        dev_alloc(d, (void **)&d->d_wrefs, (size_t)n * RC_STAGE_RECS * sizeof *d->d_wrefs) ||
        dev_alloc(d, (void **)&d->d_iota, (size_t)n * sizeof *d->d_iota))
    {
        snprintf(d->err, sizeof d->err, "cudaMalloc: %zu MB of device memory", d->bytes >> 20);
        goto fail;
    }
    RC_TRY(cudaMemset(d->d_cols, 0, (size_t)n * RR * sizeof *d->d_cols));
    RC_TRY(cudaMemset(d->d_frames, 0, (size_t)n * npix * (size_t)d->ch));
    {
        int *iota = (int *)malloc((size_t)n * sizeof *iota);
        if (iota == NULL)
        {
            snprintf(d->err, sizeof d->err, "out of memory");
            goto fail;
        }
        for (int i = 0; i < n; ++i) iota[i] = i;
        cudaError_t e = cudaMemcpy(d->d_iota, iota, (size_t)n * sizeof *iota, cudaMemcpyHostToDevice);
        free(iota);
        RC_TRY(e);
    }
    RC_TRY(cudaHostAlloc((void **)&d->heads, (size_t)n * sizeof *d->heads, cudaHostAllocMapped));
    RC_TRY(cudaHostAlloc((void **)&d->stage, (size_t)n * d->cap, cudaHostAllocMapped));
    RC_TRY(cudaHostAlloc((void **)&d->seen, (size_t)n * sizeof *d->seen, cudaHostAllocMapped));
    RC_TRY(cudaHostGetDevicePointer((void **)&d->heads_map, d->heads, 0));
    RC_TRY(cudaHostGetDevicePointer((void **)&d->stage_map, d->stage, 0));
    RC_TRY(cudaHostGetDevicePointer((void **)&d->seen_map, d->seen, 0));
    RC_TRY(cudaHostAlloc((void **)&d->wrefs_pin, (size_t)n * RC_STAGE_RECS * sizeof *d->wrefs_pin, cudaHostAllocDefault));
    memset(d->heads, 0, (size_t)n * sizeof *d->heads);
    memset(d->seen, 0, (size_t)n * sizeof *d->seen);
    for (int i = 0; i < RC_SLOTS; ++i)
    {
        struct rc_slot *L = &d->slot[i];
        RC_TRY(cudaHostAlloc((void **)&L->ids_pin, (size_t)n * sizeof *L->ids_pin, cudaHostAllocMapped));
        RC_TRY(cudaHostGetDevicePointer((void **)&L->ids_map, L->ids_pin, 0));
        RC_TRY(cudaHostAlloc((void **)&L->refs_pin, (size_t)n * RC_STAGE_RECS * sizeof *L->refs_pin, cudaHostAllocMapped));
        RC_TRY(cudaHostGetDevicePointer((void **)&L->refs_map, L->refs_pin, 0));
        if (dev_alloc(d, (void **)&L->d_ids, (size_t)n * sizeof *L->d_ids))
        {
            snprintf(d->err, sizeof d->err, "cudaMalloc: the launch lists");
            goto fail;
        }
        RC_TRY(cudaEventCreateWithFlags(&L->ev, cudaEventDisableTiming));
    }
    /* the mirror: each env's stage in the page-locked block */
    d->m.n = n, d->m.R = d->R;
    d->m.env = (struct rcm_env *)calloc((size_t)n, sizeof *d->m.env);
    if (d->m.env == NULL)
    {
        snprintf(d->err, sizeof d->err, "out of memory");
        goto fail;
    }
    for (int i = 0; i < n; ++i)
    {
        d->m.env[i].stage = d->stage + (size_t)i * d->cap;
        d->m.env[i].cap = d->cap;
    }
    d->m.flush = worker_flush;
    d->m.flush_ctx = d;
    /* the tables, once */
    {
        size_t nf = t->nface;
        if (dev_copy(d, (void **)&tab.tex, t->tex, (size_t)t->nspr * 256 * sizeof(uint32_t), 0) ||
            dev_copy(d, (void **)&tab.face[0], t->face[0], nf * sizeof(uint16_t), 1) ||
            dev_copy(d, (void **)&tab.face[1], t->face[1], nf * sizeof(uint16_t), 2) ||
            dev_copy(d, (void **)&tab.bounds, t->bounds, nf * sizeof(double), 3) ||
            dev_copy(d, (void **)&tab.kind, t->kind, 4096, 4) || dev_copy(d, (void **)&tab.tint, t->tint, 4096, 5) ||
            dev_copy(d, (void **)&tab.emit, t->emit, 4096, 6) || dev_copy(d, (void **)&tab.cull_same, t->cull_same, 4096, 7) ||
            dev_copy(d, (void **)&tab.grass, t->grass, 256 * 3 * sizeof(float), 8) ||
            dev_copy(d, (void **)&tab.foliage, t->foliage, 256 * 3 * sizeof(float), 9) ||
            dev_copy(d, (void **)&tab.water, t->water, 256 * 3 * sizeof(float), 10))
        {
            snprintf(d->err, sizeof d->err, "the tables' copy");
            goto fail;
        }
        memcpy(tab.end_sky, t->end_sky, sizeof tab.end_sky);
        tab.water_spr = t->water_spr, tab.lava_spr = t->lava_spr;
        memcpy(tab.dp_low, t->dp_low, sizeof tab.dp_low);
        memcpy(tab.dp_high, t->dp_high, sizeof tab.dp_high);
        d->tabd = tab, d->nspr = t->nspr, d->nface = nf;
        if (rc_tables_set(&tab))
        {
            snprintf(d->err, sizeof d->err, "the tables' symbol");
            goto fail;
        }
    }
    return d;
fail:
    snprintf(err, en, "rcdev: %s", d->err);
    rcdev_free(d);
    return NULL;
}

extern "C" void rcdev_free(struct rcdev *d)
{
    if (d == NULL) return;
    cudaSetDevice(d->dev);
    cudaDeviceSynchronize();
    void *dp[] = {d->d_heads, d->d_cols, d->d_secs, d->d_stage, d->d_frames, d->d_seen_key, d->d_seen_n, d->d_seen_xyz,
                  d->d_seen, d->d_wrefs, d->d_iota};
    for (size_t i = 0; i < sizeof dp / sizeof dp[0]; ++i) cudaFree(dp[i]);
    for (int i = 0; i < 12; ++i) cudaFree(d->tabs[i]);
    void *hp[] = {d->heads, d->stage, d->seen, d->wrefs_pin, d->hframes};
    for (size_t i = 0; i < sizeof hp / sizeof hp[0]; ++i) cudaFreeHost(hp[i]);
    for (int i = 0; i < RC_SLOTS; ++i)
    {
        struct rc_slot *L = &d->slot[i];
        cudaFreeHost(L->ids_pin);
        cudaFreeHost(L->refs_pin);
        cudaFree(L->d_ids);
        if (L->ev) cudaEventDestroy(L->ev);
    }
    free(d->m.env);
    free(d);
}

extern "C" struct rcd_head *rcdev_head(struct rcdev *d, int id) { return &d->heads[id]; }
extern "C" struct rcmirror *rcdev_mirror(struct rcdev *d) { return &d->m; }
extern "C" uint8_t *rcdev_frames(struct rcdev *d) { return d->d_frames; }
extern "C" const struct rcd_seen *rcdev_seen(const struct rcdev *d, int id) { return &d->seen[id]; }
extern "C" const char *rcdev_error(const struct rcdev *d) { return d->err; }
extern "C" size_t rcdev_bytes(const struct rcdev *d) { return d->bytes; }

extern "C" uint8_t *rcdev_host_frames(struct rcdev *d)
{
    size_t bytes = (size_t)d->n * d->w * d->h * (size_t)d->ch;
    if (d->hframes == NULL)
    {
        RC_TRY(cudaSetDevice(d->dev));
        RC_TRY(cudaHostAlloc((void **)&d->hframes, bytes, cudaHostAllocDefault));
        memset(d->hframes, 0, bytes);
    }
    return d->hframes;
fail:
    return NULL;
}

/* the next launch slot, its last use done */
static struct rc_slot *slot_take(struct rcdev *d)
{
    struct rc_slot *L = &d->slot[d->next];
    d->next = (d->next + 1) % RC_SLOTS;
    if (L->used && cudaEventSynchronize(L->ev) != cudaSuccess) return NULL;
    return L;
}

extern "C" int rcdev_put(struct rcdev *d, int lo, int k, void *stream)
{
    size_t px = (size_t)d->w * d->h * (size_t)d->ch;
    struct rc_slot *L;
    if (d->hframes == NULL || lo < 0 || k < 1 || lo + k > d->n)
    {
        snprintf(d->err, sizeof d->err, "rcdev_put: envs %d..%d of %d (host frames %s)", lo, lo + k - 1, d->n,
                 d->hframes ? "made" : "not made");
        return -1;
    }
    RC_TRY(cudaSetDevice(d->dev));
    L = slot_take(d);
    if (L == NULL)
    {
        snprintf(d->err, sizeof d->err, "rcdev_put: a slot's event");
        return -1;
    }
    RC_TRY(cudaMemcpyAsync(d->d_frames + (size_t)lo * px, d->hframes + (size_t)lo * px, (size_t)k * px,
                           cudaMemcpyHostToDevice, (cudaStream_t)stream));
    RC_TRY(cudaEventRecord(L->ev, (cudaStream_t)stream));
    L->used = 1;
    return (int)(L - d->slot);
fail:
    return -1;
}

/* each launched env's head into the device's copy, through its last entity box (the cast reads no further), read from
 * the workers' page-locked heads in place: one block an env. The launch's ids are read in place too (mapped) and
 * left in d_ids for the cast */
__global__ void rc_head_get(const struct rcd_head *src, struct rcd_head *dst, const int *ids, int *d_ids)
{
    const int env = ids[blockIdx.x];
    if (threadIdx.x == 0) d_ids[blockIdx.x] = env;
    const int4 *s = (const int4 *)(src + env);
    int4 *o = (int4 *)(dst + env);
    const int fixed = (int)((offsetof(struct rcd_head, ent) + 15) / 16);   /* nent's 16 bytes too */
    for (int i = threadIdx.x; i < fixed; i += blockDim.x) o[i] = s[i];
    __syncthreads();
    const int nent = dst[env].nent;
    const int n16 = (int)((offsetof(struct rcd_head, ent) + (size_t)nent * sizeof(struct rcd_ent) + 15) / 16);
    for (int i = fixed + threadIdx.x; i < n16; i += blockDim.x) o[i] = s[i];
}

extern "C" int rcdev_launch(struct rcdev *d, const int *ids, int k, void *stream)
{
    if (k <= 0) return 0;
    cudaStream_t s = (cudaStream_t)stream;
    struct rc_slot *L;
    int nref = 0;
    RC_TRY(cudaSetDevice(d->dev));
    L = slot_take(d);
    if (L == NULL)
    {
        snprintf(d->err, sizeof d->err, "rcdev_launch: a slot's event");
        return -1;
    }
    for (int i = 0; i < k; ++i)
    {
        int id = ids[i];
        L->ids_pin[i] = id;
        struct rcm_env *E = &d->m.env[id];
        if (E->nrec > 0)
        {
            /* its records where its worker staged them: the scatter reads them there */
            for (int r = 0; r < E->nrec; ++r) L->refs_pin[nref++] = make_int2(id, r);
            E->used = 0, E->nrec = 0;
        }
    }
    if (nref > 0) rc_launch_scatter(d->stage_map, d->cap, L->refs_map, nref, d->d_cols, d->d_secs, d->R, s);
    rc_head_get<<<k, 128, 0, s>>>(d->heads_map, d->d_heads, L->ids_map, L->d_ids);
    /* the instance: the launch's envs' (one spec: the first's head says it) */
    rc_launch_cast(d->d_heads, d->d_cols, d->d_secs, L->d_ids, k, d->R, d->w, d->h, d->ch, d->d_frames, d->d_seen_key, d->d_seen_n,
                   d->d_seen_xyz, d->seen_map, d->heads[ids[0]].fp, s);
    RC_TRY(cudaGetLastError());
    RC_TRY(cudaEventRecord(L->ev, s));
    L->used = 1;
    return (int)(L - d->slot);
fail:
    return -1;
}

extern "C" int rcdev_wait(struct rcdev *d, int ticket)
{
    if (ticket < 0 || ticket >= RC_SLOTS || !d->slot[ticket].used) return -1;
    return cudaEventSynchronize(d->slot[ticket].ev) == cudaSuccess ? 0 : -1;
}

extern "C" int rcdev_cast_now(struct rcdev *d, int id, uint8_t *rgb)
{
    cudaStream_t s = cudaStreamPerThread;
    struct rcm_env *E = &d->m.env[id];
    size_t px = (size_t)d->w * d->h * (size_t)d->ch;
    RC_TRY(cudaSetDevice(d->dev));
    if (E->nrec > 0)
    {
        int2 *refs = d->wrefs_pin + (size_t)id * RC_STAGE_RECS;
        for (int r = 0; r < E->nrec; ++r) refs[r] = make_int2(id, r);
        RC_TRY(cudaMemcpyAsync(d->d_stage + (size_t)id * d->cap, E->stage, E->used, cudaMemcpyHostToDevice, s));
        RC_TRY(cudaMemcpyAsync(d->d_wrefs + (size_t)id * RC_STAGE_RECS, refs, (size_t)E->nrec * sizeof *refs, cudaMemcpyHostToDevice, s));
        rc_launch_scatter(d->d_stage, d->cap, d->d_wrefs + (size_t)id * RC_STAGE_RECS, E->nrec, d->d_cols, d->d_secs, d->R, s);
        E->used = 0, E->nrec = 0;
    }
    RC_TRY(cudaMemcpyAsync(d->d_heads + id, d->heads + id, sizeof *d->heads, cudaMemcpyHostToDevice, s));
    rc_launch_cast(d->d_heads, d->d_cols, d->d_secs, d->d_iota + id, 1, d->R, d->w, d->h, d->ch, d->d_frames, d->d_seen_key, d->d_seen_n,
                   d->d_seen_xyz, d->d_seen, d->heads[id].fp, s);
    RC_TRY(cudaGetLastError());
    RC_TRY(cudaMemcpyAsync(d->seen + id, d->d_seen + id, sizeof *d->seen, cudaMemcpyDeviceToHost, s));
    RC_TRY(cudaMemcpyAsync(rgb, d->d_frames + (size_t)id * px, px, cudaMemcpyDeviceToHost, s));
    RC_TRY(cudaStreamSynchronize(s));
    return 0;
fail:
    return -1;
}

/* ------------------------------------------------- the kernel's bench input */

static int dump_dev(FILE *f, const void *dp, size_t n)
{
    void *b = malloc(n ? n : 1);
    int rc = b && cudaMemcpy(b, dp, n, cudaMemcpyDeviceToHost) == cudaSuccess && fwrite(b, 1, n, f) == n ? 0 : -1;
    free(b);
    return rc;
}

extern "C" int rcdev_dump(struct rcdev *d, const int *ids, int k, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) return -1;
    size_t RR = (size_t)d->R * d->R, px = (size_t)d->w * d->h * (size_t)d->ch, nf = d->nface;
    int32_t hd[6] = {k, d->w, d->h, d->R, d->nspr, (int32_t)nf};
    struct rcd_col *cols = (struct rcd_col *)malloc(RR * sizeof *cols);
    struct rcd_sec *sec = (struct rcd_sec *)malloc(sizeof *sec);
    const struct rcd_tabdev *t = &d->tabd;
    int rc = cols && sec && cudaDeviceSynchronize() == cudaSuccess && fwrite("RCDUMP1", 1, 8, f) == 8 &&
             fwrite(hd, sizeof hd, 1, f) == 1 && fwrite(t, sizeof *t, 1, f) == 1 ? 0 : -1;
    if (!rc)
        rc = dump_dev(f, t->tex, (size_t)d->nspr * 256 * sizeof(uint32_t)) || dump_dev(f, t->face[0], nf * 2) ||
             dump_dev(f, t->face[1], nf * 2) || dump_dev(f, t->bounds, nf * 8) || dump_dev(f, t->kind, 4096) ||
             dump_dev(f, t->tint, 4096) || dump_dev(f, t->emit, 4096) || dump_dev(f, t->cull_same, 4096) ||
             dump_dev(f, t->grass, 768 * 4) || dump_dev(f, t->foliage, 768 * 4) || dump_dev(f, t->water, 768 * 4);
    for (int i = 0; !rc && i < k; ++i)
        rc = dump_dev(f, d->d_heads + ids[i], sizeof *d->d_heads) || dump_dev(f, d->d_frames + (size_t)ids[i] * px, px) ||
             fwrite(d->seen + ids[i], sizeof *d->seen, 1, f) != 1;   /* a launch writes them to the host */
    /* each env's ring: its columns, then each section with storage: (column, section) and its bytes, (-1, -1) after */
    for (int i = 0; !rc && i < k; ++i)
    {
        rc = cudaMemcpy(cols, d->d_cols + (size_t)ids[i] * RR, RR * sizeof *cols, cudaMemcpyDeviceToHost) != cudaSuccess ||
             fwrite(cols, sizeof *cols, RR, f) != RR;
        for (size_t c = 0; !rc && c < RR; ++c)
            for (int s = 0; !rc && s < 16; ++s)
            {
                if (!cols[c].exists || !(cols[c].present >> s & 1)) continue;
                int32_t at[2] = {(int32_t)c, s};
                rc = cudaMemcpy(sec, d->d_secs + ((size_t)ids[i] * RR + c) * 16 + s, sizeof *sec, cudaMemcpyDeviceToHost) != cudaSuccess;
                if (!rc) rc_sec_order(sec, 0);   /* the dump keeps world.h's cell order */
                rc = rc || fwrite(at, sizeof at, 1, f) != 1 || fwrite(sec, sizeof *sec, 1, f) != 1;
            }
        int32_t end[2] = {-1, -1};
        if (!rc) rc = fwrite(end, sizeof end, 1, f) != 1;
    }
    free(cols);
    free(sec);
    return fclose(f) || rc ? -1 : 0;
}
