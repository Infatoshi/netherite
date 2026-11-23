/* The generic geometry kernel's recorded half (geom_gen.cu calls it;
 * lane/renderbuild's split of the geometry kernels): the recorded passes'
 * units (the entity quads the lean kernel handed to the clipper, the
 * cracks, the end portals, the lines, the depth clears), each through its C
 * function's setup. A count pass runs the entity clipper's decisions over
 * the clip coordinates alone (cvx).
 * Every function below that computes a float is a port of the raster.c,
 * raster_sky2.c, raster_entities.c, raster_overlay.c or raster_tileent.c
 * function it names, operation for operation, so the build's --fmad=false,
 * -prec-div=true and -prec-sqrt=true give the C engine's bits
 * (csrc/cuda/render/mirrors.txt lists them for the mirror rule: a change
 * to one of those C functions needs its port here). */
#include "geom.cuh"
#include "rclip.cuh"

/* raster_entities.c interpolate (the colour only under vertex_color) */
static __device__ void e_interpolate(const evtx &a, const evtx &b, float t, int vertex_color, evtx &v)
{
    v = a;
    for (int i = 0; i < 4; ++i) v.clip[i] = a.clip[i] + t * (b.clip[i] - a.clip[i]);
    for (int i = 0; i < 2; ++i) v.light[i] = a.light[i] + t * (b.light[i] - a.light[i]);
    v.diffuse = a.diffuse + t * (b.diffuse - a.diffuse);
    if (vertex_color)
        for (int i = 0; i < 4; ++i) v.color[i] = a.color[i] + t * (b.color[i] - a.color[i]);
    v.fogcoord = a.fogcoord + t * (b.fogcoord - a.fogcoord);
    v.u = a.u + t * (b.u - a.u);
    v.v = a.v + t * (b.v - a.v);
    v.alpha = a.alpha + t * (b.alpha - a.alpha);
}

/* e_interpolate for the write's vertices, the clip coordinates for the count's */
static __device__ __forceinline__ void e_lerp(const evtx &a, const evtx &b, float t, int vertex_color, evtx &v)
{
    e_interpolate(a, b, t, vertex_color, v);
}
static __device__ __forceinline__ void e_lerp(const cvx &a, const cvx &b, float t, int, cvx &v) { interpolate(a, b, t, v); }

/* vertex I of Q: the write's whole, the count's clip coordinates */
static __device__ __forceinline__ void cq_of(const cquad &q, const cextra *ex, int i, evtx &v) { cq_vertex(q, ex, i, v); }
static __device__ __forceinline__ void cq_of(const cquad &q, const cextra *, int i, cvx &v) { cq_vertex(q, i, v); }

/* polygon vertex ID of R as the count's vertex (raster_entities.c
 * screen_vertex's screen position) */
static __device__ __forceinline__ void e_rc_cvx(int W, int H, const rcpoly &r, int id, cvx &v)
{
    rc_pos(r, id, v.clip);
    e_screen(W, H, v);
}

/* e_tri's setup over a clipped triangle's screen positions, for the write
 * by recorded operations: its decisions (the culling, the winding its
 * flags keep, the box), then its record but the replayed fields (e_rc_fields).
 * The order of its vertices: 0 as given, 1 v0 and v1 swapped, 2 v1 and v2;
 * -1 nothing written. */
