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
static __device__ void make_vertex(const envp &P, const int32_t *raw, int cx, int s, int cz, int flags, vtx &v)
{
    memset(&v, 0, sizeof v);
    int bx = cx * 16, by = s * 16, bz = cz * 16;
    float pos[4];
    cam_pos(P, raw, bx, by, bz, pos);
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

/* raster.c interpolate */
static __device__ void interpolate(const vtx &a, const vtx &b, float t, vtx &v)
{
    v = a;
    for (int i = 0; i < 4; ++i) {
        v.clip[i] = a.clip[i] + t * (b.clip[i] - a.clip[i]);
        v.c[i] = a.c[i] + t * (b.c[i] - a.c[i]);
    }
    for (int i = 0; i < 2; ++i) v.l[i] = a.l[i] + t * (b.l[i] - a.l[i]);
    v.fog = a.fog + t * (b.fog - a.fog);
    v.u = a.u + t * (b.u - a.u);
    v.v = a.v + t * (b.v - a.v);
}

/* ------------------------------------------------------------ sinks */

/* a pass's triangles counted (from the count's vertices, cvx), or written */
struct count_sink {
    static constexpr bool WRITE = false;
    uint32_t n;
    __device__ void emit(const cvx *, int, float, int, int, int, int) { ++n; }
};

struct write_sink {
    static constexpr bool WRITE = true;
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
         * swapped and the area negated */
        int k[3] = {0, 1, 2};
        if (area < 0) { k[1] = 2; k[2] = 1; area = -area; }
        for (int i = 0; i < 3; ++i) {
            const vtx &v = t[k[i]];
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
        o.area = rcp && !(mode & 15) ? 1.0f / area : area;
        o.x0 = (short)x0; o.x1 = (short)x1; o.y0 = (short)y0; o.y1 = (short)y1;
        o.mode = (uint32_t)mode;
        tiles[at] = (uint32_t)((x1 / TILE - x0 / TILE + 1) * (y1 / TILE - y0 / TILE + 1));
        ++at;
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

/* raster.c clip_triangle, over the write's vertices (vtx) or the count's
 * (cvx) */
template <class V, class Sink>
static __device__ void clip_tri(int W, int H, const V *in, int mode, Sink &sink)
{
    {
        int inside = 1;
        for (int plane = 0; plane < 6 && inside; ++plane) {
            int axis = plane / 2;
            float sign = plane & 1 ? -1.0f : 1.0f;
            for (int i = 0; i < 3; ++i)
                if (!(in[i].clip[3] - sign * in[i].clip[axis] >= 0)) { inside = 0; break; }
        }
        if (inside) {
            V a[3] = {in[0], in[1], in[2]};
            for (int i = 0; i < 3; ++i) screen_vertex(W, H, a[i]);
            emit_tri(W, H, a, mode, sink);
            return;
        }
    }
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
            if ((dp >= 0) != (dq >= 0)) interpolate(p, q, dp / (dp - dq), b[m++]);
        }
        n = m;
        if (n < 3) return;
        a = b;
        V *t = b; b = spare; spare = t;
    }
    /* no plane cut (all inside): the input, copied */
    V *f = (V *)a;
    if (a == in) { for (int i = 0; i < 3; ++i) b[i] = in[i]; f = b; }
    for (int i = 0; i < n; ++i) screen_vertex(W, H, f[i]);
    for (int i = 1; i + 1 < n; ++i) {
        V t[3] = {f[0], f[i], f[i + 1]};
        emit_tri(W, H, t, mode, sink);
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

/* A unit's NT triangles of Q through clip_triangle, in order: a quad (NT 2)
 * as raster.c draws every one, {q0, q1, q3} then {q1, q2, q3}; a triangle
 * (NT 1) {q0, q1, q3}. The one place a pass clips (each unit's own call,
 * inlined, made the generic kernel the largest function the build compiled:
 * 7.7 s of ptxas; lane/renderbuild). */
template <class Sink>
static __device__ __forceinline__ void clip_unit(int W, int H, const vtx *q, int nt, int mode, Sink &sink)
{
#pragma unroll 1
    for (int t = 0; t < nt; ++t) {
        vtx a[3] = {t ? q[1] : q[0], t ? q[2] : q[1], q[3]};
        clip_tri_of(W, H, a, mode, sink);
    }
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

/* sky slot U's vertices Q and its mode: the triangles it draws (clip_unit), 0
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

/* a section quad (raster.c live_draw_section): quad I of draw D's vertices
 * and mode */
static __device__ __forceinline__ int quad_verts(const envp &P, const ddraw &d, const int32_t *mesh,
                                                 const int32_t *orders, uint32_t i, vtx *q, int *mode)
{
    int offset = d.order >= 0 ? orders[d.order + i] : (int)i * 4;
    for (int j = 0; j < 4; ++j)
        make_vertex(P, mesh + d.mesh + (int64_t)(offset + j) * 8, d.cx, d.s, d.cz, d.flags, q[j]);
    *mode = d.water ? 32 : 0;
    return 2;
}

/* the vertex half: unit X of environment E (a sky slot or a clipped section
 * quad), its triangles counted into counts[X.g] or written from
 * offsets[X.g] - tri_base on */
template <bool WRITE>
static __device__ __forceinline__ void gen_vtx(const envp &E, const xunit &x, const ddraw *draws, const int32_t *mesh,
                                               const int32_t *orders, const float *stars, int W, int H, uint32_t *counts,
                                               const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles)
{
    vtx q[4];
    int mode = 0;
    int nt = x.kind == XU_SKY ? sky_verts(E, stars, (int)x.a, q, &mode)
                              : quad_verts(E, draws[x.a], mesh, orders, x.b, q, &mode);
    if constexpr (WRITE) {
        write_sink s{out, tiles, offsets[x.g] - tri_base, (W + TILE - 1) / TILE, (E.prec & RP_RCP) != 0, E.prec == RP_FAST};
        clip_unit(W, H, q, nt, mode, s);
    } else {
        count_sink s{0};
        clip_unit(W, H, q, nt, mode, s);
        counts[x.g] = s.n;
    }
}

/* The units the lean kernel leaves (the sky's slots, the quads it handed
 * to the clipper) and the recorded passes', each through its C function's
 * whole path; LIST holds N of them, those whose global unit is in [u0, u1)
 * run. */
template <bool WRITE>
__device__ __forceinline__ void generic_one(const envp *P, int n, const xunit &x, uint32_t u0, uint32_t u1,
          const ddraw *draws, const int32_t *mesh, const int32_t *orders, const float *stars, const recs &R, int W, int H,
          uint32_t *counts, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles)
{
    uint32_t g = x.g;
    if (g < u0 || g >= u1) return;
    /* the write: a unit the count found no triangle in writes none */
    if (WRITE && offsets[g + 1] == offsets[g]) return;
    const envp &E = P[env_of(P, n, g)];
    if (x.kind == XU_SKY || x.kind == XU_QUAD)
        gen_vtx<WRITE>(E, x, draws, mesh, orders, stars, W, H, counts, offsets, tri_base, out, tiles);
    else
        gen_rec<WRITE>(E, x, R, W, H, counts, offsets, tri_base, out, tiles);
}

template <bool WRITE>
__global__ void __launch_bounds__(32)
generic(const envp *P, int n, const xunit *list, uint32_t nlist, const uint32_t *dn, uint32_t u0, uint32_t u1,
          const ddraw *draws, const int32_t *mesh, const int32_t *orders, const float *stars, recs R, int W, int H,
          uint32_t *counts, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles)
{
    /* dn: the list's length as the device counted it (nlist bounds it; the
     * grid may be smaller than the list: each thread walks it) */
    uint32_t lim = dn && *dn < nlist ? *dn : nlist;
    if constexpr (!WRITE) {
        for (uint32_t i = blockIdx.x * blockDim.x + threadIdx.x; i < lim; i += gridDim.x * blockDim.x)
            generic_one<WRITE>(P, n, list[i], u0, u1, draws, mesh, orders, stars, R, W, H, counts, offsets, tri_base, out, tiles);
    } else {
        /* the write: the units with a triangle to write, gathered 32 at a
         * time (the block is one warp) and written a lane each. Taken in
         * list order, 7 of a warp's 32 lanes had one (lane/clipclimb); each
         * unit writes at its own offsets, so the order changes no byte. */
        __shared__ xunit q[64];
        int lane = threadIdx.x;
        uint32_t qn = 0;
        for (uint32_t b = blockIdx.x * 32; b < lim; b += gridDim.x * 32) {
            uint32_t i = b + lane;
            xunit x;
            bool want = false;
            if (i < lim) {
                x = list[i];
                want = x.g >= u0 && x.g < u1 && offsets[x.g + 1] != offsets[x.g];
            }
            unsigned bal = __ballot_sync(0xffffffffu, want);
            if (want) q[qn + __popc(bal & ((1u << lane) - 1))] = x;
            qn += __popc(bal);
            __syncwarp();
            if (qn >= 32) {
                generic_one<WRITE>(P, n, q[lane], u0, u1, draws, mesh, orders, stars, R, W, H, counts, offsets, tri_base, out, tiles);
                __syncwarp();
                if ((uint32_t)lane < qn - 32) q[lane] = q[32 + lane];
                qn -= 32;
                __syncwarp();
            }
        }
        if ((uint32_t)lane < qn)
            generic_one<WRITE>(P, n, q[lane], u0, u1, draws, mesh, orders, stars, R, W, H, counts, offsets, tri_base, out, tiles);
    }
}

template __global__ void generic<true>(const envp *P, int n, const xunit *list, uint32_t nlist, const uint32_t *dn, uint32_t u0, uint32_t u1, const ddraw *draws, const int32_t *mesh, const int32_t *orders, const float *stars, recs R, int W, int H, uint32_t *counts, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);
template __global__ void generic<false>(const envp *P, int n, const xunit *list, uint32_t nlist, const uint32_t *dn, uint32_t u0, uint32_t u1, const ddraw *draws, const int32_t *mesh, const int32_t *orders, const float *stars, recs R, int W, int H, uint32_t *counts, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);
