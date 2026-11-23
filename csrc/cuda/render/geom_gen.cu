/* The generic geometry kernel (render.h; lane/renderbuild's split of the
 * geometry kernels) and its vertex half: the sky's slots and the section
 * quads the lean kernel handed to the clipper, each through raster.c's
 * clip_triangle and live_emit; the recorded passes' units are its other
 * half, in geom_rec.cu. A count pass runs the same decisions over the
 * clip coordinates alone (cvx).
 * Every function below that computes a float is a port of the raster.c,
 * raster_sky2.c, raster_entities.c, raster_overlay.c or raster_tileent.c
 * function it names, operation for operation, so the build's --fmad=false,
 * -prec-div=true and -prec-sqrt=true give the C engine's bits
 * (csrc/cuda/render/mirrors.txt lists them for the mirror rule: a change
 * to one of those C functions needs its port here). */
#include "geom.cuh"
#include "rclip.cuh"

/* ------------------------------------------------------------ vertices */

/* raster.c sky_vertex */
static __device__ void sky_vertex(const envp &P, float x, float y, float z, vtx &v)
{
    memset(&v, 0, sizeof v);
    float eye[4];
    for (int r = 0; r < 4; ++r)
        eye[r] = P.mv[r] * x + P.mv[4 + r] * y + P.mv[8 + r] * z + P.mv[12 + r];
    for (int r = 0; r < 4; ++r)
        v.clip[r] = P.proj[r] * eye[0] + P.proj[4 + r] * eye[1] + P.proj[8 + r] * eye[2] + P.proj[12 + r] * eye[3];
    v.fog = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
}

/* raster.c make_vertex */
static __device__ void make_vertex(const envp &P, const ddraw &d, const int32_t *raw, int flags, vtx &v)
{
    memset(&v, 0, sizeof v);
    float pos[4];
    cam_pos(d, raw, pos);
    float eye[4];
    for (int r = 0; r < 4; ++r)
        eye[r] = P.mv[r] * pos[0] + P.mv[4 + r] * pos[1] + P.mv[8 + r] * pos[2] + P.mv[12 + r] * pos[3];
    for (int r = 0; r < 4; ++r)
        v.clip[r] = P.proj[r] * eye[0] + P.proj[4 + r] * eye[1] + P.proj[8 + r] * eye[2] + P.proj[12 + r] * eye[3];
    v.fog = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    v.u = __int_as_float(raw[3]);
    v.v = __int_as_float(raw[4]);
    uint32_t c = flags & 2 ? (uint32_t)raw[5] : 0xffffffffU;
    for (int i = 0; i < 4; ++i) v.c[i] = ((c >> (8 * i)) & 255) / 255.0f;
    v.l[0] = flags & 4 ? (float)((uint32_t)raw[7] & 65535) : 240;
    v.l[1] = flags & 4 ? (float)((uint32_t)raw[7] >> 16) : 240;
}

