/* The table mode's host side (table.h): the class table, made from the C
 * renderer, the launch's buffers and the table's kernels. */
#include "table.h"

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../engine/blocks.h"
#include "../../engine/render_blocks_int.h"
#include "kernels.h"

struct mtab_host {
    CUfunction k_cache, k_list, k_draw, k_draw_box, k_draw_door, k_draw_fluid, k_draw_gen, k_place, k_copy;
    /* the class draws' streams besides the mesher's */
    cudaStream_t st2, st3, st4, st5;
    cudaEvent_t listed, join, join2, join3, join4, join5;
    int nsm;
    /* the class table, then each door's upper icon (door_upper_uv's) */
    uint8_t cls[RB_IDS * RB_METAS];
    struct rb_uv up[RB_IDS];
    int32_t icons[RB_IDS * RB_METAS * RB_SIDES];
    CUdeviceptr d_cls;
    int have, ncube, ngen;
    /* per request (ncap of them): the cells' id and meta, the table's and
     * the lanes' cell words, the flags and the table's vertices */
    size_t ncap;
    CUdeviceptr d_idm, d_tcell, d_tflag, d_ttot, d_tlist, d_tlcnt, d_cache, d_bcache, d_bands, d_toff;
    /* the table cells' vertices: tvcap of them, tvwant what the last pass
     * took (the next buffer holds them) */
    CUdeviceptr d_tvbuf, d_tvcur;
    int64_t tvcap, tvwant;
    uint64_t *h_tvcur;
    int pending;
};

static int ck(cudaError_t e, const char *what)
{
    if (e == cudaSuccess) return 0;
    fprintf(stderr, "meshing table: %s: %s\n", what, cudaGetErrorString(e));
    return -1;
}

static int ckd(CUresult e, const char *what)
{
    if (e == CUDA_SUCCESS) return 0;
    const char *s = NULL;
    cuGetErrorString(e, &s);
    fprintf(stderr, "meshing table: %s: %s\n", what, s ? s : "?");
    return -1;
}

struct mtab_host *meshing_tab_new(void *mod, void *st)
{
    struct mtab_host *T = calloc(1, sizeof *T);
    if (!T) return NULL;
    int prio = 0;
    int dev = 0;
    if (ckd(cuModuleGetFunction(&T->k_cache, (CUmodule)mod, "mesh_tab_cache"), "mesh_tab_cache") ||
        ckd(cuModuleGetFunction(&T->k_list, (CUmodule)mod, "mesh_tab_list"), "mesh_tab_list") ||
        ckd(cuModuleGetFunction(&T->k_draw, (CUmodule)mod, "mesh_tab_draw"), "mesh_tab_draw") ||
        ckd(cuModuleGetFunction(&T->k_draw_fluid, (CUmodule)mod, "mesh_tab_draw_fluid"), "mesh_tab_draw_fluid") ||
        ckd(cuModuleGetFunction(&T->k_draw_box, (CUmodule)mod, "mesh_tab_draw_box"), "mesh_tab_draw_box") ||
        ckd(cuModuleGetFunction(&T->k_draw_door, (CUmodule)mod, "mesh_tab_draw_door"), "mesh_tab_draw_door") ||
        ckd(cuModuleGetFunction(&T->k_draw_gen, (CUmodule)mod, "mesh_tab_draw_gen"), "mesh_tab_draw_gen") ||
        ck(cudaGetDevice(&dev), "device") || ck(cudaDeviceGetAttribute(&T->nsm, cudaDevAttrMultiProcessorCount, dev), "SMs") ||
        ckd(cuModuleGetFunction(&T->k_place, (CUmodule)mod, "mesh_tab_place"), "mesh_tab_place") ||
        ckd(cuModuleGetFunction(&T->k_copy, (CUmodule)mod, "mesh_tab_copy"), "mesh_tab_copy") ||
        ck(cudaStreamGetPriority((cudaStream_t)st, &prio), "stream priority") ||
        ck(cudaStreamCreateWithPriority(&T->st2, cudaStreamNonBlocking, prio), "stream") ||
        ck(cudaStreamCreateWithPriority(&T->st3, cudaStreamNonBlocking, prio), "stream") ||
        ck(cudaStreamCreateWithPriority(&T->st4, cudaStreamNonBlocking, prio), "stream") ||
        ck(cudaStreamCreateWithPriority(&T->st5, cudaStreamNonBlocking, prio), "stream") ||
        ck(cudaEventCreateWithFlags(&T->join4, cudaEventDisableTiming), "event") ||
        ck(cudaEventCreateWithFlags(&T->join5, cudaEventDisableTiming), "event") ||
        ck(cudaEventCreateWithFlags(&T->listed, cudaEventDisableTiming), "event") ||
        ck(cudaEventCreateWithFlags(&T->join, cudaEventDisableTiming), "event") ||
        ck(cudaEventCreateWithFlags(&T->join2, cudaEventDisableTiming), "event") ||
        ck(cudaEventCreateWithFlags(&T->join3, cudaEventDisableTiming), "event") ||
        ck(cudaMallocHost((void **)&T->h_tvcur, 8), "vertex cursor")) {
        meshing_tab_free(T);
        return NULL;
    }
    return T;
}

