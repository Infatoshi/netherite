/* The device mesher's host side (meshing.h). */
#include "meshing.h"

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../engine/meshfeed.h"
#include "../../engine/render_blocks.h"
#include "../../engine/world.h"
#include "kernels.h"
#include "table.h"

/* the module, embedded by meshing.mk (xxd) */
extern const unsigned char meshing_cubin[];
extern const unsigned int meshing_cubin_len;

#define CHUNK_STRIDE ((sizeof(struct chunk) + 255) & ~(size_t)255)
#define BAND_STRIDE (((size_t)MF_BAND_BYTES + 255) & ~(size_t)255)
enum { CHUNKS_PER_BLOCK = 256, BANDS_PER_BLOCK = 128 };

/* a slot's chunk or band records: blocks of consecutive slots, each
 * allocation all a feed lacks and at least half what is held (a walking
 * world grows its pools in a few allocations, not one per 128 bands) */
struct pool {
    CUdeviceptr *blk;
    int *first;                     /* each block's first slot */
    int nblk, cap;                  /* blocks, slots held */
};

/* one slot's world: where its chunk and band slots live */
struct slotw {
    int live;
    uint32_t epoch, next_seq;
    struct pool cp, bp;             /* chunk and band slots */
};

struct meshing {
    cudaStream_t st;
    CUmodule mod;
    CUfunction k_setup, k_run, k_run_cube, k_copy, k_wcopy;
    /* the spread count pass (kernels.c mesh_class ...): lcap lanes, their
     * records; each request's masks and plan (res_cap of them) */
    CUfunction k_class, k_wsum, k_order, k_plan, k_cut, k_pack, k_place, k_keys, k_sort, k_draw, k_draw_cube, k_check, k_fix, k_vcopy;
    int lcap, dw_cube, dw_gen;       /* lanes; the drawing kernels' warps */
    CUdeviceptr d_lanes, d_masks, d_plan, d_ckey, d_perm, d_bands, d_cut;
    CUdeviceptr d_wcache;           /* the spread pass's world caches (lcap / 64 requests) */
    int max_slots;
    struct slotw *slot;
    /* the mesher's tables and its template, one block */
    CUdeviceptr d_ctx, d_mesher, d_trig, d_sentinel, d_zero, d_spec0;
    size_t ctx_bytes;
    int ntrig, have_ctx;
    struct rb_table tab_seen;
    int n_atlas_seen;
    /* a launch: staged on the host, one copy up */
    unsigned char *h_stage;
    size_t stage_cap, stage_n;
    CUdeviceptr d_stage;
    size_t d_stage_cap;
    struct meshing_copy *cp;
    size_t ncp, capcp;
    struct meshing_req *rq;
    size_t nrq, caprq;
    CUdeviceptr d_res, d_scratch, d_voff;
    size_t res_cap;                 /* requests d_res, d_voff and the per-request arrays hold; h_res twice that */
    /* the count pass's vertices (kernels.h meshing_launch vbuf): vcap of them,
     * vwant the size the last count pass asked for */
    CUdeviceptr d_vbuf, d_vcur;
    int64_t vcap, vwant;
    /* the count pass's lane buffers (kernels.h meshing_launch pool) */
    CUdeviceptr d_pool, d_slots;
    int nslot;
    uint64_t *h_vcur;
    struct meshing_count *h_res;
    cudaEvent_t ev[6];                /* 5: the count pass's results back */
    /* the cube lanes' stream (st's priority) and its fork and join with st:
     * mesh_draw_cube and mesh_draw run at once */
    cudaStream_t st2;
    struct mtab_host *tab;          /* the table mode (table.h), on when tab_on */
    int tab_on;
    cudaEvent_t fork, join;
    struct meshing_stats stats;
    size_t pool_bytes;
    int stage_moved;
    int wrote;                      /* the last launch ran a write pass (its events hold its time) */
    /* the requests whose section has no band: counted 0 without a thread */
    char *empty;
    size_t capempty;
    int32_t *idx;
    size_t capidx;
    /* the write pass's requests: those with vertices, and where each came from */
    struct meshing_req *wq;
    size_t capwq;
    size_t nk;                      /* the count pass's requests (meshing_count_start) */
};

static int ck(cudaError_t e, const char *what)
{
    if (e == cudaSuccess) return 0;
    fprintf(stderr, "meshing: %s: %s\n", what, cudaGetErrorString(e));
    return -1;
}

/* N bytes from the host to device memory, or zeroed, on the mesher's stream,
 * waited for. The kernels run on st, a non-blocking stream: a plain
 * cudaMemcpy or cudaMemset goes on the legacy stream, which st does not wait
 * for (and a pageable copy returns before its DMA lands), so a kernel could
 * read a table, the sentinel or the slot bitmap before they arrived (the
 * setup kernel's intermittent illegal address, 2026-09-29) */
static int put(struct meshing *m, void *dst, const void *src, size_t n, const char *what)
{
    return ck(src ? cudaMemcpyAsync(dst, src, n, cudaMemcpyHostToDevice, m->st) : cudaMemsetAsync(dst, 0, n, m->st), what) ||
           ck(cudaStreamSynchronize(m->st), what);
}

static int ckd(CUresult e, const char *what)
{
    if (e == CUDA_SUCCESS) return 0;
    const char *s = NULL;
    cuGetErrorString(e, &s);
    fprintf(stderr, "meshing: %s: %s\n", what, s ? s : "?");
    return -1;
}

static void *grow(void *p, size_t *cap, size_t need, size_t size)
{
    if (need <= *cap) return p;
    size_t c = *cap ? *cap : 64;
    while (c < need) c *= 2;
    p = realloc(p, c * size);
    if (!p) abort();
    *cap = c;
    return p;
}

/* N bytes of the launch's staging, 16-aligned: its offset */
static size_t stage(struct meshing *m, size_t n)
{
    size_t at = (m->stage_n + 15) & ~(size_t)15;
    if (at + n > m->stage_cap) {
        size_t cap = m->stage_cap ? m->stage_cap : (1 << 20);
        while (cap < at + n) cap *= 2;
        unsigned char *h = NULL;
        if (ck(cudaMallocHost((void **)&h, cap), "staging")) abort();
        if (m->h_stage) {
            memcpy(h, m->h_stage, m->stage_n);
            cudaFreeHost(m->h_stage);
        }
        m->h_stage = h;
        m->stage_cap = cap;
    }
    m->stage_n = at + n;
    return at;
}