static __device__ __forceinline__ int e_tri_pos(const cvx *in, const raster_obs_estate &st, uint32_t state_g,
                                                uint32_t tex_g, int W, int H, twrite &sink)
{
    cvx v[3] = {in[0], in[1], in[2]};
    int ord = 0;
    float area = edge_xy(v[0].x, v[0].y, v[1].x, v[1].y, v[2].x, v[2].y);
    if (st.cull_front) {
        if (!(area < 0.0f)) return -1;
        cvx t = v[0]; v[0] = v[1]; v[1] = t;
        area = -area;
        ord = 1;
    }
    else if (area == 0.0f) return -1;
    else if (area < 0.0f) {
        if (!st.two_sided) return -1;
        cvx t = v[1]; v[1] = v[2]; v[2] = t;
        area = -area;
        ord = 2;
    }
    float minx = fminf(v[0].sx, fminf(v[1].sx, v[2].sx)), maxx = fmaxf(v[0].sx, fmaxf(v[1].sx, v[2].sx));
    float miny = fminf(v[0].sy, fminf(v[1].sy, v[2].sy)), maxy = fmaxf(v[0].sy, fmaxf(v[1].sy, v[2].sy));
    int x0 = f2i(fmaxf(0, floorf(minx))), x1 = f2i(fminf((float)(W - 1), ceilf(maxx)));
    int y0 = f2i(fmaxf(0, floorf(miny))), y1 = f2i(fminf((float)(H - 1), ceilf(maxy)));
    if (x0 > x1 || y0 > y1) return -1;
    tri *o = sink.next(x0, x1, y0, y1);
    for (int i = 0; i < 3; ++i) {
        o->x[i] = v[i].x; o->y[i] = v[i].y; o->sx[i] = v[i].sx; o->sy[i] = v[i].sy;
        const float w = v[i].clip[3];
        o->w[i] = sink.rcp ? 1.0f / w : w;
        o->d[i] = v[i].clip[2] / w * 0.5f + 0.5f;
    }
    o->area = sink.rcp ? 1.0f / area : area;
    o->x0 = (short)x0; o->x1 = (short)x1; o->y0 = (short)y0; o->y1 = (short)y1;
    o->mode = (uint32_t)TK_ENTITY << 8;
    o->aux = state_g;
    o->tex = tex_g;
    return ord;
}

/* the fields of the write's triangles from R's fan that e_tri_pos wrote
 * (EM: the fan's triangles, ORD their vertex orders at 2 bits each; the
 * first at AT0): each attribute of every polygon vertex replayed as
 * e_interpolate made it (rc_replay; the colour, unless vertex_color, the
 * edge's start's), a group at a time (G: 0 u, 1 v, 2 fog, 3 the diffuse
 * shade, 4 the alpha, 5 the light, 6 the colour's r and g, 7 its b and a),
 * and stored as e_tri stores it */
static __device__ __forceinline__ void e_rc_fields(const evtx *in, const rcpoly &r, int vertex_color, const twrite &sink,
                                                   uint32_t at0, uint32_t em, uint32_t ord)
{
    const int i0 = rc_id(r, 0);
#pragma unroll
    for (int g = 0; g < 8; ++g) {
#pragma unroll
        for (int i = 0; i < 3; ++i) {
            const evtx &v = in[i];
            rc_set(r, 0, i, g == 0 ? v.u : g == 1 ? v.v : g == 2 ? v.fogcoord : g == 3 ? v.diffuse : g == 4 ? v.alpha
                          : g == 5 ? v.light[0] : g == 6 ? v.color[0] : v.color[2]);
            if (g >= 5) rc_set(r, RC_S, i, g == 5 ? v.light[1] : g == 6 ? v.color[1] : v.color[3]);
        }
        if (g < 6 || vertex_color) {
            rc_replay(r, 0);
            if (g >= 5) rc_replay(r, RC_S);
        } else {
            rc_replay_start(r, 0);
            rc_replay_start(r, RC_S);
        }
        uint32_t at = at0;
        for (int i = 1; i + 1 < r.n; ++i) {
            if (!(em >> i & 1)) continue;
            tri &o = sink.out[at++];
            const int od = (int)(ord >> (2 * i) & 3);
            const int f[3] = {i0, rc_id(r, i), rc_id(r, i + 1)};
            const int id[3] = {od == 1 ? f[1] : f[0], od == 1 ? f[0] : od == 2 ? f[2] : f[1], od == 2 ? f[1] : f[2]};
#pragma unroll
            for (int j = 0; j < 3; ++j) {
                const float x = rc_val(r, 0, id[j]);
                if (g == 0) o.u[j] = x;
                else if (g == 1) o.v[j] = x;
                else if (g == 2) o.fog[j] = x;
                else if (g == 3) o.dif[j] = x;
                else if (g == 4) o.al[j] = x;
                else {
                    const float y = rc_val(r, RC_S, id[j]);
                    if (g == 5) {
                        if (sink.pack) tri_lh(o)[j] = __floats2half2_rn(x, y);
                        else { o.l[j][0] = x; o.l[j][1] = y; }
                    }
                    else if (sink.pack) tri_ch(o)[j * 2 + g - 6] = __floats2half2_rn(x, y);
                    else { o.c[j][2 * (g - 6)] = x; o.c[j][2 * (g - 6) + 1] = y; }
                }
            }
        }
    }
}

