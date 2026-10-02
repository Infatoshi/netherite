/* The table mesher's device side (table.h): its kernels, mesh_tab_cache to
 * mesh_tab_copy. Linked with kernels.c and render_blocks*.c into the
 * mesher's cubin (table.mk). */
#include <stddef.h>
#include <stdint.h>

#include "../../engine/render_blocks.h"
#include "../../engine/render_blocks_int.h"
#include "../../engine/blocks.h"
#include "../../engine/world.h"
#include "kernels.h"
#include "table.h"

typedef uint32_t v4u __attribute__((ext_vector_type(4)));

/* a drawn cell's table class (MTAB_NO: the table cannot draw it, the
 * request is counted again the exact way). The camera's cell, which
 * rb_mesh_cell draws twice (from outside and inside), is drawn whole
 * (MTAB_GEN, its end state held like any) */
static int tab_class(const struct meshing_launch *L, int id, int meta, int x, int y, int z, int px, int py, int pz)
{
    int k = ((const uint8_t *)L->tab)[id * RB_METAS + (meta & 15)];
    return k && x == px && y == py && z == pz ? MTAB_GEN : k;
}

/* ------------------------------------------------------------ mesh_tab_draw */

static void copy8(void *restrict d, const void *restrict s, size_t n)
{
    size_t i = 0;
    for (; i + 64 <= n; i += 64)
    {
        uint64_t v[8];
        for (int j = 0; j < 8; ++j) __builtin_memcpy(&v[j], __builtin_assume_aligned((const char *)s + i + 8 * j, 8), 8);
        for (int j = 0; j < 8; ++j) __builtin_memcpy(__builtin_assume_aligned((char *)d + i + 8 * j, 8), &v[j], 8);
    }
    for (; i < n; i += 8)
    {
        uint64_t v;
        __builtin_memcpy(&v, __builtin_assume_aligned((const char *)s + i, 8), 8);
        __builtin_memcpy(__builtin_assume_aligned((char *)d + i, 8), &v, 8);
    }
}

/* request Q's world over its band table */
static void world_raw(struct rb_world *w, struct chunk **near, const struct meshing_req *q, const struct rb_band *bands)
{
    for (int k = 0; k < 9; ++k) near[k] = (struct chunk *)q->chunk[k];
    *w = (struct rb_world){0};
    w->bands = bands;
    w->band_s0 = q->s - 1;
    w->chunks = near;
    w->margin = 1;
    w->rows = 3;
    w->origin_cx = q->cx;
    w->origin_cz = q->cz;
    w->te = (struct rb_te *)q->te;
    w->te_n = q->te_n;
    w->no_sky = q->no_sky;
    w->band_x0 = (q->cx - 1) * 16;
    w->band_z0 = (q->cz - 1) * 16;
    w->band_y0 = (q->s - 1) * 16;
}

enum { CC = RB_CACHE * RB_CACHE * RB_CACHE, CB = RB_CACHE * RB_CACHE };

/* request S's world with its cache */
static void world_at(struct rb_world *w, struct chunk **near, const struct meshing_launch *L, int s)
{
    const struct meshing_req *q = (const struct meshing_req *)L->req + s;
    world_raw(w, near, q, (const struct rb_band *)L->tbands + (size_t)s * 27);
    w->cache = (const uint32_t *)L->tcache + (size_t)s * CC;
    w->bcache = (const uint8_t *)L->tbcache + (size_t)s * CB;
    w->cache_x0 = (q->cx << 4) - 2;
    w->cache_y0 = (q->s << 4) - 2;
    w->cache_z0 = (q->cz << 4) - 2;
}

/* request Q's band table entry I (kernels.c band_put, which mesh_class
 * runs beside this file's first kernel) */
static void band_of_req(struct rb_band *t, const struct meshing_req *q, int i)
{
    const struct chunk *k = (const struct chunk *)q->chunk[i / 3];
    int s = q->s - 1 + i % 3;
    struct rb_band b = {NULL, 0, 0};
    if (k && s >= 0 && s < 16)
    {
        b.kind = k->mask >> s & 1 ? 1 : 2;
        b.band = chunk_sec_at(k, s);
    }
    else if (k) b.kind = 2;
    t[i] = b;
}