struct meshing *meshing_new(void *stream, int max_slots, const char *cubin)
{
    struct meshing *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->st = (cudaStream_t)stream;
    m->max_slots = max_slots;
    m->slot = calloc((size_t)max_slots, sizeof *m->slot);
    cudaFree(0);   /* the runtime's context current on this thread */
    CUresult r;
    if (cubin) r = cuModuleLoad(&m->mod, cubin);
    else r = cuModuleLoadData(&m->mod, meshing_cubin);
    if (ckd(r, "the mesher's module") ||
        ckd(cuModuleGetFunction(&m->k_setup, m->mod, "mesh_setup"), "mesh_setup") ||
        ckd(cuModuleGetFunction(&m->k_run, m->mod, "mesh_run"), "mesh_run") ||
        ckd(cuModuleGetFunction(&m->k_run_cube, m->mod, "mesh_run_cube"), "mesh_run_cube") ||
        ckd(cuModuleGetFunction(&m->k_copy, m->mod, "mesh_copy"), "mesh_copy") ||
        ckd(cuModuleGetFunction(&m->k_wcopy, m->mod, "mesh_wcopy"), "mesh_wcopy") ||
        ckd(cuModuleGetFunction(&m->k_class, m->mod, "mesh_class"), "mesh_class") ||
        ckd(cuModuleGetFunction(&m->k_wsum, m->mod, "mesh_wsum"), "mesh_wsum") ||
        ckd(cuModuleGetFunction(&m->k_plan, m->mod, "mesh_plan"), "mesh_plan") ||
        ckd(cuModuleGetFunction(&m->k_order, m->mod, "mesh_order"), "mesh_order") ||
        ckd(cuModuleGetFunction(&m->k_cut, m->mod, "mesh_cut"), "mesh_cut") ||
        ckd(cuModuleGetFunction(&m->k_pack, m->mod, "mesh_pack"), "mesh_pack") ||
        ckd(cuModuleGetFunction(&m->k_place, m->mod, "mesh_place"), "mesh_place") ||
        ckd(cuModuleGetFunction(&m->k_keys, m->mod, "mesh_keys"), "mesh_keys") ||
        ckd(cuModuleGetFunction(&m->k_sort, m->mod, "mesh_sort"), "mesh_sort") ||
        ckd(cuModuleGetFunction(&m->k_draw, m->mod, "mesh_draw"), "mesh_draw") ||
        ckd(cuModuleGetFunction(&m->k_draw_cube, m->mod, "mesh_draw_cube"), "mesh_draw_cube") ||
        ckd(cuModuleGetFunction(&m->k_check, m->mod, "mesh_check"), "mesh_check") ||
        ckd(cuModuleGetFunction(&m->k_fix, m->mod, "mesh_fix"), "mesh_fix") ||
        ckd(cuModuleGetFunction(&m->k_vcopy, m->mod, "mesh_vcopy"), "mesh_vcopy")) {
        meshing_free(m);
        return NULL;
    }
    for (int i = 0; i < 6; ++i)
        if (ck(cudaEventCreateWithFlags(&m->ev[i], cudaEventBlockingSync), "event")) { meshing_free(m); return NULL; }
    m->tab = meshing_tab_new(m->mod, m->st);
    int prio = 0;
    if (ck(cudaStreamGetPriority(m->st, &prio), "stream priority") ||
        ck(cudaStreamCreateWithPriority(&m->st2, cudaStreamNonBlocking, prio), "cube stream") ||
        ck(cudaEventCreateWithFlags(&m->fork, cudaEventDisableTiming), "event") ||
        ck(cudaEventCreateWithFlags(&m->join, cudaEventDisableTiming), "event")) { meshing_free(m); return NULL; }
    /* the chunk outside the window: no bands, no sky (a height past the top) */
    struct chunk *out = calloc(1, sizeof *out);
    for (int i = 0; i < 256; ++i) out->height[i] = 256;
    void *p = NULL;
    if (ck(cudaMalloc(&p, CHUNK_STRIDE), "sentinel") ||
        put(m, p, out, sizeof *out, "sentinel")) { free(out); meshing_free(m); return NULL; }
    free(out);
    m->d_sentinel = (CUdeviceptr)p;
    p = NULL;
    if (ck(cudaMalloc(&p, 256), "zeros") || put(m, p, NULL, 256, "zeros")) { meshing_free(m); return NULL; }
    m->d_zero = (CUdeviceptr)p;
    p = NULL;
    if (ck(cudaMalloc(&p, sizeof(struct rb_mesher)), "scratch")) { meshing_free(m); return NULL; }
    m->d_scratch = (CUdeviceptr)p;
    int n = 0;
    struct rb_trig *t = meshfeed_static_trig(&n);
    p = NULL;
    if (ck(cudaMalloc(&p, (size_t)n * sizeof *t), "trig") ||
        put(m, p, t, (size_t)n * sizeof *t, "trig")) { free(t); meshing_free(m); return NULL; }
    free(t);
    m->d_trig = (CUdeviceptr)p;
    m->ntrig = n;
    m->pool_bytes = CHUNK_STRIDE + 256 + (size_t)n * sizeof *t;
    /* a lane buffer for every block that can be resident at once: mesh_run's
     * and mesh_run_cube's run together, and an SM holds no more of the two
     * than of the one with fewer registers alone */
    int nb = 0, nbc = 0, dev = 0, nsm = 0;
    CUdevice cd;
    if (ckd(cuOccupancyMaxActiveBlocksPerMultiprocessor(&nb, m->k_run, 32 * MESHING_WARPS, 0), "occupancy") ||
        ckd(cuOccupancyMaxActiveBlocksPerMultiprocessor(&nbc, m->k_run_cube, 32 * MESHING_WARPS, 0), "occupancy") ||
        ck(cudaGetDevice(&dev), "device") || ckd(cuDeviceGet(&cd, dev), "device") ||
        ckd(cuDeviceGetAttribute(&nsm, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, cd), "SMs")) { meshing_free(m); return NULL; }
    m->nslot = (nbc > nb ? nbc : nb) * nsm;
    size_t pool = (size_t)m->nslot * 32 * MESHING_WARPS * MESHING_PK * 32;
    p = NULL;
    void *sl = NULL;
    if (m->nslot > 0 && (ck(cudaMalloc(&p, pool), "lane buffers") || ck(cudaMalloc(&sl, (size_t)(m->nslot + 31) / 32 * 4), "slots") ||
                         put(m, sl, NULL, (size_t)(m->nslot + 31) / 32 * 4, "slots"))) { meshing_free(m); return NULL; }
    m->d_pool = (CUdeviceptr)p;
    m->d_slots = (CUdeviceptr)sl;
    m->pool_bytes += pool;
    /* the spread pass's lanes: MESHING_LANES_SM an SM (48,128 on the RTX PRO
     * 6000: 24k and 72k measured slower, 96k 9% slower: each lane's stack and
     * record then outgrow the L2; with the guesses only begun, 192 and 384 an
     * SM no faster), at most one a buffer of the pool. It was
     * half of mesh_draw_cube's resident threads in blocks of 128, which a
     * change of its registers from 168 to 176 halved (lane/meshgpu) */
    m->lcap = MESHING_LANES_SM * nsm;
    /* the drawing kernels' warps: as many as each holds resident on every
     * SM (a warp's lanes run their differing paths one after another, so
     * the lanes are shared out over all of them: kernels.c draw_lane; 11
     * and 8 an SM on the RTX PRO 6000, the chain of a launch 8% shorter than
     * 32 lanes a warp; half of that 5% longer) */
    int wc = 0, wg = 0;
    if (ckd(cuOccupancyMaxActiveBlocksPerMultiprocessor(&wc, m->k_draw_cube, 32, 0), "occupancy") ||
        ckd(cuOccupancyMaxActiveBlocksPerMultiprocessor(&wg, m->k_draw, 32, 0), "occupancy")) { meshing_free(m); return NULL; }
    m->dw_cube = (wc > 0 ? wc : 1) * nsm;
    m->dw_gen = (wg > 0 ? wg : 1) * nsm;
    if (m->lcap > m->nslot * 32 * MESHING_WARPS) m->lcap = m->nslot * 32 * MESHING_WARPS;
    p = NULL;
    if (m->lcap > 0 && ck(cudaMalloc(&p, (size_t)m->lcap * MESHING_LANE_WORDS * 4), "lane records")) { meshing_free(m); return NULL; }
    m->d_lanes = (CUdeviceptr)p;
    p = NULL;
    if (m->lcap > 0 && ck(cudaMalloc(&p, ((size_t)m->lcap + MESHING_NKEY) * 4), "lane order")) { meshing_free(m); return NULL; }
    m->d_perm = (CUdeviceptr)p;
    p = NULL;
    if (m->lcap > 0 && ck(cudaMalloc(&p, (size_t)m->lcap * 3 * 4), "lane cuts")) { meshing_free(m); return NULL; }
    m->d_cut = (CUdeviceptr)p;
    m->pool_bytes += ((size_t)m->lcap * MESHING_LANE_WORDS + (size_t)m->lcap * 4 + MESHING_NKEY) * 4;
    p = NULL;
    if (m->lcap > 0 && ck(cudaMalloc(&p, (size_t)(m->lcap / 64) * MESHING_WC_BYTES), "world caches")) { meshing_free(m); return NULL; }
    m->d_wcache = (CUdeviceptr)p;
    m->pool_bytes += (size_t)(m->lcap / 64) * MESHING_WC_BYTES;
    return m;
}