void meshing_tab_free(struct mtab_host *T)
{
    if (!T) return;
    CUdeviceptr d[] = {T->d_cls, T->d_idm, T->d_tcell, T->d_tflag, T->d_ttot, T->d_tvbuf, T->d_tvcur, T->d_tlist, T->d_tlcnt, T->d_cache, T->d_bcache, T->d_bands, T->d_toff};
    for (size_t i = 0; i < sizeof d / sizeof d[0]; ++i) if (d[i]) cudaFree((void *)d[i]);
    if (T->h_tvcur) cudaFreeHost(T->h_tvcur);
    cudaEvent_t e[] = {T->listed, T->join, T->join2, T->join3, T->join4, T->join5};
    for (size_t i = 0; i < sizeof e / sizeof e[0]; ++i) if (e[i]) cudaEventDestroy(e[i]);
    cudaStream_t q[] = {T->st2, T->st3, T->st4, T->st5};
    for (size_t i = 0; i < sizeof q / sizeof q[0]; ++i) if (q[i]) cudaStreamDestroy(q[i]);
    free(T);
}

/* ------------------------------------------------------------ the class table */

/* the render types render_block dispatches (render_blocks.c); another draws
 * nothing and says so on stderr */
static int dispatched(int rt)
{
    return (rt >= 0 && rt <= 21) || (rt >= 22 && rt <= 35) || (rt >= 37 && rt <= 41) || rt == 255;
}