/* R's fan (clip_and_draw's: triangle i of the polygon's vertices 0, i and
 * i + 1) through e_tri: counted, or written (e_tri_pos, then the replayed
 * fields) */
template <class Sink>
static __device__ __forceinline__ void e_rc_fan(const typename Sink::V *in, const rcpoly &r, const raster_obs_estate &st,
                                                uint32_t state_g, uint32_t tex_g, int W, int H, Sink &sink)
{
    cvx t[3];
    e_rc_cvx(W, H, r, rc_id(r, 0), t[0]);
    e_rc_cvx(W, H, r, rc_id(r, 1), t[1]);
    if constexpr (!Sink::WRITE) {
        for (int i = 1; i + 1 < r.n; ++i) {
            e_rc_cvx(W, H, r, rc_id(r, i + 1), t[2]);
            e_tri(t, st, state_g, tex_g, W, H, sink);
            t[1] = t[2];
        }
    } else {
        const uint32_t at0 = sink.at;
        uint32_t em = 0, ord = 0;
        for (int i = 1; i + 1 < r.n; ++i) {
            e_rc_cvx(W, H, r, rc_id(r, i + 1), t[2]);
            const int od = e_tri_pos(t, st, state_g, tex_g, W, H, sink);
            if (od >= 0) {
                em |= 1u << i;
                ord |= (uint32_t)od << (2 * i);
            }
            t[1] = t[2];
        }
        if (em) e_rc_fields(in, r, st.vertex_color, sink, at0, em, ord);
    }
}

/* raster_entities.c clip_and_draw: Sutherland-Hodgman whatever the
 * vertices (all inside, it keeps them as they are) */
template <class Sink>
static __device__ __forceinline__ void e_clip(const typename Sink::V *in, const raster_obs_estate &st, uint32_t state_g,
                                              uint32_t tex_g, int W, int H, Sink &sink, float *tab)
{
    typedef typename Sink::V V;
    /* the clipper by recorded operations (rclip.cuh); the arrays below
     * clip what it does not hold */
    {
        float c[3][4];
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 4; ++k) c[i][k] = in[i].clip[k];
        rcpoly r;
        const int k = rc_clip(c, tab, r);
        if (k == 0) return;
        if (k > 0) {
            e_rc_fan(in, r, st, state_g, tex_g, W, H, sink);
            return;
        }
    }
    /* the polygon and the next, swapped after each plane (not copied);
     * the input copied in first (read where it is, the caller's vertices
     * would live in local memory for the clipper above too) */
    V va[9], vb[9], *a = va, *b = vb;
    for (int i = 0; i < 3; ++i) va[i] = in[i];
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
         * (its distance the same operations), no modulo (geom_gen.cu
         * clip_tri's, lane/kernclimb) */
        V p = a[0];
        float dp = p.clip[3] - sign * p.clip[axis];
        for (int i = 0; i < n; ++i) {
            V q = a[i + 1 < n ? i + 1 : 0];
            float dq = q.clip[3] - sign * q.clip[axis];
            if (dp >= 0) b[m++] = p;
            if ((dp >= 0) != (dq >= 0)) e_lerp(p, q, dp / (dp - dq), st.vertex_color, b[m++]);
            p = q;
            dp = dq;
        }
        n = m;
        if (n < 3) return;
        V *t = a; a = b; b = t;
    }
    V *f = a;
    for (int i = 0; i < n; ++i) e_screen(W, H, f[i]);
    /* the fan, each vertex loaded once */
    V t[3] = {f[0], f[1], f[1]};
    for (int i = 1; i + 1 < n; ++i) {
        t[2] = f[i + 1];
        e_tri(t, st, state_g, tex_g, W, H, sink);
        t[1] = t[2];
    }
}

/* raster_entities.c draw_quad */
/* draw_quad's triangle T: {q0, q1, q3}, then {q1, q2, q3} (a thread each
 * since lane/geomocc) */