static void slot_release(struct slotw *s)
{
    struct pool *ps[2] = {&s->cp, &s->bp};
    for (int k = 0; k < 2; ++k) {
        for (int i = 0; i < ps[k]->nblk; ++i) cudaFree((void *)ps[k]->blk[i]);
        free(ps[k]->blk);
        free(ps[k]->first);
    }
    memset(s, 0, sizeof *s);
}

void meshing_free(struct meshing *m)
{
    if (!m) return;
    meshing_tab_free(m->tab);
    for (int i = 0; m->slot && i < m->max_slots; ++i) slot_release(&m->slot[i]);
    free(m->slot);
    CUdeviceptr d[] = {m->d_ctx, m->d_trig, m->d_sentinel, m->d_zero, m->d_stage, m->d_res, m->d_scratch, m->d_voff, m->d_vbuf, m->d_vcur,
                       m->d_pool, m->d_slots, m->d_lanes, m->d_masks, m->d_plan, m->d_ckey, m->d_perm, m->d_bands, m->d_cut, m->d_wcache};
    for (size_t i = 0; i < sizeof d / sizeof d[0]; ++i) if (d[i]) cudaFree((void *)d[i]);
    if (m->h_stage) cudaFreeHost(m->h_stage);
    if (m->h_res) cudaFreeHost(m->h_res);
    if (m->h_vcur) cudaFreeHost(m->h_vcur);
    for (int i = 0; i < 6; ++i) if (m->ev[i]) cudaEventDestroy(m->ev[i]);
    if (m->fork) cudaEventDestroy(m->fork);
    if (m->join) cudaEventDestroy(m->join);
    if (m->st2) cudaStreamDestroy(m->st2);
    if (m->mod) cuModuleUnload(m->mod);
    free(m->cp);
    free(m->rq);
    free(m->wq);
    free(m->empty);
    free(m->idx);
    free(m);
}

/* ------------------------------------------------------------ the tables */

struct blob { unsigned char *p; size_t n, cap; };

static size_t blob_put(struct blob *b, const void *src, size_t n)
{
    size_t at = (b->n + 15) & ~(size_t)15;
    if (at + n > b->cap) {
        size_t cap = b->cap ? b->cap : (1 << 20);
        while (cap < at + n) cap *= 2;
        b->p = realloc(b->p, cap);
        if (!b->p) abort();
        b->cap = cap;
    }
    if (src) memcpy(b->p + at, src, n);
    else memset(b->p + at, 0, n);
    b->n = at + n;
    return at;
}

/* an array of N strings: the pointer array's offset (its entries patched
 * to the strings' device addresses once the base is known) */
static size_t blob_strings(struct blob *b, char *const *s, int n, size_t **fix, size_t *nfix, size_t *capfix)
{
    size_t arr = blob_put(b, NULL, (size_t)(n ? n : 1) * sizeof(uint64_t));
    for (int i = 0; i < n; ++i) {
        size_t at = blob_put(b, s[i], strlen(s[i]) + 1);
        memcpy(b->p + arr + (size_t)i * 8, &at, 8);
        *fix = grow(*fix, capfix, *nfix + 1, sizeof **fix);
        (*fix)[(*nfix)++] = arr + (size_t)i * 8;
    }
    return arr;
}

