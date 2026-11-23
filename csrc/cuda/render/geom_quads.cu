/* The section quads' lean kernel (render.h, lane/renderbuild's split of the
 * geometry kernels): raster.c live_draw_section's quads whose triangles
 * clip_triangle keeps whole or drops whole, the rest handed to the generic
 * kernel (geom_gen.cu).
 * Every function below that computes a float is a port of the raster.c,
 * raster_sky2.c, raster_entities.c, raster_overlay.c or raster_tileent.c
 * function it names, operation for operation, so the build's --fmad=false,
 * -prec-div=true and -prec-sqrt=true give the C engine's bits
 * (csrc/cuda/render/mirrors.txt lists them for the mirror rule: a change
 * to one of those C functions needs its port here). */
#include "geom.cuh"
#include "rclip.cuh"

/* make_vertex's position half: clip coordinates and the fog distance */
static __device__ __forceinline__ void quad_pos(const envp &P, const ddraw &d, const int32_t *raw, float clip[4], float *fog)
{
    float pos[4];
    cam_pos(d, raw, pos);
    float eye[4];
    for (int r = 0; r < 4; ++r)
        eye[r] = P.mv[r] * pos[0] + P.mv[4 + r] * pos[1] + P.mv[8 + r] * pos[2] + P.mv[12 + r] * pos[3];
    for (int r = 0; r < 4; ++r)
        clip[r] = P.proj[r] * eye[0] + P.proj[4 + r] * eye[1] + P.proj[8 + r] * eye[2] + P.proj[12 + r] * eye[3];
    *fog = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
}

struct sv { float x, y, sx, sy, w, d; };