static __attribute__((address_space(3))) struct rb_band mtab_bt[27];

/* a block a request (and a part of it: blockIdx.y), a thread a cache entry:
 * the accessors' answers around the request's section, its columns' biomes
 * (beside mesh_class: the block its own band table, the first part's to
 * tbands for the table's other kernels) */
__attribute__((nvptx_kernel)) void mesh_tab_cache(const struct meshing_launch *L)
{
    int s = (int)__nvvm_read_ptx_sreg_ctaid_x(), g = (int)__nvvm_read_ptx_sreg_tid_x();
    int i = (int)(__nvvm_read_ptx_sreg_ctaid_y() * __nvvm_read_ptx_sreg_ntid_x()) + g;
    if (s >= L->n) return;
    const struct meshing_req *q = (const struct meshing_req *)L->req + s;
    struct rb_band *bt = (struct rb_band *)mtab_bt;
    if (g < 27)
    {
        band_of_req(bt, q, g);
        if (__nvvm_read_ptx_sreg_ctaid_y() == 0) ((struct rb_band *)L->tbands)[(size_t)s * 27 + g] = bt[g];
    }
    __nvvm_barrier_sync(0);
    if (i >= CC + CB) return;
    struct rb_world w;
    struct chunk *near[9];
    world_raw(&w, near, q, bt);
    int x0 = (q->cx << 4) - 2, y0 = (q->s << 4) - 2, z0 = (q->cz << 4) - 2;
    if (i < CC)
    {
        int dx = i / CB, dz = i / RB_CACHE % RB_CACHE, dy = i % RB_CACHE, x = x0 + dx, y = y0 + dy, z = z0 + dz;
        uint32_t e = (uint32_t)rb_world_block(&w, x, y, z) | (uint32_t)rb_world_meta(&w, x, y, z) << 12 |
                     (uint32_t)rb_world_sky(&w, x, y, z) << 16 | (uint32_t)rb_world_blocklight(&w, x, y, z) << 20;
        ((uint32_t *)L->tcache)[(size_t)s * CC + i] = e;
    }
    else
    {
        int j = i - CC;
        ((uint8_t *)L->tbcache)[(size_t)s * CB + j] = (uint8_t)rb_world_biome(&w, x0 + j / RB_CACHE, z0 + j % RB_CACHE);
    }
}

/* mesh_tab_list's block: its request's world (cached) and a mesher to
 * classify with (rb_mesh_cell_kind only reads it) */
struct listblk {
    struct rb_mesher m;
    struct rb_world w;
    struct chunk *near[9];
    struct rb_band bands[27];
};
static __attribute__((address_space(3))) struct listblk mtab_lb;

/* a block a request and a part of its cells (blockIdx.y), a thread a cell
 * (beside mesh_tab_cache, over the band table):
 * its kind (rb_mesh_cell_kind: nothing, a hidden cube, a draw) and a drawn
 * cell's table class, its id and meta to tidm, a table cell appended to its
 * class's list (a warp's cells of one class together) */