int meshing_context(struct meshing *m, const struct rb_mesher *t)
{
    const struct rb_table *tab = t->tab;
    const struct rb_atlas *atlas = t->atlas;
    if (m->have_ctx)
        return m->tab_seen.fancy == tab->fancy && m->tab_seen.ao == tab->ao && m->tab_seen.n_icons == tab->n_icons &&
               m->tab_seen.n_special == tab->n_special && m->n_atlas_seen == atlas->n ? 0 : -1;
    struct blob b = {0};
    size_t *fix = NULL, nfix = 0, capfix = 0;   /* offsets of pointers holding blob offsets */
#define FIX(off) do { fix = grow(fix, &capfix, nfix + 1, sizeof *fix); fix[nfix++] = (off); } while (0)
    size_t o_tab = blob_put(&b, NULL, sizeof(struct rb_table));
    size_t o_atlas = blob_put(&b, NULL, sizeof(struct rb_atlas));
    size_t o_mesher = blob_put(&b, NULL, sizeof(struct rb_mesher));
    size_t o_icon_name = blob_strings(&b, tab->icon_name, tab->n_icons, &fix, &nfix, &capfix);
    size_t o_special = blob_strings(&b, tab->special, tab->n_special, &fix, &nfix, &capfix);
    size_t n_idx = (size_t)RB_IDS * RB_METAS * RB_SIDES;
    size_t o_icon_index = blob_put(&b, tab->icon_index, n_idx * sizeof *tab->icon_index);
    size_t o_grass = blob_put(&b, tab->grass_map, 65536 * sizeof *tab->grass_map);
    size_t o_foliage = blob_put(&b, tab->foliage_map, 65536 * sizeof *tab->foliage_map);
    size_t o_props = blob_put(&b, tab->props, (size_t)RB_IDS * sizeof *tab->props);
    size_t o_bounds = blob_put(&b, tab->bounds, (size_t)RB_IDS * RB_METAS * 6 * sizeof *tab->bounds);
    size_t o_biomes = blob_put(&b, tab->biomes, (size_t)RB_BIOMES * sizeof *tab->biomes);
    size_t o_sin = blob_put(&b, tab->sin_table, 65536 * sizeof *tab->sin_table);
    size_t o_aname = blob_strings(&b, atlas->name, atlas->n, &fix, &nfix, &capfix);
    size_t o_auv = blob_put(&b, atlas->uv, (size_t)(atlas->n ? atlas->n : 1) * sizeof *atlas->uv);
    size_t o_icon = blob_put(&b, t->icon, n_idx * sizeof *t->icon);
    size_t o_perlin_a = blob_put(&b, t->perlin_a, sizeof *t->perlin_a);
    size_t o_perlin_b = blob_put(&b, t->perlin_b, sizeof *t->perlin_b);
    /* the special icons, twice: the mesher's, and a copy the kernels hold
     * them against (a draw that wrote one would change every lane's) */
    size_t n_spec = (size_t)(RB_SPECIALS + RB2_SPECIALS + RB3_SPECIALS) * sizeof *t->special;
    size_t o_spec = blob_put(&b, t->special, n_spec);
    size_t o_spec0 = blob_put(&b, t->special, n_spec);

    struct rb_table *dt = (struct rb_table *)(b.p + o_tab);
    *dt = *tab;
#define SETP(field, off) do { uint64_t v_ = (off); memcpy(&(field), &v_, 8); FIX((size_t)((unsigned char *)&(field) - b.p)); } while (0)
    SETP(dt->icon_name, o_icon_name);
    SETP(dt->special, o_special);
    SETP(dt->icon_index, o_icon_index);
    SETP(dt->grass_map, o_grass);
    SETP(dt->foliage_map, o_foliage);
    SETP(dt->props, o_props);
    SETP(dt->bounds, o_bounds);
    SETP(dt->biomes, o_biomes);
    SETP(dt->sin_table, o_sin);
    struct rb_atlas *da = (struct rb_atlas *)(b.p + o_atlas);
    *da = *atlas;
    SETP(da->name, o_aname);
    SETP(da->uv, o_auv);
    struct rb_mesher *dm = (struct rb_mesher *)(b.p + o_mesher);
    *dm = *t;
    SETP(dm->tab, o_tab);
    SETP(dm->atlas, o_atlas);
    SETP(dm->icon, o_icon);
    SETP(dm->perlin_a, o_perlin_a);
    SETP(dm->perlin_b, o_perlin_b);
    SETP(dm->special, o_spec);
    SETP(dm->special2, o_spec + (size_t)RB_SPECIALS * sizeof *t->special);
    SETP(dm->special3, o_spec + (size_t)(RB_SPECIALS + RB2_SPECIALS) * sizeof *t->special);
    dm->w = NULL;
    dm->t = NULL;
    dm->override = NULL;
    dm->dev = NULL;
    dm->px = dm->py = dm->pz = 0;
#undef SETP
#undef FIX
    void *p = NULL;
    if (ck(cudaMalloc(&p, b.n), "the mesher's tables")) { free(b.p); free(fix); return -1; }
    uint64_t base = (uint64_t)(uintptr_t)p;
    for (size_t i = 0; i < nfix; ++i) {
        uint64_t v;
        memcpy(&v, b.p + fix[i], 8);
        v += base;
        memcpy(b.p + fix[i], &v, 8);
    }
    int rc = put(m, p, b.p, b.n, "the mesher's tables");
    free(b.p);
    free(fix);
    if (rc) { cudaFree(p); return -1; }
    m->d_ctx = (CUdeviceptr)p;
    m->ctx_bytes = b.n;
    m->d_mesher = m->d_ctx + o_mesher;
    m->d_spec0 = m->d_ctx + o_spec0;
    m->tab_seen = *tab;
    m->n_atlas_seen = atlas->n;
    m->have_ctx = 1;
    /* the block class table, by one thread */
    struct meshing_launch L = {0};
    L.mesher = m->d_mesher;
    L.scratch = m->d_scratch;
    L.res = m->d_zero;
    void *dl = NULL;
    if (ck(cudaMalloc(&dl, sizeof L), "setup") || put(m, dl, &L, sizeof L, "setup"))
        return -1;
    CUdeviceptr a = (CUdeviceptr)dl;
    void *args[] = {&a};
    rc = ckd(cuLaunchKernel(m->k_setup, 1, 1, 1, 1, 1, 1, 0, (CUstream)m->st, args, NULL), "setup") ||
         ck(cudaStreamSynchronize(m->st), "setup");
    cudaFree(dl);
    if (!rc && m->tab && meshing_tab_context(m->tab, t)) rc = -1;
    return rc ? -1 : 0;
}

