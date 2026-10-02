/* The geometry kernels' shared device code (lane/renderbuild): what more
 * than one of their units (geom_quads.cu, geom_equads.cu, geom_gen.cu,
 * geom_rec.cu) uses, each unit compiling its own copy, and the generic
 * kernel's recorded half, which geom_gen.cu calls in geom_rec.cu.
 * Every function below that computes a float is a port of the raster.c,
 * raster_sky2.c, raster_entities.c, raster_overlay.c or raster_tileent.c
 * function it names, operation for operation, so the build's --fmad=false,
 * -prec-div=true and -prec-sqrt=true give the C engine's bits
 * (csrc/cuda/render/mirrors.txt lists them for the mirror rule: a change
 * to one of those C functions needs its port here). */
#ifndef NETHERITE_RENDER_GEOM_CUH
#define NETHERITE_RENDER_GEOM_CUH

#include "dev.cuh"

typedef struct entity_clip_vertex evtx;

/* A count pass's vertex: what a clipper's and a setup's decisions read (the
 * clip coordinates, then the screen position), computed by the same
 * operations as the full vertex's, so a count pass keeps exactly the
 * triangles its write pass writes without their shading. */
struct cvx { float clip[4]; float x, y, sx, sy; };

/* interpolate's clip coordinates (raster.c's and raster_entities.c's) */
static __device__ __forceinline__ void interpolate(const cvx &a, const cvx &b, float t, cvx &v)
{
    for (int i = 0; i < 4; ++i) v.clip[i] = a.clip[i] + t * (b.clip[i] - a.clip[i]);
}