template <class Sink>
static __device__ void e_quad(const cquad &q, const cextra *ex, const raster_obs_estate &st, uint32_t state_g,
                              uint32_t tex_g, int t, int W, int H, Sink &sink, float *tab)
{
    typename Sink::V a[3];
    cq_of(q, ex, t ? 1 : 0, a[0]); cq_of(q, ex, t ? 2 : 1, a[1]); cq_of(q, ex, 3, a[2]);
    e_clip(a, st, state_g, tex_g, W, H, sink, tab);
}

/* raster_overlay.c crack_triangle's setup */
template <class Sink>
static __device__ void crack_setup(const raster_obs_crack &k, uint32_t crack_g, int W, int H, Sink &sink)
{
    const float *a = k.p[0], *b = k.p[1], *c = k.p[2];     /* x y z w u v f */
    if (a[3] <= 0 || b[3] <= 0 || c[3] <= 0) return;
    float area = edge_xy(a[0], a[1], b[0], b[1], c[0], c[1]);
    if (area <= 0) return;
    int x0 = f2i(fmaxf(0, floorf(fminf(a[0], fminf(b[0], c[0])))));
    int x1 = f2i(fminf((float)(W - 1), ceilf(fmaxf(a[0], fmaxf(b[0], c[0])))));
    int y0 = f2i(fmaxf(0, floorf(fminf(a[1], fminf(b[1], c[1])))));
    int y1 = f2i(fminf((float)(H - 1), ceilf(fmaxf(a[1], fmaxf(b[1], c[1])))));
    if (x0 > x1 || y0 > y1) return;
    tri *o = sink.next(x0, x1, y0, y1);
    if (!o) return;
    for (int i = 0; i < 3; ++i) {
        const float *p = k.p[i];
        o->x[i] = p[0]; o->y[i] = p[1]; o->d[i] = p[2]; o->w[i] = p[3];
        o->u[i] = p[4]; o->v[i] = p[5]; o->fog[i] = p[6];
    }
    o->area = area;
    o->x0 = (short)x0; o->x1 = (short)x1; o->y0 = (short)y0; o->y1 = (short)y1;
    o->mode = (uint32_t)TK_CRACK << 8;
    o->aux = crack_g;
}

/* raster_tileent.c portal_triangle's setup; TEX_G its texture */
template <class Sink>
static __device__ void portal_setup(const raster_obs_portal &p, uint32_t portal_g, uint32_t tex_g, int W, int H,
                                    Sink &sink)
{
    int k[3] = {0, 1, 2};
    const float *v0 = p.v[0], *v1 = p.v[1], *v2 = p.v[2];   /* x y at 4 5, sx sy at 6 7 */
    float area = edge_xy(v0[4], v0[5], v1[4], v1[5], v2[4], v2[5]);
    if (area == 0.0f) return;
    if (area < 0.0f) {
        if (p.cull) return;
        k[1] = 2; k[2] = 1;
        area = -area;
    }
    const float *a = p.v[k[0]], *b = p.v[k[1]], *c = p.v[k[2]];
    float minx = fminf(a[6], fminf(b[6], c[6])), maxx = fmaxf(a[6], fmaxf(b[6], c[6]));
    float miny = fminf(a[7], fminf(b[7], c[7])), maxy = fmaxf(a[7], fmaxf(b[7], c[7]));
    int x0 = f2i(fmaxf(0, floorf(minx))), x1 = f2i(fminf((float)(W - 1), ceilf(maxx)));
    int y0 = f2i(fmaxf(0, floorf(miny))), y1 = f2i(fminf((float)(H - 1), ceilf(maxy)));
    if (x0 > x1 || y0 > y1) return;
    tri *o = sink.next(x0, x1, y0, y1);
    if (!o) return;
    for (int i = 0; i < 3; ++i) {
        const float *f = p.v[k[i]];
        o->x[i] = f[4]; o->y[i] = f[5]; o->sx[i] = f[6]; o->sy[i] = f[7]; o->w[i] = f[8]; o->d[i] = f[9];
        o->u[i] = f[10]; o->v[i] = f[11]; o->dif[i] = f[12]; o->fog[i] = f[13];
    }
    o->area = area;
    o->x0 = (short)x0; o->x1 = (short)x1; o->y0 = (short)y0; o->y1 = (short)y1;
    o->mode = (uint32_t)TK_PORTAL << 8;
    o->aux = portal_g;
    o->tex = tex_g;
}