/* ------------------------------------------------------------ slots */

void meshing_forget(struct meshing *m, int slot)
{
    if (slot < 0 || slot >= m->max_slots) return;
    m->slot[slot].live = 0;
}

static int pool_grow(struct meshing *m, struct pool *p, int need, int per, size_t stride)
{
    if (need <= p->cap) return 0;
    int n = need - p->cap;
    if (n < p->cap / 2) n = p->cap / 2;
    n = (n + per - 1) / per * per;
    void *d = NULL;
    if (ck(cudaMalloc(&d, (size_t)n * stride), "a slot's pool")) return -1;
    /* a band slot reads zero until a feed writes it (meshfeed.h) */
    if (ck(cudaMemsetAsync(d, 0, (size_t)n * stride, m->st), "a slot's pool")) { cudaFree(d); return -1; }
    p->blk = realloc(p->blk, (size_t)(p->nblk + 1) * sizeof *p->blk);
    p->first = realloc(p->first, (size_t)(p->nblk + 1) * sizeof *p->first);
    if (!p->blk || !p->first) abort();
    p->blk[p->nblk] = (CUdeviceptr)d;
    p->first[p->nblk++] = p->cap;
    p->cap += n;
    m->pool_bytes += (size_t)n * stride;
    return 0;
}

static uint64_t pool_addr(const struct pool *p, int i, size_t stride)
{
    int b = p->nblk - 1;
    while (b > 0 && p->first[b] > i) --b;
    return p->blk[b] + (uint64_t)(i - p->first[b]) * stride;
}

static uint64_t chunk_addr(const struct slotw *s, int i)
{
    return pool_addr(&s->cp, i, CHUNK_STRIDE);
}

static uint64_t band_addr(const struct slotw *s, int i)
{
    return pool_addr(&s->bp, i, BAND_STRIDE);
}

void meshing_begin(struct meshing *m)
{
    /* the last launch's copies are done (its render waited for the device) */
    cudaEventSynchronize(m->ev[4]);
    float t = 0;
    double prev = m->wrote && cudaEventElapsedTime(&t, m->ev[3], m->ev[4]) == cudaSuccess ? t : 0;
    m->stage_n = 0;
    m->ncp = m->nrq = 0;
    m->wrote = 0;
    m->nk = 0;
    memset(&m->stats, 0, sizeof m->stats);
    m->stats.prev_write_ms = prev;
}

int meshing_add(struct meshing *m, int slot, const struct meshfeed_out *f)
{
    if (slot < 0 || slot >= m->max_slots || !m->have_ctx) return -1;
    struct slotw *s = &m->slot[slot];
    if (f->seq == 0) {
        /* a new epoch: its band slots read zero */
        s->live = 1;
        s->epoch = f->epoch;
        for (int b = 0; b < s->bp.nblk; ++b) {
            int end = b + 1 < s->bp.nblk ? s->bp.first[b + 1] : s->bp.cap;
            if (ck(cudaMemsetAsync((void *)s->bp.blk[b], 0, (size_t)(end - s->bp.first[b]) * BAND_STRIDE, m->st), "epoch")) return -1;
        }
    }
    else if (!s->live || f->epoch != s->epoch || f->seq != s->next_seq) return -1;
    s->next_seq = f->seq + 1;
    if (pool_grow(m, &s->cp, f->chunk_slots, CHUNKS_PER_BLOCK, CHUNK_STRIDE) ||
        pool_grow(m, &s->bp, f->band_slots, BANDS_PER_BLOCK, BAND_STRIDE)) {
        s->live = 0;
        return -1;
    }
    /* the payload, its pointers made the device's */
    size_t pay = stage(m, f->npayload);
    memcpy(m->h_stage + pay, f->payload, f->npayload);
    m->cp = grow(m->cp, &m->capcp, m->ncp + (size_t)f->nop, sizeof *m->cp);
    for (int i = 0; i < f->nop; ++i) {
        const struct meshfeed_op *op = &f->op[i];
        if (op->kind == MF_SKIP) continue;
        unsigned char *img = m->h_stage + pay + op->off;
        struct meshing_copy *c = &m->cp[m->ncp++];
        c->src = pay + op->off;
        if (op->kind == MF_BAND_RUN) {
            /* a run of the band's image; the one from byte 0 carries its header */
            uint64_t base = band_addr(s, op->slot);
            if (op->band[0] == 0) {
                struct chunk_sec *sec = (struct chunk_sec *)img;
                uint64_t hi = op->hi ? base + sizeof(struct chunk_sec_flat) : 0;
                memcpy(&sec->ids_hi, &hi, sizeof hi);
                sec->shared = 0;
            }
            c->dst = base + (uint64_t)op->band[0];
            c->len = (uint64_t)op->band[1];
            m->stats.band_bytes += (size_t)op->band[1];
            m->stats.band_run_bytes += (size_t)op->band[1];
        }
        else if (op->kind == MF_BAND) {
            struct chunk_sec *sec = (struct chunk_sec *)img;
            uint64_t dst = band_addr(s, op->slot);
            uint64_t hi = op->hi ? dst + sizeof(struct chunk_sec_flat) : 0;
            memcpy(&sec->ids_hi, &hi, sizeof hi);
            sec->shared = 0;
            c->dst = dst;
            c->len = MF_BAND_BYTES;
            m->stats.band_bytes += MF_BAND_BYTES;
        }
        else {
            struct chunk *ch = (struct chunk *)img;
            uint64_t dst = chunk_addr(s, op->slot);
            for (int k = 0; k < 16; ++k)
                ch->band[k] = op->band[k] >= 0 ? (bandoff)(band_addr(s, op->band[k]) - dst) : 0;
            c->dst = dst;
            c->len = sizeof(struct chunk);
            m->stats.chunk_bytes += sizeof(struct chunk);
        }
    }
    m->stats.list_bytes += (size_t)f->nte * sizeof *f->te + (size_t)f->ntrig * sizeof *f->trig;
    size_t te = stage(m, (size_t)(f->nte ? f->nte : 1) * sizeof *f->te);
    if (f->nte) memcpy(m->h_stage + te, f->te, (size_t)f->nte * sizeof *f->te);
    size_t tr = stage(m, (size_t)(f->ntrig ? f->ntrig : 1) * sizeof *f->trig);
    if (f->ntrig) memcpy(m->h_stage + tr, f->trig, (size_t)f->ntrig * sizeof *f->trig);
    int first = (int)m->nrq;
    m->rq = grow(m->rq, &m->caprq, m->nrq + (size_t)f->nreq, sizeof *m->rq);
    m->empty = grow(m->empty, &m->capempty, m->nrq + (size_t)f->nreq, sizeof *m->empty);
    for (int i = 0; i < f->nreq; ++i) {
        const struct meshfeed_req *q = &f->req[i];
        m->empty[m->nrq] = (char)q->empty;
        struct meshing_req *d = &m->rq[m->nrq++];
        memset(d, 0, sizeof *d);
        d->cx = q->cx; d->s = q->s; d->cz = q->cz; d->pass = q->pass;
        d->px = q->px; d->py = q->py; d->pz = q->pz; d->no_sky = q->no_sky;
        for (int k = 0; k < 9; ++k)
            d->chunk[k] = q->chunk[k] == MF_NULL ? 0 : q->chunk[k] == MF_OUTSIDE ? m->d_sentinel : chunk_addr(s, q->chunk[k]);
        /* the staging's device address is known at the copy: offsets for now */
        d->te = te + (size_t)q->te_first * sizeof *f->te;
        d->trig = tr + (size_t)q->trig_first * sizeof *f->trig;
        d->te_n = q->te_n;
        d->ntrig = q->trig_n;
    }
    m->stats.requests += (size_t)f->nreq;
    m->stats.ops += (size_t)f->nop;
    return first;
}