/* screen_vertex's screen position (raster.c's and raster_entities.c's) */
static __device__ __forceinline__ void screen_vertex(int W, int H, cvx &v)
{
    float w = v.clip[3];
    v.x = (v.clip[0] / w * 0.5f + 0.5f) * W;
    v.y = (0.5f - v.clip[1] / w * 0.5f) * H;
    v.sx = rintf((v.x - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v.sy = rintf((v.y - 0.5f) * 256.0f) / 256.0f + 0.5f;
}


/* the environment of global unit g */
/* raster.c make_vertex's position (raster_prec.h RP_XFORM: the camera
 * offset in float) */
static __device__ __forceinline__ void cam_pos(const envp &P, const int32_t *raw, int bx, int by, int bz, float pos[4])
{
    if (P.prec & RP_XFORM) {
        pos[0] = ((float)(bx - (bx & 1023)) - (float)P.cam[0]) + (float)(bx & 1023) +
                 (__int_as_float(raw[0]) - 8.0f) * 1.000001f + 8.0f;
        pos[1] = ((float)by - (float)P.cam[1]) + (__int_as_float(raw[1]) - 8.0f) * 1.000001f + 8.0f;
        pos[2] = ((float)(bz - (bz & 1023)) - (float)P.cam[2]) + (float)(bz & 1023) +
                 (__int_as_float(raw[2]) - 8.0f) * 1.000001f + 8.0f;
    } else {
        pos[0] = (float)((double)(bx - (bx & 1023)) - P.cam[0]) + (float)(bx & 1023) +
                 (__int_as_float(raw[0]) - 8.0f) * 1.000001f + 8.0f;
        pos[1] = (float)((double)by - P.cam[1]) + (__int_as_float(raw[1]) - 8.0f) * 1.000001f + 8.0f;
        pos[2] = (float)((double)(bz - (bz & 1023)) - P.cam[2]) + (float)(bz & 1023) +
                 (__int_as_float(raw[2]) - 8.0f) * 1.000001f + 8.0f;
    }
    pos[3] = 1;
}

static __device__ __forceinline__ int env_of(const envp *P, int n, uint32_t g)
{
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (P[mid].unit_base <= g) lo = mid; else hi = mid - 1;
    }
    return lo;
}

/* The section quads, lean: raster.c clip_triangle's own decision for each
 * of a quad's two triangles, exactly: 0 every vertex inside every plane
 * (its fast path), 1 every vertex outside the first plane not all are
 * inside (Sutherland-Hodgman keeps nothing there, interpolating nothing),
 * 2 the clipper, which the generic kernel runs for the whole quad. */
static __device__ __forceinline__ int classify(const float *a, const float *b, const float *c)
{
    for (int plane = 0; plane < 6; ++plane) {
        int axis = plane / 2;
        float sign = plane & 1 ? -1.0f : 1.0f;
        int k = (a[3] - sign * a[axis] >= 0) + (b[3] - sign * b[axis] >= 0) + (c[3] - sign * c[axis] >= 0);
        if (k == 3) continue;
        return k == 0 ? 1 : 2;
    }
    return 0;
}

/* vertex I of Q as raster_entities.c holds it */
static __device__ __forceinline__ void cq_vertex(const cquad &q, const cextra *ex, int i, evtx &v)
{
    memset(&v, 0, sizeof v);
    const cvtx &c = q.v[i];
    for (int k = 0; k < 4; ++k) v.clip[k] = c.clip[k];
    v.fogcoord = c.fogcoord;
    v.u = c.u;
    v.v = c.v;
    v.light[0] = c.light[0];
    v.light[1] = c.light[1];
    v.diffuse = c.diffuse;
    if (q.extra >= 0) {
        const cextra &x = ex[q.extra];
        for (int k = 0; k < 4; ++k) v.color[k] = x.color[i][k];
        v.alpha = x.alpha[i];
    }
}

/* vertex I of Q for a count: its clip coordinates */
static __device__ __forceinline__ void cq_vertex(const cquad &q, int i, cvx &v)
{
    for (int k = 0; k < 4; ++k) v.clip[k] = q.v[i].clip[k];
}

/* raster_entities.c screen_vertex */
static __device__ __forceinline__ void e_screen(int W, int H, evtx &v)
{
    v.w = v.clip[3];
    v.x = (v.clip[0] / v.w * 0.5f + 0.5f) * W;
    v.y = (0.5f - v.clip[1] / v.w * 0.5f) * H;
    v.sx = rintf((v.x - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v.sy = rintf((v.y - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v.depth = v.clip[2] / v.w * 0.5f + 0.5f;
}

/* raster_entities.c screen_vertex's screen position, for a count */
static __device__ __forceinline__ void e_screen(int W, int H, cvx &v) { screen_vertex(W, H, v); }

/* where a recorded unit's triangles go: counted (the vertices V the count
 * reads), or written from AT on */
struct tcount {
    static constexpr bool WRITE = false;
    typedef cvx V;
    uint32_t n;
    __device__ tri *next(int, int, int, int) { ++n; return NULL; }
};
struct twrite {
    static constexpr bool WRITE = true;
    static constexpr bool VEC = false;  /* the record built in registers, stored as uint4s (twrite_vec) */
    typedef evtx V;
    uint32_t n;
    tri *out;
    uint32_t *tiles;
    uint32_t at;
    bool rcp;                           /* raster_prec.h RP_RCP: an entity triangle's 1 / w and 1 / area */
    bool pack;                          /* prec RP_FAST: its colour and light as halves (tri_ch, tri_lh) */
    __device__ tri *next(int x0, int x1, int y0, int y1)
    {
        ++n;
        tiles[at] = (uint32_t)((x1 / TILE - x0 / TILE + 1) * (y1 / TILE - y0 / TILE + 1));
        return &out[at++];
    }
};
/* the lean entity kernel's (equads): its record built in registers and
 * stored as uint4s (lane/coalesce: word stores 240 bytes from a
 * neighbour's met a sector each; the generic kernel's 255 registers
 * spilled the record) */
struct twrite_vec : twrite {
    static constexpr bool VEC = true;
};

/* raster_entities.c raster_triangle's setup: the culling, the winding its
 * flags keep, the box. V is evtx, or the count's cvx (the decisions read
 * the screen position alone, so the count is what the write makes). */
template <class V, class Sink>
static __device__ void e_tri(const V *in, const raster_obs_estate &st, uint32_t state_g, uint32_t tex_g,
                             int W, int H, Sink &sink)
{
    V v[3] = {in[0], in[1], in[2]};
    float area = edge_xy(v[0].x, v[0].y, v[1].x, v[1].y, v[2].x, v[2].y);
    if (st.cull_front) {
        if (!(area < 0.0f)) return;
        V t = v[0]; v[0] = v[1]; v[1] = t;
        area = -area;
    }
    else if (area == 0.0f) return;
    else if (area < 0.0f) {
        if (!st.two_sided) return;
        V t = v[1]; v[1] = v[2]; v[2] = t;
        area = -area;
    }
    float minx = fminf(v[0].sx, fminf(v[1].sx, v[2].sx)), maxx = fmaxf(v[0].sx, fmaxf(v[1].sx, v[2].sx));
    float miny = fminf(v[0].sy, fminf(v[1].sy, v[2].sy)), maxy = fmaxf(v[0].sy, fmaxf(v[1].sy, v[2].sy));
    int x0 = f2i(fmaxf(0, floorf(minx))), x1 = f2i(fminf((float)(W - 1), ceilf(maxx)));
    int y0 = f2i(fmaxf(0, floorf(miny))), y1 = f2i(fminf((float)(H - 1), ceilf(maxy)));
    if (x0 > x1 || y0 > y1) return;
    if constexpr (!Sink::WRITE) sink.next(x0, x1, y0, y1);
    else {
    union { tri t; uint4 q[TRI_QUADS]; } ob;
    tri *dst = sink.next(x0, x1, y0, y1), *o = Sink::VEC ? &ob.t : dst;
    if constexpr (Sink::VEC) o->pad[0] = o->pad[1] = o->pad[2] = 0;
    for (int i = 0; i < 3; ++i) {
        o->x[i] = v[i].x; o->y[i] = v[i].y; o->sx[i] = v[i].sx; o->sy[i] = v[i].sy;
        o->w[i] = sink.rcp ? 1.0f / v[i].w : v[i].w;
        o->d[i] = v[i].depth; o->u[i] = v[i].u; o->v[i] = v[i].v;
        if (sink.pack) {
            tri_ch(*o)[i * 2] = __floats2half2_rn(v[i].color[0], v[i].color[1]);
            tri_ch(*o)[i * 2 + 1] = __floats2half2_rn(v[i].color[2], v[i].color[3]);
            tri_lh(*o)[i] = __floats2half2_rn(v[i].light[0], v[i].light[1]);
        } else {
            for (int c = 0; c < 4; ++c) o->c[i][c] = v[i].color[c];
            o->l[i][0] = v[i].light[0]; o->l[i][1] = v[i].light[1];
        }
        o->fog[i] = v[i].fogcoord; o->dif[i] = v[i].diffuse; o->al[i] = v[i].alpha;
    }
    o->area = sink.rcp ? 1.0f / area : area;
    o->x0 = (short)x0; o->x1 = (short)x1; o->y0 = (short)y0; o->y1 = (short)y1;
    o->mode = (uint32_t)TK_ENTITY << 8;
    o->aux = state_g;
    o->tex = tex_g;
    if constexpr (Sink::VEC)
        for (int j = 0; j < TRI_QUADS; ++j) ((uint4 *)dst)[j] = ob.q[j];
    }
}

/* the generic kernel's (geom_gen.cu) recorded half (geom_rec.cu):
 * unit X of environment E, its triangles counted into counts[X.g] or written
 * from offsets[X.g] - tri_base on */
template <bool WRITE>
__device__ void gen_rec(const envp &E, const xunit &x, const recs &R, int W, int H, uint32_t *counts,
                        const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);

#endif
