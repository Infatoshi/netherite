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

/* raster_entities.c clip_and_draw: Sutherland-Hodgman whatever the
 * vertices (all inside, it keeps them as they are) */
template <class Sink>
static __device__ __noinline__ void e_clip(const typename Sink::V *in, const raster_obs_estate &st, uint32_t state_g,
                                           uint32_t tex_g, int W, int H, Sink &sink)
{
    typedef typename Sink::V V;
    /* the polygon and the next, swapped after each plane (not copied);
     * the first plane that cuts reads the input where it is */
    V va[9], vb[9], *b = va, *spare = vb;
    const V *a = in;
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
        for (int i = 0; i < n; ++i) {
            const V &p = a[i], &q = a[(i + 1) % n];
            float dp = p.clip[3] - sign * p.clip[axis];
            float dq = q.clip[3] - sign * q.clip[axis];
            if (dp >= 0) b[m++] = p;
            if ((dp >= 0) != (dq >= 0)) e_lerp(p, q, dp / (dp - dq), st.vertex_color, b[m++]);
        }
        n = m;
        if (n < 3) return;
        a = b;
        V *t = b; b = spare; spare = t;
    }
    /* no plane cut (all inside): the input, copied */
    V *f = (V *)a;
    if (a == in) { for (int i = 0; i < 3; ++i) b[i] = in[i]; f = b; }
    for (int i = 0; i < n; ++i) e_screen(W, H, f[i]);
    for (int i = 1; i + 1 < n; ++i) {
        V t[3] = {f[0], f[i], f[i + 1]};
        e_tri(t, st, state_g, tex_g, W, H, sink);
    }
}

/* raster_entities.c draw_quad */
template <class Sink>
static __device__ void e_quad(const cquad &q, const cextra *ex, const raster_obs_estate &st, uint32_t state_g,
                              uint32_t tex_g, int W, int H, Sink &sink)
{
    typename Sink::V a[3];
    cq_of(q, ex, 0, a[0]); cq_of(q, ex, 1, a[1]); cq_of(q, ex, 3, a[2]);
    e_clip(a, st, state_g, tex_g, W, H, sink);
    cq_of(q, ex, 1, a[0]); cq_of(q, ex, 2, a[1]); cq_of(q, ex, 3, a[2]);
    e_clip(a, st, state_g, tex_g, W, H, sink);
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
template <class Sink>
static __device__ __forceinline__ void rec_unit(const envp &E, const xunit &x, const recs &R, int W, int H, Sink &s)
{
    if (x.kind == XU_EQUAD) {
        const cquad &q = R.equad[x.a];
        uint32_t sg = E.estate_base + (uint32_t)q.state;
        e_quad(q, R.eextra, R.estate[sg], sg, E.tex_base + (uint32_t)q.tex, W, H, s);
    }
    else if (x.kind == XU_CRACK) crack_setup(R.crack[x.a], x.a, W, H, s);
    else if (x.kind == XU_PORTAL) portal_setup(R.portal[x.a], x.a, E.tex_base + (uint32_t)R.portal[x.a].tex, W, H, s);
    else if (x.kind == XU_LINE) line_setup(R.line[x.a], x.a, W, H, s);
    else clear_setup(W, H, s, x.kind == XU_SNAP ? TK_SNAP : TK_CLEAR);
}

template <bool WRITE>
__device__ void gen_rec(const envp &E, const xunit &x, const recs &R, int W, int H, uint32_t *counts,
                        const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles)
{
    if constexpr (WRITE) {
        twrite s{0, out, tiles, offsets[x.g] - tri_base, (E.prec & RP_RCP) != 0, E.prec == RP_FAST};
        rec_unit(E, x, R, W, H, s);
    } else {
        tcount s{0};
        rec_unit(E, x, R, W, H, s);
        counts[x.g] = s.n;
    }
}

template __device__ void gen_rec<true>(const envp &E, const xunit &x, const recs &R, int W, int H, uint32_t *counts, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);
template __device__ void gen_rec<false>(const envp &E, const xunit &x, const recs &R, int W, int H, uint32_t *counts, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);