__attribute__((nvptx_kernel)) void mesh_tab_list(const struct meshing_launch *L)
{
    const unsigned all = 0xffffffffu;
    int lane = (int)__nvvm_read_ptx_sreg_laneid(), g = (int)__nvvm_read_ptx_sreg_tid_x();
    int s = (int)__nvvm_read_ptx_sreg_ctaid_x(), c = (int)(__nvvm_read_ptx_sreg_ctaid_y() * __nvvm_read_ptx_sreg_ntid_x()) + g;
    if (s >= L->n) return;
    struct listblk *B = (struct listblk *)&mtab_lb;
    const struct meshing_req *q = (const struct meshing_req *)L->req + s;
    for (int k = g * 8; k < (int)sizeof B->m; k += (int)__nvvm_read_ptx_sreg_ntid_x() * 8)
    {
        uint64_t v;
        __builtin_memcpy(&v, (const char *)L->mesher + k, 8);
        __builtin_memcpy((char *)&B->m + k, &v, 8);
    }
    /* its own band table (beside mesh_tab_cache: no cache) */
    if (g < 27) band_of_req(B->bands, q, g);
    __nvvm_barrier_sync(0);
    if (g == 0)
    {
        world_raw(&B->w, B->near, q, B->bands);
        B->m.w = &B->w;
        B->m.t = NULL;
        B->m.dev = NULL;
        B->m.px = q->px;
        B->m.py = q->py;
        B->m.pz = q->pz;
    }
    __nvvm_barrier_sync(0);
    int x = (q->cx << 4) + (c & 15), y = (q->s << 4) + (c >> 8), z = (q->cz << 4) + ((c >> 4) & 15), id = 0, meta = 0;
    int kind = rb_mesh_cell_kind(&B->m, x, y, z, q->pass, &id, &meta);
    int k = kind == 2 ? tab_class(L, id, meta, x, y, z, q->px, q->py, q->pz) : MTAB_NO;
    ((uint16_t *)L->tidm)[(size_t)s * 4096 + c] = (uint16_t)(k ? id | meta << 12 : 0);
    /* a drawn cell the table cannot draw: the request to mesh_run */
    if (__nvvm_vote_ballot_sync(all, kind == 2 && !k) && lane == 0) __atomic_fetch_or((int32_t *)L->tflag + s, MTAB_REDO, __ATOMIC_RELAXED);
    size_t cap = (size_t)L->n * 4096;
    for (int x = 1; x < MTAB_CLASSES; ++x)
    {
        unsigned b = __nvvm_vote_ballot_sync(all, k == x);
        if (!b) continue;
        int base = 0;
        if (lane == 0) base = __atomic_fetch_add((int32_t *)L->tlcnt + x, __builtin_popcount(b), __ATOMIC_RELAXED);
        base = __nvvm_shfl_sync_idx_i32(all, base, 0, 0x1f);
        if (k == x) ((int32_t *)L->tlist)[(size_t)x * cap + (size_t)base + (size_t)__builtin_popcount(b & ((1u << lane) - 1u))] = s << 12 | c;
    }
}

/* a thread a cell of class CLS's list (strided over the grid; a warp a cell
 * for MTAB_GEN, whose cells' draws differ), drawn from the template (copied
 * at a thread's first cell: a draw that passes its checks leaves the
 * template's words but the bounds, which each draw sets first); a warp's
 * vertices placed together in tvbuf, each cell's place in tcell */