/* raster_overlay.c line's setup: its early outs and the box it walks; the
 * ends (x y z w f) go to x, y, d, w, fog [0] and [1] */
template <class Sink>
static __device__ void line_setup(const raster_obs_line &l, uint32_t line_g, int W, int H, Sink &sink)
{
    const float *a = l.a, *b = l.b;
    if (a[3] <= 0 || b[3] <= 0) return;
    float dx = b[0] - a[0], dy = b[1] - a[1], len2 = dx * dx + dy * dy;
    if (len2 < 1e-12f) return;
    int x0 = f2i(fmaxf(0, floorf(fminf(a[0], b[0]) - 1.5f)));
    int x1 = f2i(fminf((float)(W - 1), ceilf(fmaxf(a[0], b[0]) + 1.5f)));
    int y0 = f2i(fmaxf(0, floorf(fminf(a[1], b[1]) - 1.5f)));
    int y1 = f2i(fminf((float)(H - 1), ceilf(fmaxf(a[1], b[1]) + 1.5f)));
    if (x0 > x1 || y0 > y1) return;
    tri *o = sink.next(x0, x1, y0, y1);
    if (!o) return;
    for (int i = 0; i < 2; ++i) {
        const float *p = i ? b : a;
        o->x[i] = p[0]; o->y[i] = p[1]; o->d[i] = p[2]; o->w[i] = p[3]; o->fog[i] = p[4];
    }
    o->x0 = (short)x0; o->x1 = (short)x1; o->y0 = (short)y0; o->y1 = (short)y1;
    o->mode = (uint32_t)TK_LINE << 8;
    o->aux = line_g;
}

/* a depth clear, or (KIND TK_SNAP) the place the returned depth is taken */
template <class Sink>
static __device__ void clear_setup(int W, int H, Sink &sink, int kind = TK_CLEAR)
{
    tri *o = sink.next(0, W - 1, 0, H - 1);
    if (!o) return;
    o->x0 = 0; o->x1 = (short)(W - 1); o->y0 = 0; o->y1 = (short)(H - 1);
    o->mode = (uint32_t)kind << 8;
}

/* unit X's path by its kind */
/* triangle T of a recorded unit: an entity quad has two, the others one */
template <class Sink>
static __device__ __forceinline__ void rec_unit(const envp &E, const xunit &x, int t, const recs &R, int W, int H, Sink &s,
                                                float *tab)
{
    if (x.kind == XU_EQUAD) {
        const cquad &q = R.equad[x.a];
        uint32_t sg = E.estate_base + (uint32_t)q.state;
        e_quad(q, R.eextra, R.estate[sg], sg, E.tex_base + (uint32_t)q.tex, t, W, H, s, tab);
    }
    else if (t) return;
    else if (x.kind == XU_CRACK) crack_setup(R.crack[x.a], x.a, W, H, s);
    else if (x.kind == XU_PORTAL) portal_setup(R.portal[x.a], x.a, E.tex_base + (uint32_t)R.portal[x.a].tex, W, H, s);
    else if (x.kind == XU_LINE) line_setup(R.line[x.a], x.a, W, H, s);
    else clear_setup(W, H, s, x.kind == XU_SNAP ? TK_SNAP : TK_CLEAR);
}

/* triangle T of recorded unit X of environment E: the triangles it makes
 * counted, or written from AT on */
__device__ uint32_t gen_rec_count(const envp &E, const xunit &x, int t, const recs &R, int W, int H, float *tab)
{
    tcount s{0};
    rec_unit(E, x, t, R, W, H, s, tab);
    return s.n;
}

__device__ void gen_rec_write(const envp &E, const xunit &x, int t, const recs &R, int W, int H, uint32_t at,
                              tri *out, uint32_t *tiles, float *tab)
{
    twrite s{0, out, tiles, at, (E.prec & RP_RCP) != 0, E.prec == RP_FAST};
    rec_unit(E, x, t, R, W, H, s, tab);
}