static __device__ __forceinline__ sv screen_of(int W, int H, const float *c)
{
    sv v;
    v.w = c[3];
    v.x = (c[0] / v.w * 0.5f + 0.5f) * W;
    v.y = (0.5f - c[1] / v.w * 0.5f) * H;
    v.sx = rintf((v.x - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v.sy = rintf((v.y - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v.d = c[2] / v.w * 0.5f + 0.5f;
    return v;
}

/* live_emit's tests for one fast triangle; its box, or 0 */
static __device__ __forceinline__ int fast_box(int W, int H, const sv &a, const sv &b, const sv &c, float *area, short box[4])
{
    *area = edge_xy(a.x, a.y, b.x, b.y, c.x, c.y);
    if (*area == 0 || *area < 0) return 0;   /* live_emit's test (sections are one-sided; NaN draws) */
    float minx = fminf(a.sx, fminf(b.sx, c.sx)), maxx = fmaxf(a.sx, fmaxf(b.sx, c.sx));
    float miny = fminf(a.sy, fminf(b.sy, c.sy)), maxy = fmaxf(a.sy, fmaxf(b.sy, c.sy));
    int x0 = f2i(fmaxf(0, floorf(minx))), x1 = f2i(fminf((float)(W - 1), ceilf(maxx)));
    int y0 = f2i(fmaxf(0, floorf(miny))), y1 = f2i(fminf((float)(H - 1), ceilf(maxy)));
    if (x0 > x1 || y0 > y1) return 0;
    box[0] = (short)x0; box[1] = (short)x1; box[2] = (short)y0; box[3] = (short)y1;
    return 1;
}

/* Whether the triangle of snapped positions A, B, C may draw a pixel of its
 * box: binning's own test for a terrain triangle (raster.cu block_cover:
 * each edge's t1 - t2 >= 0 at a pixel's centre, the values the pixel
 * function makes, without its tie rule), so a triangle it refuses has no
 * pair the binning keeps and draws nothing. Nearly half the section
 * triangles were such (boxes of up to 32 pixels: 79% of those up to 8, 50%
 * of 9 to 16, 21% of 17 to 32; 2.4% past them, which are not tested), each
 * written, binned and sorted for no pixel (lane/kernclimb3). */
enum { CULL_PX = 32 };
static __device__ __forceinline__ bool may_draw(const sv &a, const sv &b, const sv &c, const short box[4])
{
    if ((box[1] - box[0] + 1) * (box[3] - box[2] + 1) > CULL_PX) return true;
    const float X[3] = {a.sx, b.sx, c.sx}, Y[3] = {a.sy, b.sy, c.sy};
    for (int y = box[2]; y <= box[3]; ++y) {
        float sy = y + 0.5f;
        float t2[3];
#pragma unroll
        for (int i = 0; i < 3; ++i) {
            int p = (i + 1) % 3, q = (i + 2) % 3;
            t2[i] = (sy - Y[p]) * (X[q] - X[p]);
        }
        for (int x = box[0]; x <= box[1]; ++x) {
            float sx = (float)x + 0.5f;
            bool in = true;
#pragma unroll
            for (int i = 0; i < 3; ++i) {
                int p = (i + 1) % 3, q = (i + 2) % 3;
                in = in & ((sx - X[p]) * (Y[q] - Y[p]) - t2[i] >= 0);
            }
            if (in) return true;
        }
    }
    return false;
}

/* The tiles of terrain triangle T's box (at most 32) that may hold a pixel
 * block_cover keeps, a bit each, row by row: a tile is out when one edge's
 * value (block_cover's t1 - t2, the same operations) is below 0 at the four
 * corners of the tile's pixels in the box. The value is monotone in x and
 * in y apart (each rounding is: x + 0.5 - X, its product by a constant, the
 * difference of the two), so its largest over the pixels is at a corner,
 * and every pixel of such a tile fails that edge: block_cover gives the
 * tile's two warp blocks no pixel. A tile left in may still hold none (the
 * binning's mask for it is 0, as before). */
static __device__ __forceinline__ uint32_t tile_mask(const tri &T)
{
    const float X[3] = {T.sx[0], T.sx[1], T.sx[2]}, Y[3] = {T.sy[0], T.sy[1], T.sy[2]};
    int tx0 = T.x0 / TILE, nx = T.x1 / TILE - tx0 + 1, ty0 = T.y0 / TILE, ny = T.y1 / TILE - ty0 + 1;
    uint32_t m = 0;
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            int xa = max((int)T.x0, (tx0 + i) * TILE), xb = min((int)T.x1, (tx0 + i) * TILE + TILE - 1);
            bool in = false;
            /* each warp block (4 rows) apart */
            for (int h = 0; h < 2; ++h) {
                int ya = max((int)T.y0, (ty0 + j) * TILE + 4 * h), yb = min((int)T.y1, (ty0 + j) * TILE + 4 * h + 3);
                if (ya > yb) continue;
                bool out = false;
#pragma unroll
                for (int e = 0; e < 3; ++e) {
                    int a = (e + 1) % 3, b = (e + 2) % 3;
                    float ta = ((float)xa + 0.5f - X[a]) * (Y[b] - Y[a]), tb = ((float)xb + 0.5f - X[a]) * (Y[b] - Y[a]);
                    float ua = ((float)ya + 0.5f - Y[a]) * (X[b] - X[a]), ub = ((float)yb + 0.5f - Y[a]) * (X[b] - X[a]);
                    out = out || (ta - ua < 0 && ta - ub < 0 && tb - ua < 0 && tb - ub < 0);
                }
                in = in || !out;
            }
            if (in) m |= 1u << (j * nx + i);
        }
    return m;
}

/* QWARP quads of a draw a warp: the count's chunk (ddraw.chunk0) */
enum { QWARP = 32 };

/* the draw of global chunk W: the last of the N draws whose first chunk is
 * at most W (a draw without quads has its successor's first chunk), by a
 * 32-way search over the warp (W the warp's: every lane calls it): two or
 * three rounds of loads side by side where the binary search waited on
 * ten in turn, a quarter of quads_count's stall samples (lane/kernclimb) */
static __device__ __forceinline__ uint32_t draw_of(const ddraw *D, uint32_t n, uint32_t w)
{
    uint32_t lane = threadIdx.x & 31, lo = 0, hi = n;
    while (hi - lo > 1) {
        uint32_t step = (hi - lo + 31) / 32, at = lo + step * lane;
        unsigned le = __ballot_sync(0xffffffffu, at < hi && D[at].chunk0 <= w);
        lo += step * (uint32_t)(31 - __clz(le));
        hi = min(hi, lo + step);
    }
    return lo;
}

/* The count (lane/geomocc): a warp a chunk of QWARP quads of one draw, a
 * quad a lane (the block of 256 a draw left most lanes idle on the small
 * draws, and 138 registers held one block an SM). Each quad's triangles
 * are counted, and each triangle the write will make goes to the write's
 * list (QW) as its own entry, the warp's in quad order. */

__global__ void __launch_bounds__(QBLOCK)
quads_count(const envp *__restrict__ P, const ddraw *__restrict__ draws, uint32_t ndraws, uint32_t nchunks,
             const int32_t *__restrict__ mesh, const int32_t *__restrict__ orders, int W, int H,
             uint32_t *counts, xunit *clip_list, uint32_t *nclip, qwent *qw)
{
    uint32_t w = (blockIdx.x * blockDim.x + threadIdx.x) / 32, lane = threadIdx.x & 31;
    if (w >= nchunks) return;
    uint32_t di = draw_of(draws, ndraws, w);
    const ddraw &d = draws[di];
    uint32_t i = (w - d.chunk0) * QWARP + lane;
    const envp &E = P[d.env];
    int e[2] = {0, 0};
    uint32_t g = d.unit0 + i;
    if (i < d.nquads) {
        int offset = d.order >= 0 ? orders[d.order + i] : (int)i * 4;
        /* the positions' half of each vertex (16 of its 32 bytes, the
         * arena's first half: ddraw.mesh) */
        union { uint4 q[4]; int32_t w[16]; } rq;
        const uint4 *M = (const uint4 *)(mesh + d.mesh) + offset;
#pragma unroll
        for (int p = 0; p < 4; ++p) rq.q[p] = M[p];
        float c[4][4], fog;
        for (int j = 0; j < 4; ++j) quad_pos(E, d, rq.w + j * 4, c[j], &fog);
        int ka = classify(c[0], c[1], c[3]), kb = classify(c[1], c[2], c[3]);
        if (ka == 2 || kb == 2) clip_list[atomicAdd(nclip, 1u)] = xunit{g, XU_QUAD, di, i};
        else {
            sv v[4];
            for (int j = 0; j < 4; ++j) v[j] = screen_of(W, H, c[j]);
            float area;
            short box[4];
            e[0] = ka == 0 && fast_box(W, H, v[0], v[1], v[3], &area, box) && may_draw(v[0], v[1], v[3], box);
            e[1] = kb == 0 && fast_box(W, H, v[1], v[2], v[3], &area, box) && may_draw(v[1], v[2], v[3], box);
            counts[g] = (uint32_t)(e[0] + e[1]);
        }
    }
    /* the warp's entries, in quad order: one atomic a warp */
    uint32_t cnt = (uint32_t)(e[0] + e[1]), inc = cnt;
#pragma unroll
    for (int s = 1; s < 32; s <<= 1) {
        uint32_t o = __shfl_up_sync(0xffffffffu, inc, s);
        if (lane >= (uint32_t)s) inc += o;
    }
    uint32_t total = __shfl_sync(0xffffffffu, inc, 31), base = 0;
    if (total == 0) return;
    if (lane == 31) base = atomicAdd(nclip + 1, total);
    base = __shfl_sync(0xffffffffu, base, 31) + inc - cnt;
    uint32_t k = 0;
    for (int t = 0; t < 2; ++t)
        if (e[t]) { qw[base + k] = qwent{g, di, i, (uint32_t)t | k << 1}; ++k; }
}

/* entry X's triangle built in O (raster.c live_emit's record); its place
 * in the arena, and its tile count in *NT */
static __device__ __forceinline__ void quad_tri(const envp *__restrict__ P, const ddraw &d,
                                                const qwent &x, const int32_t *__restrict__ mesh,
                                                const int32_t *__restrict__ orders, int W, int H, tri &o, uint32_t *nt)
{
    const envp &E = P[d.env];
    int offset = d.order >= 0 ? orders[d.order + x.i] : (int)x.i * 4;
    const int t = x.tk & 1;
    const int k[3] = {t ? 1 : 0, t ? 2 : 1, 3};
    const uint4 *M = (const uint4 *)(mesh + d.mesh) + offset, *M2 = M + 4 * (int64_t)d.nquads;
    union { uint4 q[2]; int32_t w[8]; } rw[3];
    float c[3][4], fog[3];
    sv v[3];
    for (int m = 0; m < 3; ++m) {
        rw[m].q[0] = M[k[m]];
        rw[m].q[1] = M2[k[m]];
        quad_pos(E, d, rw[m].w, c[m], &fog[m]);
        v[m] = screen_of(W, H, c[m]);
    }
    float area;
    short box[4];
    fast_box(W, H, v[0], v[1], v[2], &area, box);
    o.aux = o.tex = o.pad[0] = o.pad[1] = o.pad[2] = 0;
    for (int m = 0; m < 3; ++m) {
        const sv &q = v[m];
        const int32_t *r = rw[m].w;
        o.x[m] = q.x; o.y[m] = q.y; o.sx[m] = q.sx; o.sy[m] = q.sy; o.d[m] = q.d;
        o.w[m] = E.prec & RP_RCP ? 1.0f / q.w : q.w;     /* raster_prec.h RP_RCP */
        o.u[m] = __int_as_float(r[3]);
        o.v[m] = __int_as_float(r[4]);
        uint32_t col = d.flags & 2 ? (uint32_t)r[5] : 0xffffffffU;
        float cf[4];
        for (int ch = 0; ch < 4; ++ch) cf[ch] = ((col >> (8 * ch)) & 255) / 255.0f;
        float l0 = d.flags & 4 ? (float)((uint32_t)r[7] & 65535) : 240;
        float l1 = d.flags & 4 ? (float)((uint32_t)r[7] >> 16) : 240;
        if (E.prec == RP_FAST) {
            /* raster_prec.h's fast mode: as halves (tri_ch, tri_lh) */
            tri_ch(o)[m * 2] = __floats2half2_rn(cf[0], cf[1]);
            tri_ch(o)[m * 2 + 1] = __floats2half2_rn(cf[2], cf[3]);
            tri_lh(o)[m] = __floats2half2_rn(l0, l1);
        } else {
            for (int ch = 0; ch < 4; ++ch) o.c[m][ch] = cf[ch];
            o.l[m][0] = l0;
            o.l[m][1] = l1;
        }
        o.fog[m] = fog[m];
    }
    o.aux = tl_bits(o.sx, o.sy);
    o.area = E.prec & RP_RCP ? 1.0f / area : area;
    o.x0 = box[0]; o.x1 = box[1]; o.y0 = box[2]; o.y1 = box[3];
    o.mode = d.water ? 32u : 0u;
    *nt = (uint32_t)((box[1] / TILE - box[0] / TILE + 1) * (box[3] / TILE - box[2] / TILE + 1));
}

/* The write: a triangle a thread, from the count's list (its length the
 * device's, *DN, at most NLIST): its quad's three vertices again (each
 * vertex's operations are its own, so the bits are the count's and C's),
 * written at its unit's offset and its place in the unit. */
__global__ void __launch_bounds__(QBLOCK)
quads_write(const envp *__restrict__ P, const ddraw *__restrict__ draws, const qwent *__restrict__ list, uint32_t nlist,
            const uint32_t *dn, uint32_t u0, uint32_t u1, const int32_t *__restrict__ mesh, const int32_t *__restrict__ orders,
            int W, int H, const uint32_t *__restrict__ offsets, uint32_t tri_base, tri *out, uint32_t *tiles,
            uint32_t *tmask)
{
    uint32_t lim = min(*dn, nlist);
    /* each lane's triangle built in shared memory, then the warp's stored
     * together: a warp's entries are mostly one run of the arena, so each
     * store instruction writes 512 contiguous bytes (a thread's own 240
     * byte record in uint4s met 32 sectors an instruction) */
    __shared__ tri S[QBLOCK];
    uint32_t lane = threadIdx.x & 31, w0 = threadIdx.x & ~31u;
    for (uint32_t b = (blockIdx.x * blockDim.x + threadIdx.x) & ~31u; b < lim; b += gridDim.x * blockDim.x) {
        uint32_t e = b + lane, at = ~0u, nt = 0;
        if (e < lim) {
            qwent x = list[e];
            /* its draw read beside its unit's offsets (both wait on the
             * list's entry alone; read after the test they made one more
             * wait on memory in the chain, lane/kernclimb) */
            uint32_t o0 = offsets[x.g], o1 = offsets[x.g + 1];
            const ddraw d = draws[x.d];
            /* past the arena (one_trip's squash) nothing is written */
            if (x.g >= u0 && x.g < u1 && o1 != o0) {
                quad_tri(P, d, x, mesh, orders, W, H, S[threadIdx.x], &nt);
                at = o0 - tri_base + (x.tk >> 1);
                /* the box's tiles the triangle may draw a pixel of, the
                 * pairs the binning makes: a tile none of whose pixels
                 * passes one edge of block_cover's terrain test is left out
                 * (44% of a box's tiles hold no pixel; lane/kernclimb3) */
                if (nt > 1 && nt <= 32) {
                    uint32_t m = tile_mask(S[threadIdx.x]);
                    nt = (uint32_t)__popc(m);
                    tmask[at] = m;
                }
                tiles[at] = nt;
            }
        }
        __syncwarp();
        const uint4 *src = (const uint4 *)&S[w0];
        for (uint32_t j = lane; j < 32 * TRI_QUADS; j += 32) {
            uint32_t L = j / TRI_QUADS, part = j - L * TRI_QUADS;
            uint32_t a = __shfl_sync(0xffffffffu, at, L);
            if (a != ~0u) ((uint4 *)&out[a])[part] = src[j];
        }
        __syncwarp();
    }
}

/* ------------------------------------------------------------ the clipped section triangles */

template <class T>
static __device__ __forceinline__ T sel3(int i, T a, T b, T c) { return i == 0 ? a : i == 1 ? b : c; }

/* Fan triangle F of section triangle T of quad I of draw D clipped as raster.c
 * clip_triangle clips it when one plane alone cuts it (or none: F 1, the
 * triangle itself), built in O as the generic kernel's write builds it
 * (write_sink emit_pos and rc_fields, or emit for a triangle inside every
 * plane): the plane's Sutherland-Hodgman over the three vertices, each
 * made vertex its edge's start plus t times its end less its start, t the
 * start's distance over the distances' difference, every attribute by the
 * same operations (rc_clip, rc_replay), the fan triangle 0, F, F + 1 of the
 * polygon. generic_count chose the triangle (count_sink lean: rc_clip made
 * two vertices, so no later plane cut the polygon) and the fan triangles
 * live_emit keeps. Its place in the arena, and its tile count in *NT. */
static __device__ __forceinline__ void clip_tri_rec(const envp *__restrict__ P, const ddraw &d, int t, uint32_t i, int F,
                                                    const int32_t *__restrict__ mesh, const int32_t *__restrict__ orders,
                                                    int W, int H, tri &o, uint32_t *nt)
{
    const envp &E = P[d.env];
    int offset = d.order >= 0 ? orders[d.order + i] : (int)i * 4;
    const int k[3] = {t ? 1 : 0, t ? 2 : 1, 3};
    const uint4 *M = (const uint4 *)(mesh + d.mesh) + offset, *M2 = M + 4 * (int64_t)d.nquads;
    float c[3][4], fog[3], uu[3], vv[3];
    uint32_t col[3], lt[3];
    for (int m = 0; m < 3; ++m) {
        union { uint4 q[2]; int32_t w[8]; } rw;
        rw.q[0] = M[k[m]];
        rw.q[1] = M2[k[m]];
        quad_pos(E, d, rw.w, c[m], &fog[m]);
        uu[m] = __int_as_float(rw.w[3]);
        vv[m] = __int_as_float(rw.w[4]);
        col[m] = d.flags & 2 ? (uint32_t)rw.w[5] : 0xffffffffU;
        lt[m] = (uint32_t)rw.w[7];
    }
    /* the polygon's ids (4 bits a vertex: 0 to 2 the triangle's, 3 and 4
     * the made ones) and each made vertex's edge (2 bits its start, 2 its
     * end, at 4 k) */
    uint32_t poly = 0x210, src = 0;
    float dd[3] = {0, 0, 0};
    const uint32_t om = rc_out(c[0]) | rc_out(c[1]) | rc_out(c[2]);
    if (om) {
        const int plane = __ffs((int)om) - 1, axis = plane / 2;
        const float sign = plane & 1 ? -1.0f : 1.0f;
        for (int j = 0; j < 3; ++j) dd[j] = c[j][3] - sign * sel3(axis, c[j][0], c[j][1], c[j][2]);
        int n = 0, nm = 0;
        poly = 0;
#pragma unroll
        for (int j = 0; j < 3; ++j) {
            const int q = j == 2 ? 0 : j + 1;
            const float dp = dd[j], dq = dd[q];
            if (dp >= 0) poly |= (uint32_t)j << (4 * n++);
            if ((dp >= 0) != (dq >= 0)) {
                src |= (uint32_t)(j | q << 2) << (4 * nm);
                poly |= (uint32_t)(3 + nm++) << (4 * n++);
            }
        }
    }
    const bool rcp = (E.prec & RP_RCP) != 0, pack = E.prec == RP_FAST;
    o.aux = o.tex = o.pad[0] = o.pad[1] = o.pad[2] = 0;
    /* a vertex at a time (unrolled, the three held 207 registers) */
#pragma unroll 1
    for (int m = 0; m < 3; ++m) {
        const int id = (int)(poly >> (4 * (m == 0 ? 0 : F + m - 1)) & 15);
        float cl[4], fu, fv, ff, cf[4], l[2];
        if (id < 3) {
            for (int q = 0; q < 4; ++q) cl[q] = sel3(id, c[0][q], c[1][q], c[2][q]);
            fu = sel3(id, uu[0], uu[1], uu[2]);
            fv = sel3(id, vv[0], vv[1], vv[2]);
            ff = sel3(id, fog[0], fog[1], fog[2]);
            const uint32_t cw = sel3(id, col[0], col[1], col[2]), lw = sel3(id, lt[0], lt[1], lt[2]);
            for (int ch = 0; ch < 4; ++ch) cf[ch] = ((cw >> (8 * ch)) & 255) / 255.0f;
            l[0] = d.flags & 4 ? (float)(lw & 65535) : 240;
            l[1] = d.flags & 4 ? (float)(lw >> 16) : 240;
        } else {
            const uint32_t e = src >> (4 * (id - 3));
            const int a = (int)(e & 3), b = (int)(e >> 2 & 3);
            const float da = sel3(a, dd[0], dd[1], dd[2]), db = sel3(b, dd[0], dd[1], dd[2]);
            const float tt = da / (da - db);
#define LERP(X) ({ const float fa_ = sel3(a, X[0], X[1], X[2]), fb_ = sel3(b, X[0], X[1], X[2]); fa_ + tt * (fb_ - fa_); })
            for (int q = 0; q < 4; ++q) {
                const float fa = sel3(a, c[0][q], c[1][q], c[2][q]), fb = sel3(b, c[0][q], c[1][q], c[2][q]);
                cl[q] = fa + tt * (fb - fa);
            }
            fu = LERP(uu);
            fv = LERP(vv);
            ff = LERP(fog);
#undef LERP
            const uint32_t ca = sel3(a, col[0], col[1], col[2]), cb = sel3(b, col[0], col[1], col[2]);
            const uint32_t la = sel3(a, lt[0], lt[1], lt[2]), lb = sel3(b, lt[0], lt[1], lt[2]);
            for (int ch = 0; ch < 4; ++ch) {
                const float fa = ((ca >> (8 * ch)) & 255) / 255.0f, fb = ((cb >> (8 * ch)) & 255) / 255.0f;
                cf[ch] = fa + tt * (fb - fa);
            }
            const float a0 = d.flags & 4 ? (float)(la & 65535) : 240, b0 = d.flags & 4 ? (float)(lb & 65535) : 240;
            const float a1 = d.flags & 4 ? (float)(la >> 16) : 240, b1 = d.flags & 4 ? (float)(lb >> 16) : 240;
            l[0] = a0 + tt * (b0 - a0);
            l[1] = a1 + tt * (b1 - a1);
        }
        const sv q = screen_of(W, H, cl);
        o.x[m] = q.x; o.y[m] = q.y; o.sx[m] = q.sx; o.sy[m] = q.sy; o.d[m] = q.d;
        o.w[m] = rcp ? 1.0f / q.w : q.w;
        o.u[m] = fu;
        o.v[m] = fv;
        if (pack) {
            tri_ch(o)[m * 2] = __floats2half2_rn(cf[0], cf[1]);
            tri_ch(o)[m * 2 + 1] = __floats2half2_rn(cf[2], cf[3]);
            tri_lh(o)[m] = __floats2half2_rn(l[0], l[1]);
        } else {
            for (int ch = 0; ch < 4; ++ch) o.c[m][ch] = cf[ch];
            o.l[m][0] = l[0];
            o.l[m][1] = l[1];
        }
        o.fog[m] = ff;
    }
    sv v[3];
    for (int m = 0; m < 3; ++m) { v[m].x = o.x[m]; v[m].y = o.y[m]; v[m].sx = o.sx[m]; v[m].sy = o.sy[m]; }
    float area;
    short box[4];
    fast_box(W, H, v[0], v[1], v[2], &area, box);
    o.aux = tl_bits(o.sx, o.sy);
    o.area = rcp ? 1.0f / area : area;
    o.x0 = box[0]; o.x1 = box[1]; o.y0 = box[2]; o.y1 = box[3];
    o.mode = d.water ? 32u : 0u;
    *nt = (uint32_t)((box[1] / TILE - box[0] / TILE + 1) * (box[3] / TILE - box[2] / TILE + 1));
}

/* The write of the clipped section triangles generic_count leaves to it
 * (its list from TOP down, *DN of at most NLIST entries, each a section
 * quad's triangle and its fan triangles): a fan triangle a thread, each
 * warp's entries spread over its lanes by their fan triangles, then built
 * and stored as quads_write builds and stores its triangles. The generic
 * kernel wrote them through its full clipper, a triangle's fan a thread
 * at 255 registers (2 blocks an SM); 98% of the clipped section triangles'
 * output is from one plane (gold-fast, lane/kcgeom). */
__global__ void __launch_bounds__(QBLOCK)
quads_clip(const envp *__restrict__ P, const ddraw *__restrict__ draws, const xunit *__restrict__ top, uint32_t nlist,
           const uint32_t *dn, uint32_t u0, uint32_t u1, const int32_t *__restrict__ mesh, const int32_t *__restrict__ orders,
           int W, int H, const uint32_t *__restrict__ offsets, uint32_t tri_base, tri *out, uint32_t *tiles,
           uint32_t *tmask)
{
    uint32_t lim = min(*dn, nlist);
    __shared__ tri S[QBLOCK];
    /* each warp's fan triangles: the lane of its entry, and which */
    __shared__ uint8_t slot[2 * QBLOCK];
    uint32_t lane = threadIdx.x & 31, w0 = threadIdx.x & ~31u;
    uint8_t *sl = slot + 2 * w0;
    for (uint32_t b = (blockIdx.x * blockDim.x + threadIdx.x) & ~31u; b < lim; b += gridDim.x * blockDim.x) {
        uint32_t e = b + lane;
        xunit x = {0, 0, 0, 0};
        if (e < lim) x = top[-(int64_t)e];
        const uint32_t fm = e < lim ? x.kind >> 1 & 3 : 0, cnt = (uint32_t)__popc(fm);
        uint32_t inc = cnt;
#pragma unroll
        for (int s = 1; s < 32; s <<= 1) {
            uint32_t o = __shfl_up_sync(0xffffffffu, inc, s);
            if (lane >= (uint32_t)s) inc += o;
        }
        const uint32_t total = __shfl_sync(0xffffffffu, inc, 31), s0 = inc - cnt;
        if (fm & 1) sl[s0] = (uint8_t)(lane << 1);
        if (fm & 2) sl[s0 + (fm & 1)] = (uint8_t)(lane << 1 | 1);
        __syncwarp();
        for (uint32_t r0 = 0; r0 < total; r0 += 32) {
            const uint32_t o = r0 + lane, sv_ = o < total ? sl[o] : 0, L = sv_ >> 1;
            xunit y;
            y.g = __shfl_sync(0xffffffffu, x.g, L);
            y.kind = __shfl_sync(0xffffffffu, x.kind, L);
            y.a = __shfl_sync(0xffffffffu, x.a, L);
            y.b = __shfl_sync(0xffffffffu, x.b, L);
            uint32_t at = ~0u, nt = 0;
            if (o < total) {
                uint32_t o0 = offsets[y.g], o1 = offsets[y.g + 1];
                const ddraw d = draws[y.a];
                /* past the arena (one_trip's squash) nothing is written */
                if (y.g >= u0 && y.g < u1 && o1 != o0) {
                    const int F = (int)(sv_ & 1) + 1;
                    clip_tri_rec(P, d, (int)(y.kind & 1), y.b, F, mesh, orders, W, H, S[threadIdx.x], &nt);
                    at = o0 - tri_base + (y.kind >> 3) + (F == 2 && (y.kind >> 1 & 1));
                    if (nt > 1 && nt <= 32) {
                        uint32_t m = tile_mask(S[threadIdx.x]);
                        nt = (uint32_t)__popc(m);
                        tmask[at] = m;
                    }
                    tiles[at] = nt;
                }
            }
            __syncwarp();
            const uint4 *src = (const uint4 *)&S[w0];
#pragma unroll 1
            for (uint32_t j = lane; j < 32 * TRI_QUADS; j += 32) {
                uint32_t Lw = j / TRI_QUADS, part = j - Lw * TRI_QUADS;
                uint32_t a = __shfl_sync(0xffffffffu, at, Lw);
                if (a != ~0u) ((uint4 *)&out[a])[part] = src[j];
            }
            __syncwarp();
        }
    }
}