static inline __attribute__((always_inline)) void draw_list(const struct meshing_launch *L, int cls)
{
    const unsigned all = 0xffffffffu;
    int lane = (int)__nvvm_read_ptx_sreg_laneid();
    int nth = (int)(__nvvm_read_ptx_sreg_nctaid_x() * __nvvm_read_ptx_sreg_ntid_x());
    int t0 = (int)(__nvvm_read_ptx_sreg_ctaid_x() * __nvvm_read_ptx_sreg_ntid_x() + __nvvm_read_ptx_sreg_tid_x()) - lane;
    const int one = cls == MTAB_GEN;
    if (one) nth /= 32, t0 /= 32;
    const struct rb_mesher *tp = (const struct rb_mesher *)L->mesher;
    const int32_t *list = (const int32_t *)L->tlist + (size_t)cls * (size_t)L->n * 4096;
    int cnt = ((const int32_t *)L->tlcnt)[cls];
    int32_t v[MTAB_MAXV * 8] __attribute__((aligned(16)));
    struct rb_mesher m;
    struct rb_tess t;
    struct rb_world wd;
    struct chunk *near[9];
    struct meshing_ctx cx;
    int dirty = 1;
    for (int e0 = t0; e0 < cnt; e0 += nth)
    {
        int e = one ? e0 : e0 + lane, mine = e < cnt && (!one || lane == 0), n = 0, fl = 0, s = 0, c = 0;
        if (mine)
        {
            s = list[e] >> 12;
            c = list[e] & 4095;
            const struct meshing_req *q = (const struct meshing_req *)L->req + s;
            int w = ((const uint16_t *)L->tidm)[(size_t)s * 4096 + c];
            world_at(&wd, near, L, s);
            if (dirty) copy8(&m, tp, sizeof m);
            __builtin_memset(&t, 0, sizeof t);
            cx = (struct meshing_ctx){(const struct rb_trig *)q->trig, (const struct rb_trig *)L->trig, q->ntrig, L->ntrig, 0};
            m.w = &wd;
            m.t = &t;
            m.dev = &cx;
            m.px = q->px;
            m.py = q->py;
            m.pz = q->pz;
            rb_mesh_section_begin(&m, q->cx, q->cz, q->s);
            n = mtab_draw_check(&m, &t, tp, w & 4095, w >> 12, (q->cx << 4) + (c & 15), (q->s << 4) + (c >> 8), (q->cz << 4) + ((c >> 4) & 15),
                                v, &fl, cls, 0);
            if (cx.err) fl |= MTAB_REDO << 4;
            dirty = fl >> 4 & MTAB_REDO;
        }
        int incl = n;
        for (int d = 1; d < 32; d <<= 1)
        {
            int u = __nvvm_shfl_sync_up_i32(all, incl, d, 0);
            if (lane >= d) incl += u;
        }
        int tot = __nvvm_shfl_sync_idx_i32(all, incl, 31, 0x1f);
        unsigned long long base = 0;
        if (lane == 0 && tot > 0) base = __atomic_fetch_add((unsigned long long *)L->tvcur, (unsigned long long)tot, __ATOMIC_RELAXED);
        base = ((unsigned long long)(uint32_t)__nvvm_shfl_sync_idx_i32(all, (int)(base >> 32), 0, 0x1f) << 32) |
               (uint32_t)__nvvm_shfl_sync_idx_i32(all, (int)(uint32_t)base, 0, 0x1f);
        int fits = base + (unsigned long long)tot <= (unsigned long long)L->tvcap, flag = 0;
        if (mine)
        {
            unsigned long long at = base + (unsigned long long)(incl - n);
            if (fits)
            {
                /* the vertices to tvbuf, 16 bytes at a time, each held to the
                 * marks (a field no setter of the cell wrote) */
                const uint32_t mu = mtab_fbits((float)MTAB_MARK_UV);
                const v4u *a = (const v4u *)v;
                v4u *dst = (v4u *)((int32_t *)L->tvbuf + (size_t)at * 8);
                int bad = 0;
                for (int j = 0; j < n; ++j)
                {
                    v4u p = a[2 * j], r = a[2 * j + 1];
                    bad |= p.w == mu || r.x == mu || r.y == (uint32_t)MTAB_MARK_COLOR || r.w == (uint32_t)MTAB_MARK_BRIGHT;
                    dst[2 * j] = p;
                    dst[2 * j + 1] = r;
                }
                if (bad) fl |= MTAB_REDO << 4;
                ((uint64_t *)L->tcell)[(size_t)s * 4096 + c] = at | (uint64_t)n << 40;
            }
            flag = (fl & 15) << 8 | (fl >> 4 & MTAB_REDO) | (fits ? 0 : MTAB_FULL);
        }
        /* the request's flags and count: once for a warp whose cells are one
         * request's, else a cell at a time */
        unsigned act = __nvvm_vote_ballot_sync(all, mine);
        int s0 = __nvvm_shfl_sync_idx_i32(all, s, act ? __builtin_ctz(act) : 0, 0x1f);
        if (__nvvm_vote_all_sync(all, !mine || s == s0))
        {
            int f = flag, m_ = n;
            for (int d = 16; d > 0; d >>= 1)
            {
                f |= __nvvm_shfl_sync_down_i32(all, f, d, 0x1f);
                m_ += __nvvm_shfl_sync_down_i32(all, m_, d, 0x1f);
            }
            if (lane == 0 && act)
            {
                if (f) __atomic_fetch_or((int32_t *)L->tflag + s0, f, __ATOMIC_RELAXED);
                if (m_) __atomic_fetch_add((int32_t *)L->ttot + s0, m_, __ATOMIC_RELAXED);
            }
        }
        else if (mine)
        {
            if (flag) __atomic_fetch_or((int32_t *)L->tflag + s, flag, __ATOMIC_RELAXED);
            if (n) __atomic_fetch_add((int32_t *)L->ttot + s, n, __ATOMIC_RELAXED);
        }
    }
}