/* raster.c screen_vertex */
static __device__ __forceinline__ void screen_vertex(int W, int H, vtx &v)
{
    v.w = v.clip[3];
    v.x = (v.clip[0] / v.w * 0.5f + 0.5f) * W;
    v.y = (0.5f - v.clip[1] / v.w * 0.5f) * H;
    v.sx = rintf((v.x - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v.sy = rintf((v.y - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v.depth = v.clip[2] / v.w * 0.5f + 0.5f;
}

/* ------------------------------------------------------------ sinks */

/* a pass's triangles counted (from the count's vertices, cvx), or written;
 * with the fan triangles it emitted (bit I of em: fan triangle I, or 1 for
 * a triangle inside every plane) and whether quads_clip's one-plane write
 * makes them (lean: inside every plane, or clipped by its one plane alone) */
struct count_sink {
    static constexpr bool WRITE = false;
    float *tab;                         /* the thread's column of the kernel's clipper table (rclip.cuh) */
    uint32_t n;
    uint32_t em = 0;
    int fi = 1;
    bool lean = false;
    __device__ void emit(const cvx *, int, float, int, int, int, int) { ++n; em |= 1u << fi; }
};

struct write_sink {
    static constexpr bool WRITE = true;
    float *tab;                         /* the thread's column of the kernel's clipper table (rclip.cuh) */
    tri *out;
    uint32_t *tiles;                    /* each triangle's tile count */
    uint32_t at;
    int tiles_x;
    bool rcp;                           /* raster_prec.h RP_RCP: the terrain's (not the sky's) 1 / w and 1 / area */
    bool pack;                          /* prec RP_FAST: its colour and light as halves (tri_ch, tri_lh) */
    __device__ void emit(const vtx *t, int mode, float area, int x0, int x1, int y0, int y1)
    {
        tri &o = out[at];
        /* triangle(): a two-sided (cloud) back face is drawn with v1 and v2
         * swapped and the area negated (the swap by value: an index into
         * T put the caller's vertices in local memory) */
        const bool sw = area < 0;
        if (sw) area = -area;
        float tsx[3], tsy[3];
        for (int i = 0; i < 3; ++i) {
            const vtx v = i == 0 ? t[0] : (i == 1) != sw ? t[1] : t[2];
            tsx[i] = v.sx; tsy[i] = v.sy;
            o.x[i] = v.x; o.y[i] = v.y; o.sx[i] = v.sx; o.sy[i] = v.sy;
            o.w[i] = rcp && !(mode & 15) ? 1.0f / v.w : v.w;
            o.d[i] = v.depth; o.u[i] = v.u; o.v[i] = v.v;
            if (pack && !(mode & 15)) {
                tri_ch(o)[i * 2] = __floats2half2_rn(v.c[0], v.c[1]);
                tri_ch(o)[i * 2 + 1] = __floats2half2_rn(v.c[2], v.c[3]);
                tri_lh(o)[i] = __floats2half2_rn(v.l[0], v.l[1]);
            } else {
                for (int c = 0; c < 4; ++c) o.c[i][c] = v.c[c];
                o.l[i][0] = v.l[0]; o.l[i][1] = v.l[1];
            }
            o.fog[i] = v.fog;
        }
        o.aux = tl_bits(tsx, tsy);
        o.area = rcp && !(mode & 15) ? 1.0f / area : area;
        o.x0 = (short)x0; o.x1 = (short)x1; o.y0 = (short)y0; o.y1 = (short)y1;
        o.mode = (uint32_t)mode;
        tiles[at] = (uint32_t)((x1 / TILE - x0 / TILE + 1) * (y1 / TILE - y0 / TILE + 1));
        ++at;
    }
    /* emit's record of a clipped triangle but the fields the clipper by
     * recorded operations replays (rc_fields): its screen fields from its
     * vertices' clip coordinates (screen_vertex's w and depth) */
    __device__ void emit_pos(const cvx *t, int mode, float area, int x0, int x1, int y0, int y1)
    {
        tri &o = out[at];
        const bool sw = area < 0;
        if (sw) area = -area;
        float tsx[3], tsy[3];
        for (int i = 0; i < 3; ++i) {
            const cvx v = i == 0 ? t[0] : (i == 1) != sw ? t[1] : t[2];
            tsx[i] = v.sx; tsy[i] = v.sy;
            o.x[i] = v.x; o.y[i] = v.y; o.sx[i] = v.sx; o.sy[i] = v.sy;
            const float w = v.clip[3];
            o.w[i] = rcp && !(mode & 15) ? 1.0f / w : w;
            o.d[i] = v.clip[2] / w * 0.5f + 0.5f;
        }
        o.aux = tl_bits(tsx, tsy);
        o.area = rcp && !(mode & 15) ? 1.0f / area : area;
        o.x0 = (short)x0; o.x1 = (short)x1; o.y0 = (short)y0; o.y1 = (short)y1;
        o.mode = (uint32_t)mode;
        tiles[at] = (uint32_t)((x1 / TILE - x0 / TILE + 1) * (y1 / TILE - y0 / TILE + 1));
        ++at;
    }
};

/* the write's sink for the clipper by recorded operations' fan: each
 * triangle live_emit keeps (bit I of em) and swaps (of sw), its record but
 * the replayed fields written */
struct rc_sink {
    write_sink *s;
    uint32_t em, sw;
    int i;
    __device__ void emit(const cvx *t, int mode, float area, int x0, int x1, int y0, int y1)
    {
        em |= 1u << i;
        if (area < 0) sw |= 1u << i;
        s->emit_pos(t, mode, area, x0, x1, y0, y1);
    }
};

/* raster.c live_emit: a zero area, a back face outside the two-sided clouds
 * or a box off the screen records nothing */
template <class V, class Sink>
static __device__ void emit_tri(int W, int H, const V *v, int mode, Sink &sink)
{
    float area = edge_xy(v[0].x, v[0].y, v[1].x, v[1].y, v[2].x, v[2].y);
    if (area == 0 || (area < 0 && (mode & 15) != 3)) return;
    float minx = fminf(v[0].sx, fminf(v[1].sx, v[2].sx));
    float maxx = fmaxf(v[0].sx, fmaxf(v[1].sx, v[2].sx));
    float miny = fminf(v[0].sy, fminf(v[1].sy, v[2].sy));
    float maxy = fmaxf(v[0].sy, fmaxf(v[1].sy, v[2].sy));
    int x0 = f2i(fmaxf(0, floorf(minx))), x1 = f2i(fminf((float)(W - 1), ceilf(maxx)));
    int y0 = f2i(fmaxf(0, floorf(miny))), y1 = f2i(fminf((float)(H - 1), ceilf(maxy)));
    if (x0 > x1 || y0 > y1) return;
    sink.emit(v, mode, area, x0, x1, y0, y1);
}

/* A polygon vertex of the clipper: what its planes and interpolation read
 * and write (lane/geomocc). The screen fields (x, y, sx, sy, w, depth) are
 * written by screen_vertex after the last plane alone, so the polygon
 * carries the rest: the clipper's arrays (local memory) move 52 bytes a
 * vertex where they moved vtx's 84, and the count's 16 where cvx's 32. */
struct pvx { float clip[4], c[4], l[2], fog, u, v; };
struct pcx { float clip[4]; };
template <class V> struct poly_of;
template <> struct poly_of<vtx> { typedef pvx T; };
template <> struct poly_of<cvx> { typedef pcx T; };

static __device__ __forceinline__ void to_poly(const vtx &a, pvx &p)
{
    for (int i = 0; i < 4; ++i) { p.clip[i] = a.clip[i]; p.c[i] = a.c[i]; }
    p.l[0] = a.l[0]; p.l[1] = a.l[1];
    p.fog = a.fog; p.u = a.u; p.v = a.v;
}
static __device__ __forceinline__ void to_poly(const cvx &a, pcx &p) { for (int i = 0; i < 4; ++i) p.clip[i] = a.clip[i]; }

/* a polygon vertex as the vertex the arrays of whole vertices held (its
 * screen fields screen_vertex's, from its clip coordinates) */
static __device__ __forceinline__ void of_poly(int W, int H, const pvx &p, vtx &a)
{
    for (int i = 0; i < 4; ++i) { a.clip[i] = p.clip[i]; a.c[i] = p.c[i]; }
    a.l[0] = p.l[0]; a.l[1] = p.l[1];
    a.fog = p.fog; a.u = p.u; a.v = p.v;
    screen_vertex(W, H, a);
}
static __device__ __forceinline__ void of_poly(int W, int H, const pcx &p, cvx &a)
{
    for (int i = 0; i < 4; ++i) a.clip[i] = p.clip[i];
    screen_vertex(W, H, a);
}

/* raster.c interpolate, over the polygon's fields (vtx's interpolate's
 * operations, field for field) */
static __device__ __forceinline__ void interpolate(const pvx &a, const pvx &b, float t, pvx &v)
{
    for (int i = 0; i < 4; ++i) {
        v.clip[i] = a.clip[i] + t * (b.clip[i] - a.clip[i]);
        v.c[i] = a.c[i] + t * (b.c[i] - a.c[i]);
    }
    for (int i = 0; i < 2; ++i) v.l[i] = a.l[i] + t * (b.l[i] - a.l[i]);
    v.fog = a.fog + t * (b.fog - a.fog);
    v.u = a.u + t * (b.u - a.u);
    v.v = a.v + t * (b.v - a.v);
}
static __device__ __forceinline__ void interpolate(const pcx &a, const pcx &b, float t, pcx &v)
{
    for (int i = 0; i < 4; ++i) v.clip[i] = a.clip[i] + t * (b.clip[i] - a.clip[i]);
}

/* polygon vertex ID of R as the count's vertex (its screen position
 * screen_vertex's) */
static __device__ __forceinline__ void rc_cvx(int W, int H, const rcpoly &r, int id, cvx &v)
{
    rc_pos(r, id, v.clip);
    screen_vertex(W, H, v);
}

/* the fields of the write's triangles from R's fan that rc_sink kept (EM,
 * SW; the first at AT0): each attribute of every polygon vertex replayed
 * (rc_replay), a group at a time (G: 0 u, 1 v, 2 fog, 3 the colour's r
 * and g, 4 its b and a, 5 the light), and stored as write_sink stores it */
static __device__ __forceinline__ void rc_fields(const vtx *in, const rcpoly &r, int mode, const write_sink &sink,
                                                 uint32_t at0, uint32_t em, uint32_t sw)
{
    const bool pk = sink.pack && !(mode & 15);
    const int i0 = rc_id(r, 0);
#pragma unroll
    for (int g = 0; g < 6; ++g) {
#pragma unroll
        for (int i = 0; i < 3; ++i) {
            const vtx &v = in[i];
            rc_set(r, 0, i, g == 0 ? v.u : g == 1 ? v.v : g == 2 ? v.fog : g == 3 ? v.c[0] : g == 4 ? v.c[2] : v.l[0]);
            if (g >= 3) rc_set(r, RC_S, i, g == 3 ? v.c[1] : g == 4 ? v.c[3] : v.l[1]);
        }
        rc_replay(r, 0);
        if (g >= 3) rc_replay(r, RC_S);
        uint32_t at = at0;
        for (int i = 1; i + 1 < r.n; ++i) {
            if (!(em >> i & 1)) continue;
            tri &o = sink.out[at++];
            const bool s = sw >> i & 1;
            const int id[3] = {i0, s ? rc_id(r, i + 1) : rc_id(r, i), s ? rc_id(r, i) : rc_id(r, i + 1)};
#pragma unroll
            for (int j = 0; j < 3; ++j) {
                const float x = rc_val(r, 0, id[j]);
                if (g == 0) o.u[j] = x;
                else if (g == 1) o.v[j] = x;
                else if (g == 2) o.fog[j] = x;
                else {
                    const float y = rc_val(r, RC_S, id[j]);
                    if (g == 5) {
                        if (pk) tri_lh(o)[j] = __floats2half2_rn(x, y);
                        else { o.l[j][0] = x; o.l[j][1] = y; }
                    }
                    else if (pk) tri_ch(o)[j * 2 + g - 3] = __floats2half2_rn(x, y);
                    else { o.c[j][2 * (g - 3)] = x; o.c[j][2 * (g - 3) + 1] = y; }
                }
            }
        }
    }
}

/* R's fan (clip_triangle's: triangle i of the polygon's vertices 0, i and
 * i + 1, each vertex's screen position once) through live_emit: counted,
 * or written (its screen fields by rc_sink, then the replayed ones) */
template <class V, class Sink>
static __device__ __forceinline__ void rc_fan(int W, int H, const V *in, const rcpoly &r, int mode, Sink &sink)
{
    cvx t[3];
    rc_cvx(W, H, r, rc_id(r, 0), t[0]);
    rc_cvx(W, H, r, rc_id(r, 1), t[1]);
    if constexpr (!Sink::WRITE) {
        for (int i = 1; i + 1 < r.n; ++i) {
            rc_cvx(W, H, r, rc_id(r, i + 1), t[2]);
            sink.fi = i;
            emit_tri(W, H, t, mode, sink);
            t[1] = t[2];
        }
    } else {
        const uint32_t at0 = sink.at;
        rc_sink rs{&sink, 0, 0, 0};
        for (int i = 1; i + 1 < r.n; ++i) {
            rc_cvx(W, H, r, rc_id(r, i + 1), t[2]);
            rs.i = i;
            emit_tri(W, H, t, mode, rs);
            t[1] = t[2];
        }
        if (rs.em) rc_fields(in, r, mode, sink, at0, rs.em, rs.sw);
    }
}

/* raster.c clip_triangle, over the write's vertices (vtx) or the count's
 * (cvx) */
template <class V, class Sink>
static __device__ void clip_tri(int W, int H, const V *in, int mode, Sink &sink)
{
    float c[3][4];
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 4; ++k) c[i][k] = in[i].clip[k];
    /* every vertex inside every plane (the planes each vertex is outside,
     * rc_out: clip_triangle's tests with the axis a constant, so the
     * vertices stay in registers) */
    if (!(rc_out(c[0]) | rc_out(c[1]) | rc_out(c[2]))) {
        V a[3] = {in[0], in[1], in[2]};
        for (int i = 0; i < 3; ++i) screen_vertex(W, H, a[i]);
        if constexpr (!Sink::WRITE) sink.lean = true;
        emit_tri(W, H, a, mode, sink);
        return;
    }
    /* the clipper by recorded operations (rclip.cuh); the arrays below
     * clip what it does not hold */
    {
        rcpoly r;
        const int k = rc_clip(c, sink.tab, r);
        if (k == 0) return;
        if (k > 0) {
            /* one plane clipped it (its two made vertices), the case
             * quads_clip writes */
            if constexpr (!Sink::WRITE) sink.lean = r.nops == 2;
            rc_fan(W, H, in, r, mode, sink);
            return;
        }
    }
    /* the polygon and the next, swapped after each plane (not copied), of
     * polygon vertices (pvx, pcx) */
    typedef typename poly_of<V>::T P;
    P va[9], vb[9], *a = va, *b = vb;
    for (int i = 0; i < 3; ++i) to_poly(in[i], va[i]);
    int n = 3;
    for (int plane = 0; plane < 6; ++plane) {
        int m = 0, axis = plane / 2;
        float sign = plane & 1 ? -1.0f : 1.0f;
        /* every vertex inside the plane: the pass copies the polygon as it
         * is (no vertex dropped, none made), so it is skipped: the arrays
         * live in local memory, and their copies were most of the kernel's
         * memory traffic (lane/coalesce) */
        bool all = true;
        for (int i = 0; i < n && all; ++i) all = a[i].clip[3] - sign * a[i].clip[axis] >= 0;
        if (all) continue;
        /* each vertex loaded once, the edge's end the next edge's start
         * (its distance the same operations) */
        P p = a[0];
        float dp = p.clip[3] - sign * p.clip[axis];
        for (int i = 0; i < n; ++i) {
            P q = a[i + 1 < n ? i + 1 : 0];
            float dq = q.clip[3] - sign * q.clip[axis];
            if (dp >= 0) b[m++] = p;
            if ((dp >= 0) != (dq >= 0)) interpolate(p, q, dp / (dp - dq), b[m++]);
            p = q;
            dp = dq;
        }
        n = m;
        if (n < 3) return;
        P *t = a; a = b; b = t;
    }
    /* the fan, each polygon vertex made whole once (the fan's first, then
     * each next one; a vertex's fields are its own operations whichever
     * triangle reads them): three loads of a vertex from the polygon's
     * local array a triangle were a third of generic_write's stalls
     * (lane/kernclimb) */
    V t[3];
    of_poly(W, H, a[0], t[0]);
    of_poly(W, H, a[1], t[1]);
    for (int i = 1; i + 1 < n; ++i) {
        of_poly(W, H, a[i + 1], t[2]);
        emit_tri(W, H, t, mode, sink);
        t[1] = t[2];
    }
}

/* a triangle of vertices T through clip_triangle: the write's whole, the
 * count's clip coordinates alone (the rest of T is then dead) */
template <class Sink>
static __device__ __forceinline__ void clip_tri_of(int W, int H, const vtx *t, int mode, Sink &sink)
{
    if constexpr (Sink::WRITE) clip_tri(W, H, t, mode, sink);
    else {
        cvx c[3];
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 4; ++k) c[i].clip[k] = t[i].clip[k];
        clip_tri(W, H, c, mode, sink);
    }
}

/* Triangle T of a unit's NT through clip_triangle: a quad (NT 2) as raster.c
 * draws every one, {q0, q1, q3} then {q1, q2, q3}; a triangle (NT 1) {q0,
 * q1, q3}. Each triangle is a thread of its own (lane/geomocc): the one
 * place a pass clips (each unit's own call, inlined, made the generic
 * kernel the largest function the build compiled: 7.7 s of ptxas;
 * lane/renderbuild). */
template <class Sink>
static __device__ __forceinline__ void clip_one(int W, int H, const vtx *q, int nt, int t, int mode, Sink &sink)
{
    if (t >= nt) return;
    vtx a[3] = {t ? q[1] : q[0], t ? q[2] : q[1], q[3]};
    clip_tri_of(W, H, a, mode, sink);
}

/* ------------------------------------------------------------ the sky's units */

/* raster.c draw_clouds: one slot of a cell (0 bottom, 1 top, then per unit
 * the x-, x+, z- and z+ sides); 0 when the cell does not draw it */
static __device__ int cloud_slot(const envp &P, int cell, int slot, float xyzuv[4][5], float *shade)
{
    const raster_obs_sky &k = P.sky;
    int xi = cell / 8 - 3, zi = cell % 8 - 3;
    float xx = (float)(xi * 8), zz = (float)(zi * 8);
    float x0 = xx - k.cloud_fx, x1 = xx + 8.0f - k.cloud_fx;
    float z0 = zz - k.cloud_fz, z1 = zz + 8.0f - k.cloud_fz;
    float u0 = xx * 0.00390625f + k.cloud_ix, u1 = (xx + 8.0f) * 0.00390625f + k.cloud_ix;
    float v0 = zz * 0.00390625f + k.cloud_iz, v1 = (zz + 8.0f) * 0.00390625f + k.cloud_iz;
    float y0 = k.cloud_height, y1 = y0 + 4.0f;
#define CQ(i, a, b, c, d, e) (xyzuv[i][0] = (a), xyzuv[i][1] = (b), xyzuv[i][2] = (c), xyzuv[i][3] = (d), xyzuv[i][4] = (e))
    if (slot == 0) {
        if (!(y0 > -5.0f)) return 0;
        CQ(0, x0, y0, z1, u0, v1); CQ(1, x1, y0, z1, u1, v1); CQ(2, x1, y0, z0, u1, v0); CQ(3, x0, y0, z0, u0, v0);
        *shade = 0.7f;
        return 1;
    }
    if (slot == 1) {
        if (!(y0 <= 5.0f)) return 0;
        float y = y1 - 0.0009765625f;
        CQ(0, x0, y, z1, u0, v1); CQ(1, x1, y, z1, u1, v1); CQ(2, x1, y, z0, u1, v0); CQ(3, x0, y, z0, u0, v0);
        *shade = 1.0f;
        return 1;
    }
    int unit = (slot - 2) / 4, side = (slot - 2) % 4;
    float u = (xx + (float)unit + 0.5f) * 0.00390625f + k.cloud_ix;
    float v = (zz + (float)unit + 0.5f) * 0.00390625f + k.cloud_iz;
    if (side == 0 || side == 1) {
        if (side == 0 ? !(xi > -1) : !(xi <= 1)) return 0;
        float sd = side == 0 ? x0 + (float)unit : x0 + (float)unit + 1.0f - 0.0009765625f;
        CQ(0, sd, y0, z1, u, v1); CQ(1, sd, y1, z1, u, v1); CQ(2, sd, y1, z0, u, v0); CQ(3, sd, y0, z0, u, v0);
        *shade = 0.9f;
        return 1;
    }
    if (side == 2 ? !(zi > -1) : !(zi <= 1)) return 0;
    float sd = side == 2 ? z0 + (float)unit : z0 + (float)unit + 1.0f - 0.0009765625f;
    CQ(0, x0, y1, sd, u0, v); CQ(1, x1, y1, sd, u1, v); CQ(2, x1, y0, sd, u1, v); CQ(3, x0, y0, sd, u0, v);
    *shade = 0.8f;
    return 1;
#undef CQ
}

/* raster_sky2.c celestial_quad's vertices, then raster.c celestial_quad */
static __device__ void celestial(const envp &P, int mode, float radius, float height,
                          float u0, float v0, float u1, float v1, vtx *q)
{
    float z0 = mode == 5 ? radius : -radius;
    float z1 = -z0;
    const float xyz[4][3] = {{-radius, height, z0}, {radius, height, z0},
                             {radius, height, z1}, {-radius, height, z1}};
    float cs = P.sky.cel_cs, sn = P.sky.cel_sn;
    for (int i = 0; i < 4; ++i) {
        float x = xyz[i][0], y = cs * xyz[i][1] - sn * xyz[i][2];
        float z = sn * xyz[i][1] + cs * xyz[i][2];
        sky_vertex(P, -z, y, x, q[i]);
    }
    q[0].u = q[3].u = u0;
    q[1].u = q[2].u = u1;
    q[0].v = q[1].v = v0;
    q[2].v = q[3].v = v1;
}

/* sky slot U's vertices Q and its mode: the triangles it draws (clip_one), 0
 * when the frame does not draw it */
static __device__ __forceinline__ int sky_verts(const envp &P, const float *stars, int u, vtx *q, int *mode)
{
    const raster_obs_sky &k = P.sky;
    if (k.dimension == 1) {
        if (u >= 6) return 0;
        /* raster.c draw_end_sky */
        const float p[4][3] = {{-100, -100, -100}, {-100, -100, 100}, {100, -100, 100}, {100, -100, -100}};
        const float uv[4][2] = {{0, 0}, {0, 16}, {16, 16}, {16, 0}};
        int side = u;
        for (int i = 0; i < 4; ++i) {
            float x = p[i][0], y = p[i][1], z = p[i][2], a;
            if (side == 1) { a = y; y = -z; z = a; }
            if (side == 2) { a = y; y = z; z = -a; }
            if (side == 3) { y = -y; z = -z; }
            if (side == 4) { a = x; x = -y; y = a; }
            if (side == 5) { a = x; x = y; y = -a; }
            sky_vertex(P, x, y, z, q[i]);
            q[i].u = uv[i][0];
            q[i].v = uv[i][1];
        }
        *mode = 7;
        return 2;
    }
    if (k.dimension != 0) return 0;
    if (u < U_RISE || (u >= U_PLANE2 && u < U_CLOUDS)) {
        /* raster.c draw_sky */
        int pass = u < U_RISE ? 1 : 2, i = u < U_RISE ? u : u - U_PLANE2;
        int x = -384 + 64 * (i / 13), z = -384 + 64 * (i % 13);
        float y = pass == 1 ? 16.0f : k.sky_floor;
        if (pass == 1) {
            sky_vertex(P, (float)x, y, (float)z, q[0]);
            sky_vertex(P, (float)(x + 64), y, (float)z, q[1]);
            sky_vertex(P, (float)(x + 64), y, (float)(z + 64), q[2]);
            sky_vertex(P, (float)x, y, (float)(z + 64), q[3]);
        } else {
            sky_vertex(P, (float)(x + 64), y, (float)z, q[0]);
            sky_vertex(P, (float)x, y, (float)z, q[1]);
            sky_vertex(P, (float)x, y, (float)(z + 64), q[2]);
            sky_vertex(P, (float)(x + 64), y, (float)(z + 64), q[3]);
        }
        *mode = pass;
        return 2;
    }
    if (u < U_SUN) {
        /* raster.c draw_sunrise: triangle i of the fan, i = 1..16 */
        if (k.sunrise[3] <= 0.0f) return 0;
        int i = u - U_RISE + 1;
        float dir = k.rise_dir;
        vtx t[3];
        sky_vertex(P, dir * 100.0f, 0.0f, 0.0f, t[0]);
        for (int c = 0; c < 4; ++c) t[0].c[c] = k.rise_center[c];
        for (int j = 0; j < 2; ++j) {
            float s = k.rise_sc[i - 1 + j][0], c = k.rise_sc[i - 1 + j][1];
            sky_vertex(P, dir * c * 120.0f, c * 40.0f * k.sunrise[3], -dir * s * 120.0f, t[1 + j]);
            for (int m = 0; m < 3; ++m) t[1 + j].c[m] = t[0].c[m];
        }
        q[0] = t[0]; q[1] = t[1]; q[2] = q[3] = t[2];
        *mode = 6;
        return 1;
    }
    if (u == U_SUN) {
        celestial(P, 4, 30.0f, 100.0f, 0, 0, 1, 1, q);
        *mode = 4;
        return 2;
    }
    if (u == U_MOON) {
        celestial(P, 5, 20.0f, -100.0f, k.moon_uv[0], k.moon_uv[1], k.moon_uv[2], k.moon_uv[3], q);
        *mode = 5;
        return 2;
    }
    if (u < U_PLANE2) {
        /* raster_sky2_stars, raster.c celestial_quad's mode 8 */
        int s = u - U_STARS;
        if (!k.stars || s >= (int)P.nstars) return 0;
        float cs = k.cel_cs, sn = k.cel_sn;
        for (int i = 0; i < 4; ++i) {
            const float *p = stars + ((size_t)s * 4 + i) * 3;
            float y = cs * p[1] - sn * p[2], z = sn * p[1] + cs * p[2];
            sky_vertex(P, -z, y, p[0], q[i]);
            q[i].c[0] = q[i].c[1] = q[i].c[2] = q[i].c[3] = k.star;
        }
        *mode = 8;
        return 2;
    }
    /* raster.c draw_clouds, cloud_quad */
    if (!k.clouds) return 0;
    int c = u - U_CLOUDS, pass = c / (64 * 34), cell = c % (64 * 34) / 34, slot = c % 34;
    float p[4][5], shade;
    if (!cloud_slot(P, cell, slot, p, &shade)) return 0;
    for (int i = 0; i < 4; ++i) {
        sky_vertex(P, p[i][0] * 12.0f, p[i][1], p[i][2] * 12.0f, q[i]);
        q[i].u = p[i][3];
        q[i].v = p[i][4];
        q[i].c[0] = shade;
    }
    *mode = 3 | (pass == 0 ? 16 : 0);
    return 2;
}

/* triangle T of a section quad (raster.c live_draw_section): quad I of
 * draw D's vertices {q0, q1, q3}, then {q1, q2, q3} (clip_one's), each
 * made by make_vertex; the quad's fourth is not made (lane/cliprep) */
static __device__ __forceinline__ void quad_tri(const envp &P, const ddraw &d, const int32_t *mesh,
                                                const int32_t *orders, uint32_t i, int t, vtx *a, int *mode)
{
    int offset = d.order >= 0 ? orders[d.order + i] : (int)i * 4;
    for (int j = 0; j < 3; ++j) {
        int32_t raw[8];
        mesh_vertex(mesh, d, offset + (j == 2 ? 3 : j + t), raw);
        make_vertex(P, d, raw, d.flags, a[j]);
    }
    *mode = d.water ? 32 : 0;
}

/* the vertex half: triangle T of unit X of environment E (a sky slot or a
 * clipped section quad), the triangles it makes counted (returned) or
 * written from AT on */
template <class Sink>
static __device__ __forceinline__ void gen_vtx(const envp &E, const xunit &x, int t, const ddraw *draws,
                                               const int32_t *mesh, const int32_t *orders, const float *stars,
                                               int W, int H, Sink &s)
{
    int mode = 0;
    if (x.kind == XU_SKY) {
        vtx q[4];
        int nt = sky_verts(E, stars, (int)x.a, q, &mode);
        clip_one(W, H, q, nt, t, mode, s);
    } else {
        vtx a[3];
        quad_tri(E, draws[x.a], mesh, orders, x.b, t, a, &mode);
        clip_tri_of(W, H, a, mode, s);
    }
}

/* the triangles it makes; *LEAN the fan triangles quads_clip writes (a
 * section quad's triangle inside every plane or clipped by one), else 0 */
static __device__ __forceinline__ uint32_t gen_vtx_count(const envp &E, const xunit &x, int t, const ddraw *draws,
                                                         const int32_t *mesh, const int32_t *orders, const float *stars,
                                                         int W, int H, float *tab, uint32_t *lean)
{
    count_sink s{tab, 0};
    gen_vtx(E, x, t, draws, mesh, orders, stars, W, H, s);
    *lean = x.kind == XU_QUAD && s.lean ? s.em : 0;
    return s.n;
}

static __device__ __forceinline__ void gen_vtx_write(const envp &E, const xunit &x, int t, const ddraw *draws,
                                                     const int32_t *mesh, const int32_t *orders, const float *stars,
                                                     int W, int H, uint32_t at, tri *out, uint32_t *tiles, float *tab)
{
    write_sink s{tab, out, tiles, at, (W + TILE - 1) / TILE, (E.prec & RP_RCP) != 0, E.prec == RP_FAST};
    gen_vtx(E, x, t, draws, mesh, orders, stars, W, H, s);
}

/* The count of the units the lean kernels leave (the sky's slots, the
 * quads they handed to the clipper) and the recorded passes': LIST holds N
 * of them (*DN, when given, of at most NLIST), each through its C
 * function's whole path, two lanes a unit (lane/geomocc: a lane a
 * triangle, the unit's even lane its first). Each unit's count goes to
 * counts[X.g], and each triangle that makes any to a write's list: the sky's
 * and the section quads' to GW (its length GW_N[0]), the recorded units'
 * to GR (GW_N[1]), each written by its own kernel (generic_write<REC>):
 * the unit, its kind with the triangle (bit 4), the triangles before it
 * in the unit (bits 5 to 7: a fan holds at most 7) and its environment's
 * index in P (bits 8 on: the write's own binary search over P was a chain
 * of dependent loads a thread; lane/cliprep). */
__global__ void __launch_bounds__(GBLOCK)
generic_count(const envp *P, int n, ulist list, uint32_t nlist, const uint32_t *dn,
              const ddraw *draws, const int32_t *mesh, const int32_t *orders, const float *stars, recs R, int W, int H,
              uint32_t *counts, xunit *gw, xunit *gr, xunit *gq, uint32_t *gw_n)
{
    RC_TAB(tab);
    uint32_t lim = dn && *dn < nlist ? *dn : nlist, lane = threadIdx.x & 31;
    for (uint32_t b = (blockIdx.x * blockDim.x + threadIdx.x) & ~31u; b < 2 * lim; b += gridDim.x * blockDim.x) {
        uint32_t p = b + lane;
        int t = (int)(p & 1);
        xunit x = {0, 0, 0, 0};
        uint32_t c = 0, ei = 0, le = 0;
        if (p < 2 * lim) {
            x = unit_of(list, p >> 1);
            ei = (uint32_t)env_of(P, n, x.g);
            const envp &E = P[ei];
            float *T = tab + threadIdx.x;
            c = x.kind == XU_SKY || x.kind == XU_QUAD ? gen_vtx_count(E, x, t, draws, mesh, orders, stars, W, H, T, &le)
                                                      : gen_rec_count(E, x, t, R, W, H, T);
        }
        uint32_t c0 = __shfl_sync(0xffffffffu, c, lane & ~1u), c1 = __shfl_sync(0xffffffffu, c, lane | 1u);
        if (p < 2 * lim && !t) counts[x.g] = c0 + c1;
        bool rec = x.kind != XU_SKY && x.kind != XU_QUAD;
        xunit e = {x.g, x.kind | (uint32_t)t << 4 | (t ? c0 : 0) << 5 | ei << 8, x.a, x.b};
        /* quads_clip's: the triangle, its fan triangles (bits 1 and 2: fan
         * triangles 1 and 2) and the triangles before them in the unit
         * (bits 3 on), from GQ down */
        if (le) e = xunit{x.g, (uint32_t)t | (le >> 1) << 1 | (t ? c0 : 0) << 3, x.a, x.b};
#pragma unroll
        for (int k = 0; k < 3; ++k) {
            const bool mine = c != 0 && (k == 2 ? le != 0 : !le && rec == (k == 1));
            unsigned bal = __ballot_sync(0xffffffffu, mine);
            if (!bal) continue;
            uint32_t base = 0;
            if (lane == 0) base = atomicAdd(gw_n + k, (uint32_t)__popc(bal));
            base = __shfl_sync(0xffffffffu, base, 0) + __popc(bal & ((1u << lane) - 1));
            if (mine) {
                if (k == 2) gq[-(int64_t)base] = e;
                else (k ? gr : gw)[base] = e;
            }
        }
    }
}

/* The write: a triangle of a unit a thread, from the count's list (its
 * length the device's, *DN, at most NLIST), those whose unit is in [u0, u1)
 * written from their unit's offset less TRI_BASE (and the triangles before
 * them in the unit) on. Each unit writes at its own offsets, so the list's
 * order changes no byte. */
template <bool REC>
__global__ void __launch_bounds__(GBLOCK)
generic_write(const envp *P, int n, const xunit *list, uint32_t nlist, const uint32_t *dn, uint32_t u0, uint32_t u1,
              const ddraw *draws, const int32_t *mesh, const int32_t *orders, const float *stars, recs R, int W, int H,
              const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles)
{
    RC_TAB(tab);
    uint32_t lim = min(*dn, nlist);
    for (uint32_t i = blockIdx.x * blockDim.x + threadIdx.x; i < lim; i += gridDim.x * blockDim.x) {
        xunit x = list[i];
        if (x.g < u0 || x.g >= u1) continue;
        uint32_t o0 = offsets[x.g];
        /* past the arena (one_trip's squash) nothing is written */
        if (offsets[x.g + 1] == o0) continue;
        int t = (int)(x.kind >> 4 & 1);
        uint32_t at = o0 - tri_base + (x.kind >> 5 & 7);
        const envp &E = P[x.kind >> 8];
        x.kind &= 15;
        if constexpr (REC) gen_rec_write(E, x, t, R, W, H, at, out, tiles, tab + threadIdx.x);
        else gen_vtx_write(E, x, t, draws, mesh, orders, stars, W, H, at, out, tiles, tab + threadIdx.x);
    }
}

template __global__ void generic_write<false>(const envp *P, int n, const xunit *list, uint32_t nlist, const uint32_t *dn, uint32_t u0, uint32_t u1, const ddraw *draws, const int32_t *mesh, const int32_t *orders, const float *stars, recs R, int W, int H, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);
template __global__ void generic_write<true>(const envp *P, int n, const xunit *list, uint32_t nlist, const uint32_t *dn, uint32_t u0, uint32_t u1, const ddraw *draws, const int32_t *mesh, const int32_t *orders, const float *stars, recs R, int W, int H, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);