static uint32_t xs(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* the test world: three by three probe records (chunks.bin's layout)
 * around chunk (0, 0), every section present; the cell at (8, 70, 8) and
 * its 5 x 5 x 5 neighbourhood random */
enum { VX = 8, VY = 70, VZ = 8, VTRIES = 24 };

static uint8_t *vrecord(uint8_t *data, int dx, int dz)
{
    return data + ((size_t)(dx + 1) * 3 + (size_t)(dz + 1)) * RB_CHUNK_RECORD + 8;
}

static void vfill(uint8_t *data, const int *ids, int nids, int id, int meta, uint32_t *rng)
{
    uint8_t *c = vrecord(data, 0, 0);
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -2; dz <= 2; ++dz) {
                int x = VX + dx, y = VY + dy, z = VZ + dz;
                size_t i = (size_t)(((x & 15) << 12) | ((z & 15) << 8) | y);
                uint32_t r = xs(rng) % 10;
                int b = r < 5 ? 0 : r < 7 ? id : ids[xs(rng) % (uint32_t)nids];
                int mt = (int)(xs(rng) & 15);
                if (!dx && !dy && !dz) b = id, mt = meta;
                c[2 * i] = (uint8_t)(b & 255);
                c[2 * i + 1] = (uint8_t)(b >> 8);
                c[RB_CHUNK_META + i] = (uint8_t)mt;
                c[RB_CHUNK_SKY + i] = (uint8_t)(xs(rng) & 15);
                c[RB_CHUNK_BLOCK + i] = (uint8_t)(xs(rng) & 15);
            }
    for (int col = 0; col < 256; ++col) c[RB_CHUNK_BIOMES + col] = (uint8_t)(xs(rng) % 40);
}

/* (ID, META)'s class: every draw of it in VTRIES random worlds passes
 * mtab_draw_check (the device holds each draw to the same checks); a
 * standard block's (MTAB_CUBE, a face a thread) only when its faces drawn
 * apart (mtab_cube_face) are the draw's vertices, byte for byte, and its
 * flags texture, colour and brightness */
static int vclass(const struct rb_mesher *tp, uint8_t *data, const int *ids, int nids, int id, int meta, int32_t *v, int32_t *v2,
                  const struct rb_uv *up, const int32_t *icons)
{
    int rt = BLOCKS[id].render_type, faces = rt == 0 || rt == 31 || rt == 11 || rt == 10 || rt == 7 || rt == 4;
    uint32_t rng = 0x9e3779b9u ^ (uint32_t)(id * RB_METAS + meta) * 2654435761u;
    struct rb_world w;
    memset(&w, 0, sizeof w);
    w.data = data;
    w.stride = RB_CHUNK_RECORD;
    w.margin = 1;
    w.rows = 3;
    for (int k = 0; k < VTRIES; ++k) {
        vfill(data, ids, nids, id, meta, &rng);
        struct rb_mesher m = *tp;
        struct rb_tess t;
        memset(&t, 0, sizeof t);
        m.w = &w;
        m.t = &t;
        m.dev = NULL;
        m.px = m.py = m.pz = -1000000;
        rb_mesh_section_begin(&m, 0, 0, VY >> 4);
        int fl = 0;
        int n = mtab_draw_check(&m, &t, tp, id, meta, VX, VY, VZ, v, &fl, 0, 1);
        if (fl & MTAB_REDO << 4) return MTAB_NO;
        if (faces)
        {
            /* the faces apart, from the begin */
            struct rb_mesher m2 = *tp;
            struct rb_tess t2;
            memset(&t2, 0, sizeof t2);
            m2.w = &w;
            m2.t = &t2;
            m2.dev = NULL;
            m2.px = m2.py = m2.pz = -1000000;
            rb_mesh_section_begin(&m2, 0, 0, VY >> 4);
            t2.raw = v2;
            t2.cap = MTAB_MAXV * 8;
            mtab_begin(&m2, id, meta, VX, VY, VZ);
            float r, g, b;
            mtab_cube_color(&m2, id, VX, VY, VZ, &r, &g, &b);
            struct mtab_box bx[MTAB_BOXES];
            int nb = rt == 0 ? 1 : rt == 7 || rt == 4 ? 0 : mtab_boxes(&m2, id, meta, VX, VY, VZ, bx);
            if (rt == 7 && mtab_door_drawn(&m2, id, VX, VY, VZ))
                for (int f = 0; f < 6; ++f) mtab_door_face(&m2, id, VX, VY, VZ, f, up);
            if (rt == 4)
                for (int f = 0; f < 6; ++f)
                    if (mtab_fluid_shown(&m2, id, VX, VY, VZ, f)) mtab_fluid_face(&m2, id, VX, VY, VZ, f);
            for (int k = 0; k < nb; ++k)
            {
                if (rt != 0) mtab_box_set(&m2, &bx[k]);
                for (int f = 0; f < 6; ++f)
                    if (mtab_cube_face_shown(&m2, id, VX, VY, VZ, f)) mtab_cube_face(&m2, id, meta, VX, VY, VZ, f, r, g, b, icons);
            }
            int same = t2.vertex_count == n && !t2.dropped && (fl & 15) == (n ? 7 : 0);
            for (int j = 0; same && j < n * 8; ++j) same = j % 8 == 6 || v[j] == v2[j];
            faces = same;
        }
    }
    if (rt == 0) return faces ? MTAB_CUBE : MTAB_GEN;
    if (faces) return rt == 7 ? MTAB_DOOR : rt == 4 ? MTAB_FLUID : MTAB_BOX;
    return MTAB_GEN;
}

int meshing_tab_context(struct mtab_host *T, const struct rb_mesher *t)
{
    if (!T || T->have) return 0;
    memset(T->cls, 0, sizeof T->cls);
    T->ncube = T->ngen = 0;
    /* hidden cubes left out and a table cell's state only its begin's hold
     * without smooth lighting alone */
    if (t->tab->ao != 0) return 0;
    struct rb_mesher tp = *t;
    tp.w = NULL;
    tp.t = NULL;
    tp.override = NULL;
    tp.dev = NULL;
    int *ids = malloc(RB_IDS * sizeof *ids), nids = 0;
    uint8_t *data = calloc(9, RB_CHUNK_RECORD);
    int32_t *v = malloc(MTAB_MAXV * 8 * sizeof *v), *v2 = malloc(MTAB_MAXV * 8 * sizeof *v2);
    if (!ids || !data || !v || !v2) { free(ids); free(data); free(v); free(v2); return -1; }
    for (int id = 1; id < RB_IDS; ++id)
        if (BLOCKS[id].name && dispatched(BLOCKS[id].render_type)) ids[nids++] = id;
    /* each (id, meta, face)'s icon code where the world does not decide it
     * (block_icon_world_index: a grass side by the block above, the anvil's
     * and the piston's by their state) */
    for (int id = 0; id < RB_IDS; ++id)
        for (int mt = 0; mt < RB_METAS; ++mt)
            for (int f = 0; f < RB_SIDES; ++f)
            {
                int32_t k = MTAB_ICON_WORLD;
                const char *cn = BLOCKS[id].class_name;
                if (id == 2 && cn && strcmp(cn, "BlockGrass") == 0) k = f == 1 ? table_icon_index(&tp, 2, mt, 1) : f == 0 ? table_icon_index(&tp, 3, 0, 0) : k;
                else if (cn && (strcmp(cn, "BlockAnvil") == 0 || strcmp(cn, "BlockPistonBase") == 0)) k = MTAB_ICON_WORLD;
                else if (BLOCKS[id].name) k = table_icon_index(&tp, id, mt, f);
                T->icons[(id * RB_METAS + mt) * RB_SIDES + f] = k;
            }
    /* each door's upper icon, door_upper_uv's (its lower icon's name with
     * _upper for _lower, found in the atlas) */
    memset(T->up, 0, sizeof T->up);
    for (int id = 1; id < RB_IDS; ++id)
    {
        if (!BLOCKS[id].name || BLOCKS[id].render_type != 7) continue;
        const char *lower = icon_name(tp.tab, tp.tab->icon_index[(id * RB_METAS + 0) * RB_SIDES + 0]);
        char buf[64];
        snprintf(buf, sizeof buf, "%s", lower);
        size_t n = strlen(buf);
        if (n > 6 && strcmp(buf + n - 6, "_lower") == 0) strcpy(buf + n - 6, "_upper");
        const struct rb_uv *uv = rb_atlas_by_name(&tp, buf);
        if (uv) T->up[id] = *uv;
    }
    for (int k = 0; k < 9; ++k) {
        uint8_t *c = vrecord(data, k / 3 - 1, k % 3 - 1);
        c[RB_CHUNK_BYTES - 2] = 0xff;
        c[RB_CHUNK_BYTES - 1] = 0xff;
        for (int col = 0; col < 256; ++col) {
            int32_t cached = -999;
            memcpy(c + RB_CHUNK_HEIGHT + 1024 + col * 4, &cached, 4);
        }
    }
    for (int i = 0; i < nids; ++i)
        for (int meta = 0; meta < RB_METAS; ++meta) {
            int c = vclass(&tp, data, ids, nids, ids[i], meta, v, v2, T->up, T->icons);
            T->cls[ids[i] * RB_METAS + meta] = (uint8_t)c;
            T->ncube += c == MTAB_CUBE;
            T->ngen += c != MTAB_NO && c != MTAB_CUBE;
        }
    free(ids);
    free(data);
    free(v);
    free(v2);
    void *p = NULL;
    if (ck(cudaMalloc(&p, sizeof T->cls + sizeof T->up + sizeof T->icons), "class table") ||
        ck(cudaMemcpy(p, T->cls, sizeof T->cls, cudaMemcpyHostToDevice), "class table") ||
        ck(cudaMemcpy((char *)p + sizeof T->cls, T->up, sizeof T->up, cudaMemcpyHostToDevice), "door icons") ||
        ck(cudaMemcpy((char *)p + sizeof T->cls + sizeof T->up, T->icons, sizeof T->icons, cudaMemcpyHostToDevice), "icons")) {
        if (p) cudaFree(p);
        return -1;
    }
    T->d_cls = (CUdeviceptr)p;
    T->have = 1;
    return 0;
}

void meshing_tab_counts(const struct mtab_host *T, int *cube, int *gen)
{
    *cube = T ? T->ncube : 0;
    *gen = T ? T->ngen : 0;
}

/* ------------------------------------------------------------ launches */

static int regrow(CUdeviceptr *d, size_t bytes, const char *what)
{
    if (*d) cudaFree((void *)*d);
    *d = 0;
    void *p = NULL;
    if (ck(cudaMalloc(&p, bytes), what)) return -1;
    *d = (CUdeviceptr)p;
    return 0;
}

int meshing_tab_prepare(struct mtab_host *T, struct meshing_launch *L, size_t n)
{
    L->tab = L->tidm = L->tcell = L->tvbuf = L->tvcur = L->tflag = L->ttot = L->tlist = L->tlcnt = L->tcache = L->tbcache = L->tbands = L->toff = 0;
    L->tvcap = 0;
    if (!T || !T->have || !n) return 0;
    if (n > T->ncap) {
        size_t cap = n + n / 2 + 64;
        if (regrow(&T->d_idm, cap * 4096 * 2, "cell ids") || regrow(&T->d_tcell, cap * 4096 * 8, "table cells") ||
            regrow(&T->d_tflag, cap * 4, "table flags") ||
            regrow(&T->d_ttot, cap * 4, "table counts") || regrow(&T->d_tlist, cap * 4096 * 4 * MTAB_CLASSES, "table lists") ||
            regrow(&T->d_cache, cap * RB_CACHE * RB_CACHE * RB_CACHE * 4, "world caches") ||
            regrow(&T->d_bcache, cap * RB_CACHE * RB_CACHE, "biome caches") || regrow(&T->d_bands, cap * 27 * sizeof(struct rb_band), "band tables") ||
            regrow(&T->d_toff, cap * 4096 * 4, "cell places")) {
            T->ncap = 0;
            return 0;
        }
        T->ncap = cap;
    }
    /* the last pass's vertices (it is done: its counts were waited for) */
    if (T->pending && (int64_t)*T->h_tvcur > T->tvwant) T->tvwant = (int64_t)*T->h_tvcur;
    T->pending = 0;
    if (!T->d_tvbuf || T->tvwant > T->tvcap) {
        int64_t cap = T->tvwant > T->tvcap ? T->tvwant + T->tvwant / 4 : (int64_t)1 << 20;
        if (regrow(&T->d_tvbuf, (size_t)cap * 32, "table vertices")) { T->tvcap = 0; return 0; }
        T->tvcap = cap;
    }
    if (!T->d_tvcur && regrow(&T->d_tvcur, 8, "table cursor")) return 0;
    if (!T->d_tlcnt && regrow(&T->d_tlcnt, MTAB_CLASSES * 4, "table list counts")) return 0;
    L->tab = T->d_cls;
    L->tidm = T->d_idm;
    L->tcell = T->d_tcell;
    L->tvbuf = T->d_tvbuf;
    L->tvcur = T->d_tvcur;
    L->tvcap = T->tvcap;
    L->tflag = T->d_tflag;
    L->ttot = T->d_ttot;
    L->tlist = T->d_tlist;
    L->tlcnt = T->d_tlcnt;
    L->tcache = T->d_cache;
    L->tbcache = T->d_bcache;
    L->tbands = T->d_bands;
    L->toff = T->d_toff;
    return 1;
}

int meshing_tab_count(struct mtab_host *T, void *st, void **args, size_t n, void *run, void *run_cube)
{
    /* on ST and the second stream at once: the world caches, the lists
     * (the table cells; a request with a drawn cell the table cannot draw
     * marked MTAB_REDO); then the classes' draws on their streams (the device's threads, a few blocks an SM,
     * strided over each list), then the places, the vertices' copies, and
     * mesh_run for the marked requests */
    cudaStream_t s = (cudaStream_t)st;
    unsigned dblocks = (unsigned)(T->nsm > 0 ? T->nsm * 8 : 256), dt = MTAB_DT, nt = 32 * MESHING_WARPS;
    cudaStream_t ds[5] = {s, T->st2, T->st3, T->st4, T->st5};
    CUfunction dk[5] = {T->k_draw, T->k_draw_fluid, T->k_draw_box, T->k_draw_gen, T->k_draw_door};   /* the longest first */
    cudaEvent_t dj[5] = {T->join, T->join2, T->join3, T->join4, T->join5};
    if (ck(cudaMemsetAsync((void *)T->d_tflag, 0, n * 4, s), "table flags") || ck(cudaMemsetAsync((void *)T->d_ttot, 0, n * 4, s), "table counts") ||
        ck(cudaMemsetAsync((void *)T->d_tvcur, 0, 8, s), "table cursor") ||
        ck(cudaMemsetAsync((void *)T->d_tlcnt, 0, MTAB_CLASSES * 4, s), "table list counts") ||
        ck(cudaEventRecord(T->listed, s), "zeros") || ck(cudaStreamWaitEvent(T->st2, T->listed, 0), "zeros") ||
        ckd(cuLaunchKernel(T->k_list, (unsigned)n, 4096 / MTAB_THREADS, 1, MTAB_THREADS, 1, 1, 0, (CUstream)T->st2, args, NULL), "table lists") ||
        ckd(cuLaunchKernel(T->k_cache, (unsigned)n, (RB_CACHE * RB_CACHE * RB_CACHE + RB_CACHE * RB_CACHE + 255) / 256, 1, 256, 1, 1, 0,
                           (CUstream)s, args, NULL), "world caches") ||
        ck(cudaEventRecord(T->join2, T->st2), "lists") || ck(cudaStreamWaitEvent(s, T->join2, 0), "lists") ||
        ck(cudaEventRecord(T->listed, s), "lists"))
        return -1;
    for (int k = 1; k < 5; ++k)
        if (ck(cudaStreamWaitEvent(ds[k], T->listed, 0), "lists")) return -1;
    for (int k = 0; k < 5; ++k)
        if (ckd(cuLaunchKernel(dk[k], dblocks, 1, 1, dt, 1, 1, 0, (CUstream)ds[k], args, NULL), "table draws") ||
            ck(cudaEventRecord(dj[k], ds[k]), "join"))
            return -1;
    for (int k = 1; k < 5; ++k)
        if (ck(cudaStreamWaitEvent(s, dj[k], 0), "join")) return -1;
    if (ckd(cuLaunchKernel(T->k_place, (unsigned)n, 1, 1, MTAB_THREADS, 1, 1, 0, (CUstream)s, args, NULL), "table place") ||
        ckd(cuLaunchKernel(T->k_copy, (unsigned)n, 4096 / MTAB_THREADS, 1, MTAB_THREADS, 1, 1, 0, (CUstream)s, args, NULL), "table copy") ||
        ckd(cuLaunchKernel((CUfunction)run_cube, (unsigned)n, 1, 1, nt, 1, 1, 0, (CUstream)s, args, NULL), "table redo cubes") ||
        ckd(cuLaunchKernel((CUfunction)run, (unsigned)n, 1, 1, nt, 1, 1, 0, (CUstream)s, args, NULL), "table redo") ||
        ck(cudaMemcpyAsync(T->h_tvcur, (void *)T->d_tvcur, 8, cudaMemcpyDeviceToHost, s), "table cursor"))
        return -1;
    T->pending = 1;
    return 0;
}