/* the standard blocks' list (CLS MTAB_CUBE), a group of 8 lanes a cell (4
 * a warp, strided over the grid), lane f of a group the cell's face f
 * (0..5); or the boxed cells' (MTAB_BOX), a warp a cell, lane l its box
 * l / 6's face l % 6: whether it is drawn, then (the warp's quads placed
 * together in tvbuf, in cell, box and face order) its quad straight to its
 * place (mtab_cube_face); a group's first lane the cell's place in tcell */
static inline __attribute__((always_inline)) void draw_faces(const struct meshing_launch *L, const int cls)
{
    const unsigned all = 0xffffffffu;
    const int G = cls == MTAB_BOX ? 32 : 8, PG = 32 / G;
    int lane = (int)__nvvm_read_ptx_sreg_laneid(), grp = lane / G, f = lane % G % 6, bxi = lane % G / 6;
    int nw = (int)(__nvvm_read_ptx_sreg_nctaid_x() * __nvvm_read_ptx_sreg_ntid_x() / 32);
    int w0 = (int)((__nvvm_read_ptx_sreg_ctaid_x() * __nvvm_read_ptx_sreg_ntid_x() + __nvvm_read_ptx_sreg_tid_x()) / 32);
    const struct rb_mesher *tp = (const struct rb_mesher *)L->mesher;
    const int32_t *list = (const int32_t *)L->tlist + (size_t)cls * (size_t)L->n * 4096;
    int cnt = ((const int32_t *)L->tlcnt)[cls];
    struct rb_mesher m;
    struct rb_tess t;
    struct rb_world wd;
    struct chunk *near[9];
    struct meshing_ctx cx;
    int fresh = 1;
    for (int e0 = w0 * PG; e0 < cnt; e0 += nw * PG)
    {
        int e = e0 + grp, have = e < cnt, s = 0, c = 0, shown = 0, id = 0, meta = 0, x = 0, y = 0, z = 0;
        if (have && lane % G < (cls == MTAB_BOX ? 6 * MTAB_BOXES : 6))
        {
            s = list[e] >> 12;
            c = list[e] & 4095;
            const struct meshing_req *q = (const struct meshing_req *)L->req + s;
            int w = ((const uint16_t *)L->tidm)[(size_t)s * 4096 + c];
            id = w & 4095;
            meta = w >> 12;
            x = (q->cx << 4) + (c & 15);
            y = (q->s << 4) + (c >> 8);
            z = (q->cz << 4) + ((c >> 4) & 15);
            world_at(&wd, near, L, s);
            /* the words past the bounds stay the template's: a face's draw
             * writes none of them */
            if (fresh) copy8(&m, tp, sizeof m);
            fresh = 0;
            __builtin_memset(&t, 0, sizeof t);
            cx = (struct meshing_ctx){(const struct rb_trig *)q->trig, (const struct rb_trig *)L->trig, q->ntrig, L->ntrig, 0};
            m.w = &wd;
            m.t = &t;
            m.dev = &cx;
            m.px = q->px;
            m.py = q->py;
            m.pz = q->pz;
            rb_mesh_section_begin(&m, q->cx, q->cz, q->s);
            mtab_begin(&m, id, meta, x, y, z);
            if (cls == MTAB_BOX)
            {
                struct mtab_box bx[MTAB_BOXES];
                if (bxi < mtab_boxes(&m, id, meta, x, y, z, bx))
                {
                    mtab_box_set(&m, &bx[bxi]);
                    shown = mtab_cube_face_shown(&m, id, x, y, z, f);
                }
            }
            else if (cls == MTAB_DOOR) shown = mtab_door_drawn(&m, id, x, y, z);
            else if (cls == MTAB_FLUID) shown = mtab_fluid_shown(&m, id, x, y, z, f);
            else shown = mtab_cube_face_shown(&m, id, x, y, z, f);
        }
        /* the lanes' vertices (a liquid's top and sides 8, a quad 4), their
         * places in the warp's in lane order: cell, box, face */
        int nv = shown ? (cls == MTAB_FLUID && f != 1 ? 8 : 4) : 0, incl = nv;
        for (int d = 1; d < 32; d <<= 1)
        {
            int u = __nvvm_shfl_sync_up_i32(all, incl, d, 0);
            if (lane >= d) incl += u;
        }
        int tot = __nvvm_shfl_sync_idx_i32(all, incl, 31, 0x1f);
        int gfirst = __nvvm_shfl_sync_idx_i32(all, incl - nv, grp * G, 0x1f), glast = __nvvm_shfl_sync_idx_i32(all, incl, grp * G + G - 1, 0x1f);
        unsigned long long base = 0;
        if (lane == 0 && tot > 0) base = __atomic_fetch_add((unsigned long long *)L->tvcur, (unsigned long long)tot, __ATOMIC_RELAXED);
        base = ((unsigned long long)(uint32_t)__nvvm_shfl_sync_idx_i32(all, (int)(base >> 32), 0, 0x1f) << 32) |
               (uint32_t)__nvvm_shfl_sync_idx_i32(all, (int)(uint32_t)base, 0, 0x1f);
        int fits = base + (unsigned long long)tot <= (unsigned long long)L->tvcap;
        if (shown && fits)
        {
            float r = 0, g = 0, bl = 0;
            if (cls != MTAB_DOOR && cls != MTAB_FLUID) mtab_cube_color(&m, id, x, y, z, &r, &g, &bl);
            t.raw = (int32_t *)L->tvbuf + (size_t)(base + (unsigned long long)(incl - nv)) * 8;
            t.cap = nv * 8;
            if (cls == MTAB_DOOR) mtab_door_face(&m, id, x, y, z, f, (const struct rb_uv *)((const uint8_t *)L->tab + RB_IDS * RB_METAS));
            else if (cls == MTAB_FLUID) mtab_fluid_face(&m, id, x, y, z, f);
            else mtab_cube_face(&m, id, meta, x, y, z, f, r, g, bl,
                                (const int32_t *)((const uint8_t *)L->tab + RB_IDS * RB_METAS + RB_IDS * sizeof(struct rb_uv)));
        }
        /* a box's words back to the template's (a lane's next cell begins
         * from them; the bounds its begin sets) */
        if ((cls == MTAB_BOX || cls == MTAB_DOOR) && have)
        {
            m.flip_texture = tp->flip_texture;
            m.f152631f = tp->f152631f;
            m.uv_east = tp->uv_east;
            m.uv_west = tp->uv_west;
            m.uv_south = tp->uv_south;
            m.uv_north = tp->uv_north;
            m.uv_top = tp->uv_top;
            m.uv_bottom = tp->uv_bottom;
        }
        /* the cell: a group's first lane (its flags: a face drawn sets the
         * texture, the colour and the brightness) */
        int lead = have && lane % G == 0, n = lead ? glast - gfirst : 0, flag = 0;
        if (lead)
        {
            unsigned long long at = base + (unsigned long long)gfirst;
            if (fits) ((uint64_t *)L->tcell)[(size_t)s * 4096 + c] = at | (uint64_t)n << 40;
            flag = (n ? 7 << 8 : 0) | (fits ? 0 : MTAB_FULL);
        }
        unsigned act = __nvvm_vote_ballot_sync(all, lead);
        int s0 = __nvvm_shfl_sync_idx_i32(all, s, act ? __builtin_ctz(act) : 0, 0x1f);
        if (__nvvm_vote_all_sync(all, !lead || s == s0))
        {
            int fo = flag, m_ = n;
            for (int d = 16; d > 0; d >>= 1)
            {
                fo |= __nvvm_shfl_sync_down_i32(all, fo, d, 0x1f);
                m_ += __nvvm_shfl_sync_down_i32(all, m_, d, 0x1f);
            }
            if (lane == 0 && act)
            {
                if (fo) __atomic_fetch_or((int32_t *)L->tflag + s0, fo, __ATOMIC_RELAXED);
                if (m_) __atomic_fetch_add((int32_t *)L->ttot + s0, m_, __ATOMIC_RELAXED);
            }
        }
        else if (lead)
        {
            if (flag) __atomic_fetch_or((int32_t *)L->tflag + s, flag, __ATOMIC_RELAXED);
            if (n) __atomic_fetch_add((int32_t *)L->ttot + s, n, __ATOMIC_RELAXED);
        }
    }
}