/* the staging up, the copies and the requests' pass (count, or write; a
 * count pass SPREAD over the launch's lanes: kernels.c mesh_class) over a
 * frame set of REQUESTS: the device's per-request arrays hold that many
 * (a launch indexes them by request, below it), h_res twice that (the
 * counts land past the requests' own: meshing_count_wait). They were
 * sized at twice the requests too, the cell keys alone 8 KB a request
 * (lane/heapprof: a 256-env reset's burst of whole views to mesh) */
static int run(struct meshing *m, int write, size_t requests, int spread, int redraw)
{
    size_t nrq = m->nrq;
    size_t rq = stage(m, (nrq ? nrq : 1) * sizeof *m->rq);
    size_t cp = stage(m, (m->ncp ? m->ncp : 1) * sizeof *m->cp);
    size_t la = stage(m, sizeof(struct meshing_launch));
    if (m->stage_n > m->d_stage_cap) {
        if (m->d_stage) cudaFree((void *)m->d_stage);
        m->d_stage_cap = m->stage_n + m->stage_n / 2;
        void *p = NULL;
        if (ck(cudaMalloc(&p, m->d_stage_cap), "staging")) { m->d_stage = 0; m->d_stage_cap = 0; return -1; }
        m->d_stage = (CUdeviceptr)p;
        m->stage_moved = 1;
    }
    if (requests > m->res_cap) {
        if (m->d_res) cudaFree((void *)m->d_res);
        if (m->h_res) cudaFreeHost(m->h_res);
        if (m->d_voff) cudaFree((void *)m->d_voff);
        if (m->d_masks) cudaFree((void *)m->d_masks);
        if (m->d_plan) cudaFree((void *)m->d_plan);
        if (m->d_ckey) cudaFree((void *)m->d_ckey);
        if (m->d_bands) cudaFree((void *)m->d_bands);
        m->res_cap = requests + requests / 2 + 64;
        void *p = NULL, *v = NULL, *mk = NULL, *pl = NULL, *ky = NULL, *bt = NULL;
        if (ck(cudaMalloc(&p, m->res_cap * sizeof(struct meshing_res)), "results") ||
            ck(cudaMallocHost((void **)&m->h_res, 2 * m->res_cap * sizeof *m->h_res), "results") ||
            ck(cudaMalloc(&v, m->res_cap * sizeof(int64_t)), "vertex offsets") ||
            ck(cudaMalloc(&mk, m->res_cap * MESHING_MASK_WORDS * 4), "masks") || ck(cudaMalloc(&pl, (3 * m->res_cap + 1) * 4), "plan") ||
            ck(cudaMalloc(&ky, m->res_cap * 4096), "cell keys") || ck(cudaMalloc(&bt, m->res_cap * 27 * sizeof(struct rb_band)), "band tables"))
            return -1;
        m->d_ckey = (CUdeviceptr)ky;
        m->d_bands = (CUdeviceptr)bt;
        m->d_res = (CUdeviceptr)p;
        m->d_voff = (CUdeviceptr)v;
        m->d_masks = (CUdeviceptr)mk;
        m->d_plan = (CUdeviceptr)pl;
    }
    /* the count pass's vertex buffer, as large as the last one asked (the
     * last launch's write pass is done: meshing_begin) */
    if (!write && (m->vwant > m->vcap || !m->d_vbuf)) {
        if (m->d_vbuf) cudaFree((void *)m->d_vbuf);
        m->vcap = m->vwant > m->vcap ? m->vwant + m->vwant / 4 : (int64_t)1 << 20;
        void *p = NULL;
        if (ck(cudaMalloc(&p, (size_t)m->vcap * 32), "vertex buffer")) { m->d_vbuf = 0; m->vcap = 0; return -1; }
        m->d_vbuf = (CUdeviceptr)p;
    }
    if (!m->d_vcur) {
        void *p = NULL;
        if (ck(cudaMalloc(&p, 8), "vertex cursor") || ck(cudaMallocHost((void **)&m->h_vcur, 8), "vertex cursor")) return -1;
        m->d_vcur = (CUdeviceptr)p;
    }
    struct meshing_req *q = (struct meshing_req *)(m->h_stage + rq);
    for (size_t i = 0; i < nrq; ++i) {
        q[i] = m->rq[i];
        q[i].te += m->d_stage;
        q[i].trig += m->d_stage;
    }
    struct meshing_copy *c = (struct meshing_copy *)(m->h_stage + cp);
    if (!write) memcpy(c, m->cp, m->ncp * sizeof *c);
    struct meshing_launch *L = (struct meshing_launch *)(m->h_stage + la);
    memset(L, 0, sizeof *L);
    L->req = m->d_stage + rq;
    L->res = m->d_res;
    L->mesher = m->d_mesher;
    L->spec0 = m->d_spec0;
    L->trig = m->d_trig;
    L->ntrig = m->ntrig;
    L->scratch = m->d_scratch;
    L->vbuf = m->d_vbuf;
    L->vcur = m->d_vcur;
    L->voff = m->d_voff;
    L->vcap = m->vcap;
    L->pool = m->d_pool;
    L->slots = m->d_slots;
    L->nslot = m->nslot;
    L->n = (int32_t)nrq;
    L->masks = m->d_masks;
    L->plan = m->d_plan;
    L->lanes = m->d_lanes;
    L->ckey = m->d_ckey;
    L->perm = m->d_perm;
    L->cut = m->d_cut;

    L->bands = m->d_bands;
    L->lcap = m->lcap;
    /* the table mode: any count pass (it has no lanes to spread) */
    int tab = !write && m->tab_on && meshing_tab_prepare(m->tab, L, nrq);
    L->ordered = !write && !spread && !tab;
    L->wcache = !write && spread && !tab ? m->d_wcache : 0;
    /* the write pass sends only what changed: the requests and the launch
     * (all of it when the device's staging moved) */
    size_t from = write && !m->stage_moved ? rq : 0;
    m->stage_moved = 0;
    cudaEventRecord(m->ev[0], m->st);
    if (!write && ck(cudaMemsetAsync((void *)m->d_vcur, 0, 8, m->st), "vertex cursor")) return -1;
    if (ck(cudaMemcpyAsync((void *)(m->d_stage + from), m->h_stage + from, m->stage_n - from, cudaMemcpyHostToDevice, m->st), "upload"))
        return -1;
    m->stats.upload_bytes += m->stage_n - from;
    m->stats.req_bytes += nrq * sizeof *m->rq;
    m->stats.ctl_bytes += (write ? 0 : m->ncp * sizeof *m->cp) + sizeof(struct meshing_launch);
    if (!write && m->ncp) {
        CUdeviceptr ops = m->d_stage + cp, staged = m->d_stage;
        int n = (int)m->ncp;
        void *args[] = {&ops, &staged, &n};
        if (ckd(cuLaunchKernel(m->k_copy, (unsigned)m->ncp, 1, 1, 256, 1, 1, 0, (CUstream)m->st, args, NULL), "copy")) return -1;
    }
    cudaEventRecord(m->ev[write ? 3 : 1], m->st);
    if (nrq && tab) {
        /* the table mode (table.h): the table's kernels, mesh_run for the
         * requests it cannot mesh */
        CUdeviceptr a = m->d_stage + la;
        void *args[] = {&a};
        if (meshing_tab_count(m->tab, m->st, args, nrq, m->k_run, m->k_run_cube)) return -1;
    }
    else if (nrq && spread) {
        CUdeviceptr a = m->d_stage + la;
        void *args[] = {&a};
        unsigned nt = 32 * MESHING_WARPS;
        if (ckd(cuLaunchKernel(m->k_class, (unsigned)nrq, 4096 / MESHING_CLASS_THREADS, 1, MESHING_CLASS_THREADS, 1, 1, 0, (CUstream)m->st, args, NULL),
                "classes") ||
            ckd(cuLaunchKernel(m->k_wsum, (unsigned)(nrq + 7) / 8, 1, 1, 256, 1, 1, 0, (CUstream)m->st, args, NULL), "weights") ||
            ckd(cuLaunchKernel(m->k_plan, 1, 1, 1, 1024, 1, 1, 0, (CUstream)m->st, args, NULL), "plan") ||
            ck(cudaMemsetAsync((void *)(m->d_perm + (size_t)m->lcap * 4), 0, MESHING_NKEY * 4, m->st), "key counts") ||
            ckd(cuLaunchKernel(m->k_cut, (unsigned)m->lcap / nt, 1, 1, nt, 1, 1, 0, (CUstream)m->st, args, NULL), "cut") ||
            ckd(cuLaunchKernel(m->k_pack, 1, 1, 1, 1024, 1, 1, 0, (CUstream)m->st, args, NULL), "pack") ||
            ckd(cuLaunchKernel(m->k_place, (unsigned)m->lcap / nt, 1, 1, nt, 1, 1, 0, (CUstream)m->st, args, NULL), "place") ||
            ckd(cuLaunchKernel(m->k_keys, 1, 1, 1, 1024, 1, 1, 0, (CUstream)m->st, args, NULL), "keys") ||
            ckd(cuLaunchKernel(m->k_sort, (unsigned)m->lcap / nt, 1, 1, nt, 1, 1, 0, (CUstream)m->st, args, NULL), "sort") ||
            /* the general kernel first, the chain's longer: launched second it
             * waited ~23 us for the cube kernel's warps to leave it room */
            ck(cudaEventRecord(m->fork, m->st), "fork") ||
            ckd(cuLaunchKernel(m->k_draw, (unsigned)m->dw_gen, 1, 1, 32, 1, 1, 0, (CUstream)m->st, args, NULL), "draw") ||
            ck(cudaStreamWaitEvent(m->st2, m->fork, 0), "fork") ||
            ckd(cuLaunchKernel(m->k_draw_cube, (unsigned)m->dw_cube, 1, 1, 32, 1, 1, 0, (CUstream)m->st2, args, NULL),
                "draw cubes") ||
            ck(cudaEventRecord(m->join, m->st2), "join") ||
            ck(cudaStreamWaitEvent(m->st, m->join, 0), "join") ||
            ckd(cuLaunchKernel(m->k_check, (unsigned)m->lcap / MESHING_WARPS, 1, 1, nt, 1, 1, 0, (CUstream)m->st, args, NULL), "check") ||
            ckd(cuLaunchKernel(m->k_fix, (unsigned)nrq, 1, 1, 32 * MESHING_FIX_WARPS, 1, 1, 0, (CUstream)m->st, args, NULL), "fix") ||
            ckd(cuLaunchKernel(m->k_vcopy, (unsigned)m->lcap / MESHING_WARPS, 1, 1, nt, 1, 1, 0, (CUstream)m->st, args, NULL), "vertex copy"))
            return -1;
    }
    else if (nrq) {
        CUdeviceptr a = m->d_stage + la;
        void *args[] = {&a};
        /* a block of MESHING_WARPS warps a request, the cube sections' blocks
         * on the second stream at once; a count launch's requests classified
         * and ordered first (mesh_order) */
        if (!write && (ckd(cuLaunchKernel(m->k_class, (unsigned)nrq, 4096 / MESHING_CLASS_THREADS, 1, MESHING_CLASS_THREADS, 1, 1, 0, (CUstream)m->st, args,
                                          NULL), "classes") ||
                       ckd(cuLaunchKernel(m->k_wsum, (unsigned)(nrq + 7) / 8, 1, 1, 256, 1, 1, 0, (CUstream)m->st, args, NULL), "weights") ||
                       ckd(cuLaunchKernel(m->k_order, 1, 1, 1, 1024, 1, 1, 0, (CUstream)m->st, args, NULL), "order")))
            return -1;
        /* the write pass: the requests the count pass wrote copied, the
         * others (REDRAW) drawn again by the kernels below */
        if (write && ckd(cuLaunchKernel(m->k_wcopy, (unsigned)nrq, 1, 1, 1024, 1, 1, 0, (CUstream)m->st, args, NULL), "vertex copy"))
            return -1;
        if (redraw && (ck(cudaEventRecord(m->fork, m->st), "fork") || ck(cudaStreamWaitEvent(m->st2, m->fork, 0), "fork") ||
            ckd(cuLaunchKernel(m->k_run_cube, (unsigned)nrq, 1, 1, 32 * MESHING_WARPS, 1, 1, 0, (CUstream)m->st2, args, NULL), "mesh cubes") ||
            ck(cudaEventRecord(m->join, m->st2), "join") ||
            ckd(cuLaunchKernel(m->k_run, (unsigned)nrq, 1, 1, 32 * MESHING_WARPS, 1, 1, 0, (CUstream)m->st, args, NULL), "mesh") ||
            ck(cudaStreamWaitEvent(m->st, m->join, 0), "join")))
            return -1;
    }
    cudaEventRecord(m->ev[write ? 4 : 2], m->st);
    return 0;
}