__attribute__((nvptx_kernel)) void mesh_tab_draw_fluid(const struct meshing_launch *L)
{
    draw_faces(L, MTAB_FLUID);
}

__attribute__((nvptx_kernel)) void mesh_tab_draw_gen(const struct meshing_launch *L)
{
    draw_list(L, MTAB_GEN);
}

__attribute__((nvptx_kernel)) void mesh_tab_draw(const struct meshing_launch *L)
{
    draw_faces(L, MTAB_CUBE);
}

__attribute__((nvptx_kernel)) void mesh_tab_draw_box(const struct meshing_launch *L)
{
    draw_faces(L, MTAB_BOX);
}

__attribute__((nvptx_kernel)) void mesh_tab_draw_door(const struct meshing_launch *L)
{
    draw_faces(L, MTAB_DOOR);
}

/* ------------------------------------------------------------ mesh_tab_place */

enum { PT = MTAB_THREADS, ROUNDS = 4096 / PT, PW = PT / 32 };

struct placeblk {
    int wsum[ROUNDS][PW];
    int spec;
};
static __attribute__((address_space(3))) struct placeblk mtab_pb;

/* a block a request, after the table's draws: its cells laid out in order
 * (PT cells a round, a thread a cell), its place in vbuf, its count and
 * flags (mesh_tab_copy then copies the vertices); the request left to the
 * write pass (voff -1, MESHING_RES_REDRAW) when the table passed its buffer, to mesh_run (after
 * mesh_tab_copy) when a check failed or a cell is not the table's
 * (MTAB_REDO) */