static int by_cap(const void *a, const void *b)
{
    const struct meshing_req *x = a, *y = b;
    return x->cap > y->cap ? -1 : x->cap < y->cap;
}

int meshing_count_start(struct meshing *m)
{
    m->nk = 0;
    if (!m->nrq) return 0;
    /* the requests to run (the empty sections out), in the launch's order */
    size_t k = 0, all = m->nrq;
    m->wq = grow(m->wq, &m->capwq, all, sizeof *m->wq);
    m->idx = grow(m->idx, &m->capidx, all, sizeof *m->idx);
    for (size_t i = 0; i < all; ++i) {
        if (m->empty[i]) continue;
        m->wq[k] = m->rq[i];
        m->wq[k].out = 0;
        m->wq[k].cap = 0;
        m->wq[k].key = (int32_t)i;
        m->idx[k++] = (int32_t)i;
    }
    struct meshing_req *keep = m->rq;
    m->rq = m->wq;
    m->nrq = k;
    /* a launch of few requests (64 lanes a request or more) spread over the
     * device's lanes; one of many (whole views meshed again) a block a
     * request: there each lane's run would pass its buffer, and mesh_run's
     * blocks keep every SM busy (the spread pass took the gate's first
     * launch of 1493 requests from 8.8 to 16 ms) */
    int rc = run(m, 0, all, m->lcap > 0 && k > 0 && (int64_t)k * 64 <= m->lcap, 1);
    m->rq = keep;
    m->nrq = all;
    if (rc) return -1;
    if (k && ck(cudaMemcpyAsync(m->h_res + all, (void *)m->d_res, k * sizeof *m->h_res, cudaMemcpyDeviceToHost, m->st), "counts"))
        return -1;
    if (k && ck(cudaMemcpyAsync(m->h_vcur, (void *)m->d_vcur, 8, cudaMemcpyDeviceToHost, m->st), "vertex cursor")) return -1;
    if (ck(cudaEventRecord(m->ev[5], m->st), "counts")) return -1;
    m->nk = k;
    return 0;
}