__attribute__((nvptx_kernel)) void mesh_tab_place(const struct meshing_launch *L)
{
    const unsigned all = 0xffffffffu;
    int lane = (int)__nvvm_read_ptx_sreg_laneid(), g = (int)__nvvm_read_ptx_sreg_tid_x(), warp = g / 32;
    int s = (int)__nvvm_read_ptx_sreg_ctaid_x(), n = L->n;
    if (s >= n) return;
    int tflag = ((const int32_t *)L->tflag)[s];
    if (tflag & MTAB_REDO) return;
    struct placeblk *B = (struct placeblk *)&mtab_pb;
    const struct meshing_req *q = (const struct meshing_req *)L->req + s;
    const uint16_t *idm = (const uint16_t *)L->tidm + (size_t)s * 4096;
    const uint64_t *tc = (const uint64_t *)L->tcell + (size_t)s * 4096;
    /* the mesher's special icons against the copy made with them (a draw
     * that wrote one: kernels.c specials_changed) */
    if (g == 0) B->spec = 0;
    __nvvm_barrier_sync(0);
    if (L->spec0)
    {
        const uint32_t *sp = (const uint32_t *)((const struct rb_mesher *)L->mesher)->special, *s0 = (const uint32_t *)L->spec0;
        uint32_t d = 0;
        for (int k = g; k < (int)((RB_SPECIALS + RB2_SPECIALS + RB3_SPECIALS) * sizeof(struct rb_uv) / 4); k += PT) d |= sp[k] ^ s0[k];
        if (d) B->spec = 1;
    }
    /* each round's cells: its count (every round's loads first), then its
     * offset */
    int cnt[ROUNDS], off[ROUNDS], total = 0, tid[ROUNDS];
    for (int r = 0; r < ROUNDS; ++r) tid[r] = idm[r * PT + g];
    uint64_t cw[ROUNDS];
    for (int r = 0; r < ROUNDS; ++r) cw[r] = tid[r] ? tc[r * PT + g] : 0;
    for (int r = 0; r < ROUNDS; ++r)
    {
        cnt[r] = tid[r] ? (int)(cw[r] >> 40 & 255) : 0;
        int incl = cnt[r];
        for (int d = 1; d < 32; d <<= 1)
        {
            int u = __nvvm_shfl_sync_up_i32(all, incl, d, 0);
            if (lane >= d) incl += u;
        }
        if (lane == 31) B->wsum[r][warp] = incl;
        off[r] = incl - cnt[r];
    }
    __nvvm_barrier_sync(0);
    for (int r = 0; r < ROUNDS; ++r)
    {
        int before = 0, sum = 0;
        for (int k = 0; k < PW; ++k)
        {
            int ws = B->wsum[r][k];
            if (k < warp) before += ws;
            sum += ws;
        }
        off[r] += total + before;
        total += sum;
    }
    struct meshing_res *res = (struct meshing_res *)L->res + s;
    if (g == 0)
    {
        /* the count: the table's (the cells' sum unless the table passed
         * its buffer); the flags its cells set, the specials unchanged */
        int count = ((const int32_t *)L->ttot)[s];
        int err = (count != total && !(tflag & MTAB_FULL)) || B->spec ? MESHING_ERR_STATE : 0;
        unsigned long long base = 0;
        int put = !(tflag & MTAB_FULL) && !err && count > 0 && L->vbuf;
        if (put) base = __atomic_fetch_add((unsigned long long *)L->vcur, (unsigned long long)count, __ATOMIC_RELAXED);
        put = put && base + (unsigned long long)count <= (unsigned long long)L->vcap;
        ((long long *)L->voff)[q->key] = put ? (long long)base : -1;
        /* vertices left out of vbuf: the write pass draws them (mesh_run) */
        *res = (struct meshing_res){count, tflag >> 8 & 15, err, count > 0 && !put ? MESHING_RES_REDRAW : 0};
    }
    /* each cell's place in the request, for mesh_tab_copy */
    for (int r = 0; r < ROUNDS; ++r) ((int32_t *)L->toff)[(size_t)s * 4096 + r * PT + g] = off[r];
}

/* a block a request and a round of its cells (blockIdx.y), a thread a cell:
 * its vertices from tvbuf to their place in vbuf
 * (mesh_tab_place's), 16 bytes at a time, eight loads out, then their
 * stores (a store waits for its load: one at a time, each move would wait
 * a full trip); not a request left to the write pass or to mesh_run */
__attribute__((nvptx_kernel)) void mesh_tab_copy(const struct meshing_launch *L)
{
    int s = (int)__nvvm_read_ptx_sreg_ctaid_x(), c = (int)(__nvvm_read_ptx_sreg_ctaid_y() * PT + __nvvm_read_ptx_sreg_tid_x());
    if (s >= L->n || ((const int32_t *)L->tflag)[s] & MTAB_REDO) return;
    const struct meshing_req *q = (const struct meshing_req *)L->req + s;
    long long base = ((const long long *)L->voff)[q->key];
    if (base < 0) return;
    if (!((const uint16_t *)L->tidm)[(size_t)s * 4096 + c]) return;
    uint64_t w = ((const uint64_t *)L->tcell)[(size_t)s * 4096 + c];
    int cnt = (int)(w >> 40 & 255);
    const int32_t *src = (const int32_t *)L->tvbuf + (size_t)(w & 0xffffffffffull) * 8;
    if (!cnt) return;
    const v4u *restrict a = (const v4u *)src;
    v4u *restrict d = (v4u *)((int32_t *)L->vbuf + (size_t)(base + ((const int32_t *)L->toff)[(size_t)s * 4096 + c]) * 8);
    int nw = cnt * 2, k = 0;
    for (; k + 8 <= nw; k += 8)
    {
        v4u x[8];
        for (int j = 0; j < 8; ++j) x[j] = a[k + j];
        for (int j = 0; j < 8; ++j) d[k + j] = x[j];
    }
    for (; k < nw; ++k) d[k] = a[k];
}