int meshing_count_wait(struct meshing *m, const struct meshing_count **counts, int *n)
{
    *counts = NULL;
    *n = (int)m->nrq;
    if (!m->nrq) return 0;
    size_t k = m->nk, all = m->nrq;
    if (ck(cudaEventSynchronize(m->ev[5]), "counts")) return -1;
    /* the vertices the pass wanted to keep: the next pass's buffer holds them */
    if (k && (int64_t)*m->h_vcur > m->vcap) m->vwant = (int64_t)*m->h_vcur;
    memset(m->h_res, 0, all * sizeof *m->h_res);
    for (size_t j = 0; j < k; ++j) m->h_res[m->idx[j]] = m->h_res[all + j];
    float t = 0;
    cudaEventElapsedTime(&t, m->ev[1], m->ev[2]);
    m->stats.count_ms = t;
    m->stats.write_ms = 0;
    *counts = m->h_res;
    return 0;
}

int meshing_count(struct meshing *m, const struct meshing_count **counts, int *n)
{
    *counts = NULL;
    *n = (int)m->nrq;
    if (meshing_count_start(m)) return -1;
    return meshing_count_wait(m, counts, n);
}

int meshing_write(struct meshing *m, int32_t *const *out)
{
    if (!m->nrq) return 0;
    /* only the requests with vertices, the largest first (a warp's threads
     * then mesh sections of about one size) */
    size_t k = 0;
    int redraw = 0;
    m->wq = grow(m->wq, &m->capwq, m->nrq, sizeof *m->wq);
    for (size_t i = 0; i < m->nrq; ++i) {
        if (!out[i] || m->h_res[i].count <= 0) continue;
        if (m->h_res[i].pad & MESHING_RES_REDRAW) redraw = 1;
        m->wq[k] = m->rq[i];
        m->wq[k].out = (uint64_t)(uintptr_t)out[i];
        m->wq[k].cap = m->h_res[i].count;
        m->wq[k].key = (int32_t)i;
        ++k;
    }
    qsort(m->wq, k, sizeof *m->wq, by_cap);
    struct meshing_req *keep = m->rq;
    size_t nkeep = m->nrq;
    m->rq = m->wq;
    m->nrq = k;
    int rc = k ? run(m, 1, nkeep, 0, redraw) : 0;
    m->wrote = k && !rc;
    m->rq = keep;
    m->nrq = nkeep;
    return rc;
}

void meshing_set_tab(struct meshing *m, int on)
{
    m->tab_on = on;
}

size_t meshing_device_bytes(const struct meshing *m)
{
    return m->ctx_bytes + m->pool_bytes + m->d_stage_cap + m->res_cap * (sizeof(struct meshing_res) + sizeof(int64_t)) +
           (size_t)m->vcap * 32;
}

const struct meshing_stats *meshing_stats(struct meshing *m)
{
    /* the write pass's time, once the stream has passed it */
    float t = 0;
    if (m->wrote && cudaEventQuery(m->ev[4]) == cudaSuccess && cudaEventElapsedTime(&t, m->ev[3], m->ev[4]) == cudaSuccess)
        m->stats.write_ms = t;
    return &m->stats;
}
