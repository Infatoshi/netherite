/* The device renderer (render.h): raster.c's live terrain passes over many
 * environments. Every function below that computes a float is a port of the
 * raster.c or raster_sky2.c function it names, operation for operation, so
 * the build's --fmad=false, -prec-div=true and -prec-sqrt=true give the C
 * engine's bits (csrc/cuda/render/mirrors.txt lists them for the mirror
 * rule: a change to one of those C functions needs its port here). */
#include "dev.cuh"

__device__ __forceinline__ double edge_exact(float ax, float ay, float bx, float by, double x, double y)
{
    return (x - ax) * ((double)by - ay) - (y - ay) * ((double)bx - ax);
}

/* the sign of edge_exact (-1, 0, 1) without FP64 where floats decide it:
 * FP64 runs at 1/64 of FP32 here, and three edges a pixel made an entity
 * tile's triangles cost five times a terrain tile's. In floats each of the
 * two products is within 3.01u of its exact value (u = 2^-24, subnormal
 * parts under 2^-149) and the difference within u more, so when |f| is
 * over 2^-19 of |t1| + |t2| (at least 2^-100, finite) the exact value, and
 * the double's (within 2^-51 of it), have f's sign; else the double. */
__device__ __forceinline__ int edge_sign(float ax, float ay, float bx, float by, float x, float y)
{
    float t1 = (x - ax) * (by - ay), t2 = (y - ay) * (bx - ax);
    float f = t1 - t2, m = fabsf(t1) + fabsf(t2);
    if (m >= 0x1p-100f && m <= 3.4028234664e38f && fabsf(f) > m * 0x1p-19f) return f > 0 ? 1 : -1;
    double e = edge_exact(ax, ay, bx, by, x, y);
    return e > 0 ? 1 : e < 0 ? -1 : 0;
}

/* raster_prec.h RP_EDGE's test of a float edge value as edge_sign's
 * sign (NaN passes, as the C test's comparisons let it) */
__device__ __forceinline__ int edge_fsign(float f) { return f < 0 ? -1 : f == 0 ? 0 : 1; }

/* edge_exact(...) >= 0 out of line (lane/coalesce): inlined, the
 * binning's loops computed its doubles for every row of every triangle
 * (hoisted and speculated: FP64 runs at 1/64 of FP32 here, 62% of pairs'
 * stall samples), where edge_may takes it on a near miss alone */
__device__ __noinline__ bool edge_exact_ge0(float ax, float ay, float bx, float by, float x, float y)
{
    return edge_exact(ax, ay, bx, by, x, y) >= 0;
}

/* whether edge_sign or edge_fsign of the float edge (RP_EDGE: the pair
 * lists and cover masks do not know the frame's precision) may be >= 0 */
__device__ __forceinline__ bool edge_may(float ax, float ay, float bx, float by, float x, float y)
{
    float t1 = (x - ax) * (by - ay), t2 = (y - ay) * (bx - ax);
    float f = t1 - t2, m = fabsf(t1) + fabsf(t2);
    if (!(f < 0)) return true;
    if (m >= 0x1p-100f && m <= 3.4028234664e38f && fabsf(f) > m * 0x1p-19f) return false;
    return edge_exact_ge0(ax, ay, bx, by, x, y);
}

/* whether triangle T may draw pixel (x, y) of its box: the edge test its
 * pixel function starts with (pixel, e_pixel, crack_pixel,
 * portal_pixel), on the same values, without the tie rule (a superset;
 * entities' and portals' of both precisions, edge_may);
 * the other kinds' pixels are their box's */
template <class TT>
__device__ __forceinline__ bool may_cover(const TT &T, int x, int y)
{
    int kind = T.mode >> 8;
    if (kind != TK_TERRAIN && kind != TK_ENTITY && kind != TK_CRACK && kind != TK_PORTAL) return true;
    /* chosen by value, not by pointer (lane/coalesce: raster -1%) */
    bool ck = kind == TK_CRACK;
    const float X[3] = {ck ? T.x[0] : T.sx[0], ck ? T.x[1] : T.sx[1], ck ? T.x[2] : T.sx[2]};
    const float Y[3] = {ck ? T.y[0] : T.sy[0], ck ? T.y[1] : T.sy[1], ck ? T.y[2] : T.sy[2]};
    float sx = x + 0.5f, sy = y + 0.5f;
    if (kind == TK_ENTITY || kind == TK_PORTAL)
        return edge_may(X[1], Y[1], X[2], Y[2], sx, sy) && edge_may(X[2], Y[2], X[0], Y[0], sx, sy) &&
               edge_may(X[0], Y[0], X[1], Y[1], sx, sy);
    return edge_xy(X[1], Y[1], X[2], Y[2], sx, sy) >= 0 && edge_xy(X[2], Y[2], X[0], Y[0], sx, sy) >= 0 &&
           edge_xy(X[0], Y[0], X[1], Y[1], sx, sy) >= 0;
}

/* whether T may draw a pixel of tile (tx, ty): a pair it cannot draw in
 * takes the sentinel key (sorted past the others, never read). Tiny
 * triangles meet boxes they draw no pixel of: in the heaviest tiles 78 to
 * 92% of the triangles a warp's block met drew none of its pixels. */
template <class TT>
__device__ __forceinline__ bool tile_meets(const TT &T, int tx, int ty)
{
    int x0 = max((int)T.x0, tx * TILE), x1 = min((int)T.x1, tx * TILE + TILE - 1);
    int y0 = max((int)T.y0, ty * TILE), y1 = min((int)T.y1, ty * TILE + TILE - 1);
    if ((x1 - x0 + 1) * (y1 - y0 + 1) > PAIR_TEST_MAX) return true;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (may_cover(T, x, y)) return true;
    return false;
}

/* what the binning reads of a triangle: tri's first 64 bytes (the box,
 * the mode, the screen positions, a crack's x and y) */
struct thead {
    short x0, x1, y0, y1;
    uint32_t mode;
    float area;
    float sx[3], sy[3], x[3], y[3];
};
static_assert(sizeof(thead) == 64 && offsetof(thead, y) == offsetof(tri, y), "thead is tri's head");

/* a warp's share of pairs: its 32 triangles from T0 (of NTRIS) and their
 * pairs, the pairs a lane each (lane/coalesce: a thread a triangle read its
 * fields 240 bytes from its neighbour's, a sector each, and a triangle over
 * many tiles held its warp while the others' lanes idled). The heads come
 * in whole (the warp's 16-byte loads over 2 KB), word w of triangle j at
 * hd[w][j]; pair p's triangle is the last j with off[j] <= p, its tile
 * the p - off[j]th of its box, row by row: the keys and values are the
 * ones a thread a triangle wrote, in their places. */
enum { PAIR_WARPS = 8 };   /* 256 threads: the launches' (dev.cuh) */
struct pair_stage { uint32_t hd[16][33], off[33], env[32]; };
__device__ __forceinline__ void pairs_warp(const tri *tris, uint32_t t0, uint32_t ntris, const uint32_t *tri_env,
                                           const uint32_t *pair_off, int tiles_x, int ntiles, uint32_t sentinel,
                                           uint32_t *keys, uint32_t *vals, pair_stage &S)
{
    int lane = threadIdx.x & 31;
    uint32_t nt = min(ntris - t0, 32u);
    for (int i = lane; i < 128; i += 32) {
        uint32_t j = i >> 2, c = i & 3;
        if (j < nt) {
            uint4 q = ((const uint4 *)(tris + t0 + j))[c];
            S.hd[c * 4][j] = q.x; S.hd[c * 4 + 1][j] = q.y; S.hd[c * 4 + 2][j] = q.z; S.hd[c * 4 + 3][j] = q.w;
        }
    }
    S.off[lane] = (uint32_t)lane <= nt ? pair_off[t0 + lane] : 0xffffffffu;
    if (lane == 0) S.off[32] = nt == 32 ? pair_off[t0 + 32] : 0xffffffffu;
    if ((uint32_t)lane < nt) S.env[lane] = tri_env[t0 + lane] * (uint32_t)ntiles;
    __syncwarp();
    uint32_t p0 = S.off[0], p1 = S.off[nt];
    for (uint32_t p = p0 + lane; p < p1; p += 32) {
        int j = 0;
#pragma unroll
        for (int b = 16; b; b >>= 1)
            if (S.off[j + b] <= p) j += b;
        union { thead t; uint32_t w[16]; } h;
#pragma unroll
        for (int w = 0; w < 16; ++w) h.w[w] = S.hd[w][j];
        const thead &T = h.t;
        int tx0 = T.x0 / TILE, nx = T.x1 / TILE - tx0 + 1, k = (int)(p - S.off[j]);
        int ty = T.y0 / TILE + k / nx, tx = tx0 + k % nx;
        keys[p] = tile_meets(T, tx, ty) ? S.env[j] + (uint32_t)(ty * tiles_x + tx) : sentinel;
        vals[p] = t0 + (uint32_t)j;
    }
}

/* each triangle's (environment, tile) keys, in triangle order: 32
 * triangles a warp */
__global__ void __launch_bounds__(256)
pairs(const tri *tris, uint32_t ntris, const uint32_t *tri_env, const uint32_t *pair_off,
                        int tiles_x, int ntiles, uint32_t sentinel, uint32_t *keys, uint32_t *vals)
{
    __shared__ pair_stage S[PAIR_WARPS];
    uint32_t t0 = (blockIdx.x * blockDim.x + threadIdx.x) & ~31u;
    if (t0 >= ntris) return;
    pairs_warp(tris, t0, ntris, tri_env, pair_off, tiles_x, ntiles, sentinel, keys, vals, S[threadIdx.x >> 5]);
}

/* dst[i] = src[idx[i]]: the host's few numbers in one copy back */
__global__ void gather(const uint32_t *src, const uint32_t *idx, uint32_t n, uint32_t *dst)
{
    uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) dst[i] = src[idx[i]];
}

/* the environment (in the group) of every triangle: tri offsets are env-major */
__global__ void tri_envs(const uint32_t *env_tri0, int nenv, uint32_t ntris, uint32_t *tri_env)
{
    uint32_t t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= ntris) return;
    int lo = 0, hi = nenv - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (env_tri0[mid] <= t) lo = mid; else hi = mid - 1;
    }
    tri_env[t] = (uint32_t)lo;
}

/* each (environment, tile) key's run of the sorted pairs, [start, end)
 * (both left zero for a key with none); W: the pair count is the one-trip
 * tail's (w[TRIP_PAIRS]), else NPAIRS */
__global__ void key_runs(const uint32_t *keys, uint32_t npairs, const uint32_t *w, uint32_t *key_start, uint32_t *key_end)
{
    uint32_t n = w ? w[TRIP_PAIRS] : npairs;
    uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    uint32_t k = keys[i];
    if (i == 0 || keys[i - 1] != k) key_start[k] = i;
    if (i + 1 == n || keys[i + 1] != k) key_end[k] = i + 1;
}


/* the triangles, each environment's first (UNIT_BASE[0..N], units last),
 * and whether they fit */
__global__ void trip_tris(const uint32_t *offsets, const uint32_t *unit_base, int n, uint32_t tris_cap,
                            uint32_t *env_tri0, uint32_t *w)
{
    int e = threadIdx.x;
    for (int i = e; i <= n; i += blockDim.x) env_tri0[i] = offsets[unit_base[i]];
    if (e == 0) {
        uint32_t t = offsets[unit_base[n]];
        w[TRIP_OVER] = t > tris_cap;
        w[TRIP_TRIS] = t > tris_cap ? 0 : t;
        w[TRIP_PAIRS] = 0;
    }
}

/* past the arena: every unit writes its triangles from 0 (in bounds; the
 * frames are void) */
__global__ void trip_squash(uint32_t *offsets, uint32_t units, const uint32_t *w)
{
    uint32_t g = blockIdx.x * blockDim.x + threadIdx.x;
    if (w[TRIP_OVER] && g <= units) offsets[g] = 0;
}

__global__ void trip_tri_envs(const uint32_t *env_tri0, int nenv, const uint32_t *w, uint32_t *tri_env)
{
    uint32_t t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= w[TRIP_TRIS]) return;
    int lo = 0, hi = nenv - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (env_tri0[mid] <= t) lo = mid; else hi = mid - 1;
    }
    tri_env[t] = (uint32_t)lo;
}

/* the pairs the triangles make, and whether they fit */
__global__ void trip_pairs_n(const uint32_t *pair_off, uint32_t pairs_cap, uint32_t *w)
{
    uint32_t p = pair_off[w[TRIP_TRIS]];
    if (p > pairs_cap) { w[TRIP_OVER] = 1; w[TRIP_TRIS] = 0; p = 0; }
    w[TRIP_PAIRS] = p;
}

/* the most triangles and pairs one frame made, and the clipped units
 * (one block) */
__global__ void trip_env_max(const uint32_t *env_tri0, const uint32_t *pair_off, int n, const uint32_t *nclip,
                             uint32_t *w)
{
    __shared__ uint32_t mt, mp;
    if (threadIdx.x == 0) { mt = 0; mp = 0; }
    __syncthreads();
    if (!w[TRIP_OVER])
        for (int e = threadIdx.x; e < n; e += blockDim.x) {
            uint32_t a = env_tri0[e], b = env_tri0[e + 1];
            atomicMax(&mt, b - a);
            atomicMax(&mp, pair_off[b] - pair_off[a]);
        }
    __syncthreads();
    if (threadIdx.x == 0) { w[TRIP_MAXT] = mt; w[TRIP_MAXP] = mp; w[TRIP_NCLIP] = *nclip; }
}

/* pairs over the device's count; the keys past it are SENTINEL, which
 * sorts last */
__global__ void __launch_bounds__(256)
trip_pairs(const tri *tris, const uint32_t *w, const uint32_t *tri_env, const uint32_t *pair_off,
                             int tiles_x, int ntiles, uint32_t bound, uint32_t sentinel, uint32_t *keys, uint32_t *vals)
{
    __shared__ pair_stage S[PAIR_WARPS];
    uint32_t t = blockIdx.x * blockDim.x + threadIdx.x;
    for (uint32_t i = w[TRIP_PAIRS] + t; i < bound; i += gridDim.x * blockDim.x) keys[i] = sentinel;
    uint32_t t0 = t & ~31u, ntris = w[TRIP_TRIS];
    if (t0 >= ntris) return;
    pairs_warp(tris, t0, ntris, tri_env, pair_off, tiles_x, ntiles, sentinel, keys, vals, S[threadIdx.x >> 5]);
}

/* the tile counts from the device's triangle count on (the scan runs to
 * the arena's end) */
__global__ void trip_zero_tail(uint32_t *tiles, uint32_t n, const uint32_t *w)
{
    uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n && i >= w[TRIP_TRIS]) tiles[i] = 0;
}


/* ------------------------------------------------------------ the pixel */


/* raster.c light_sample */
__device__ __forceinline__ void light_sample(const uint32_t *lm, float block, float sky, float out[3])
{
    float x = fmaxf(0, fminf(15, block / 16.0f));
    float y = fmaxf(0, fminf(15, sky / 16.0f));
    int x0 = f2i(x), y0 = f2i(y);
    int x1 = x0 < 15 ? x0 + 1 : x0, y1 = y0 < 15 ? y0 + 1 : y0;
    float fx = x - x0, fy = y - y0;
    uint32_t c[4] = {lm[y0 * 16 + x0], lm[y0 * 16 + x1], lm[y1 * 16 + x0], lm[y1 * 16 + x1]};
    for (int i = 0; i < 3; ++i) {
        int shift = 8 * (2 - i);
        float a = ((c[0] >> shift) & 255) * (1 - fx) + ((c[1] >> shift) & 255) * fx;
        float b = ((c[2] >> shift) & 255) * (1 - fx) + ((c[3] >> shift) & 255) * fx;
        out[i] = (a * (1 - fy) + b * fy) / 255.0f;
    }
}

__device__ __forceinline__ int wrap(int t, int n) { return (t % n + n) % n; }

/* raster.c light_sample_h (raster_prec.h RP_LIGHT) */
__device__ __forceinline__ void light_sample_h(const uint32_t *lm, float block, float sky, float out[3])
{
    float x = fmaxf(0, fminf(15, HM(block, 0.0625f)));
    float y = fmaxf(0, fminf(15, HM(sky, 0.0625f)));
    int x0 = f2i(x), y0 = f2i(y);
    int x1 = x0 < 15 ? x0 + 1 : x0, y1 = y0 < 15 ? y0 + 1 : y0;
    float fx = hr(x - x0), fy = hr(y - y0), gx = HS(1, fx), gy = HS(1, fy);
    uint32_t c[4] = {lm[y0 * 16 + x0], lm[y0 * 16 + x1], lm[y1 * 16 + x0], lm[y1 * 16 + x1]};
    for (int i = 0; i < 3; ++i) {
        int shift = 8 * (2 - i);
        float a = HA(HM((float)((c[0] >> shift) & 255), gx), HM((float)((c[1] >> shift) & 255), fx));
        float b = HA(HM((float)((c[2] >> shift) & 255), gx), HM((float)((c[3] >> shift) & 255), fx));
        out[i] = hr(HA(HM(a, gy), HM(b, fy)) / 255.0f);
    }
}

/* shade_h with every float16 stage on (raster_prec.h RP_HALF, the fast
 * mode), in the device's own halves: two channels a __half2 operation,
 * each lane the operation shade_h rounds, in its order (the same bits) */
#define H2(a, b) __floats2half2_rn(a, b)
__device__ __forceinline__ int shade_all_h(const tri &T, const envp &P, bool packed, int render_pass, const float q[3],
                                           float distance, const float tex[4], float src[3], float *alpha_out)
{
    const __half2 one = __half2half2(__ushort_as_half((unsigned short)0x3c00));
    const __half2 q0 = __half2half2(__float2half_rn(q[0])), q1 = __half2half2(__float2half_rn(q[1])),
                  q2 = __half2half2(__float2half_rn(q[2]));
    /* the colour (r g, b a) and the light coordinates */
    /* packed (prec RP_FAST): the writer stored them as these halves */
    const __half2 *ch = tri_ch(T), *lh = tri_lh(T);
#define VC(i, k) (packed ? ch[(i) * 2 + (k)] : H2(T.c[i][(k) * 2], T.c[i][(k) * 2 + 1]))
#define VL(i) (packed ? lh[i] : H2(T.l[i][0], T.l[i][1]))
    __half2 c01 = __hadd2_rn(__hadd2_rn(__hmul2_rn(q0, VC(0, 0)), __hmul2_rn(q1, VC(1, 0))), __hmul2_rn(q2, VC(2, 0)));
    __half2 c23 = __hadd2_rn(__hadd2_rn(__hmul2_rn(q0, VC(0, 1)), __hmul2_rn(q1, VC(1, 1))), __hmul2_rn(q2, VC(2, 1)));
    __half2 lc = __hadd2_rn(__hadd2_rn(__hmul2_rn(q0, VL(0)), __hmul2_rn(q1, VL(1))), __hmul2_rn(q2, VL(2)));
#undef VC
#undef VL
    /* light_sample_h */
    __half2 xy = __hmul2_rn(lc, __half2half2(__ushort_as_half((unsigned short)0x2c00)));   /* 1/16 */
    float x = fmaxf(0, fminf(15, __low2float(xy))), y = fmaxf(0, fminf(15, __high2float(xy)));
    int x0 = f2i(x), y0 = f2i(y);
    int x1 = x0 < 15 ? x0 + 1 : x0, y1 = y0 < 15 ? y0 + 1 : y0;
    __half2 fxy = H2(x - x0, y - y0), gxy = __hsub2_rn(one, fxy);
    __half2 fx = __low2half2(fxy), gx = __low2half2(gxy), fy = __high2half2(fxy), gy = __high2half2(gxy);
    uint32_t c[4] = {P.lm[y0 * 16 + x0], P.lm[y0 * 16 + x1], P.lm[y1 * 16 + x0], P.lm[y1 * 16 + x1]};
#define CH(k, s0, s1) H2((float)((c[k] >> (s0)) & 255), (float)((c[k] >> (s1)) & 255))
    __half2 lrg = __hadd2_rn(__hmul2_rn(__hadd2_rn(__hmul2_rn(CH(0, 16, 8), gx), __hmul2_rn(CH(1, 16, 8), fx)), gy),
                             __hmul2_rn(__hadd2_rn(__hmul2_rn(CH(2, 16, 8), gx), __hmul2_rn(CH(3, 16, 8), fx)), fy));
    __half2 lb = __hadd2_rn(__hmul2_rn(__hadd2_rn(__hmul2_rn(CH(0, 0, 0), gx), __hmul2_rn(CH(1, 0, 0), fx)), gy),
                            __hmul2_rn(__hadd2_rn(__hmul2_rn(CH(2, 0, 0), gx), __hmul2_rn(CH(3, 0, 0), fx)), fy));
#undef CH
    __half2 light01 = H2(__low2float(lrg) / 255.0f, __high2float(lrg) / 255.0f);
    __half2 light2 = __half2half2(__float2half_rn(__low2float(lb) / 255.0f));
    __half2 t01 = H2(tex[0], tex[1]), t23 = H2(tex[2], tex[3]);
    __half2 ta = __hmul2_rn(t23, c23);                              /* (b's product, the alpha) */
    float alpha = __high2float(ta);
    if (alpha <= (render_pass ? 0.1f : 0.5f)) return 0;
    *alpha_out = alpha;
    float fog;
    if (P.fogm == 9729) fog = clamp01((P.foge - distance) / (P.foge - P.fogs));
    else if (P.fogm == 2048) fog = clamp01(glibc_expf(-P.fogd * distance));
    else {
        float d = P.fogd * distance;
        fog = clamp01(glibc_expf(-d * d));
    }
    __half2 fh = __half2half2(__float2half_rn(fog)), gh = __hsub2_rn(one, fh);
    __half2 v01 = __hmul2_rn(__hmul2_rn(t01, c01), light01), v2 = __hmul2_rn(ta, light2);
    __half2 s01 = __hadd2_rn(__hmul2_rn(v01, fh), __hmul2_rn(H2(P.fog[0], P.fog[1]), gh));
    __half2 s2 = __hadd2_rn(__hmul2_rn(v2, fh), __hmul2_rn(H2(P.fog[2], P.fog[2]), gh));
    src[0] = __low2float(s01); src[1] = __high2float(s01); src[2] = __low2float(s2);
    return 1;
}

/* raster.c shade_h: a terrain pixel's shading with raster_prec.h's float16
 * stages; 0 when the alpha test drops it */
__device__ __forceinline__ int shade_h(const tri &T, const envp &P, int p, int render_pass, const float q[3],
                                       float distance, const float tex[4], float src[3], float *alpha_out)
{
    float col[4] = {0, 0, 0, 0}, lc[2] = {0, 0}, light[3];
    if (p & RP_COLOR) {
        float qh[3] = {hr(q[0]), hr(q[1]), hr(q[2])};
        for (int c = 0; c < 4; ++c) {
            float a = HM(qh[0], hr(T.c[0][c]));
            a = HA(a, HM(qh[1], hr(T.c[1][c])));
            col[c] = HA(a, HM(qh[2], hr(T.c[2][c])));
        }
    } else
        for (int i = 0; i < 3; ++i) for (int c = 0; c < 4; ++c) col[c] += q[i] * T.c[i][c];
    if (p & RP_LIGHT) {
        float qh[3] = {hr(q[0]), hr(q[1]), hr(q[2])};
        for (int c = 0; c < 2; ++c) {
            float a = HM(qh[0], hr(T.l[0][c]));
            a = HA(a, HM(qh[1], hr(T.l[1][c])));
            lc[c] = HA(a, HM(qh[2], hr(T.l[2][c])));
        }
        light_sample_h(P.lm, lc[0], lc[1], light);
    } else {
        for (int i = 0; i < 3; ++i) for (int c = 0; c < 2; ++c) lc[c] += q[i] * T.l[i][c];
        light_sample(P.lm, lc[0], lc[1], light);
    }
    float alpha = p & RP_SHADE ? HM(hr(tex[3]), hr(col[3])) : tex[3] * col[3];
    if (alpha <= (render_pass ? 0.1f : 0.5f)) return 0;
    *alpha_out = alpha;
    float fog;
    if (P.fogm == 9729) fog = clamp01((P.foge - distance) / (P.foge - P.fogs));
    else if (P.fogm == 2048) fog = clamp01(glibc_expf(-P.fogd * distance));
    else {
        float d = P.fogd * distance;
        fog = clamp01(glibc_expf(-d * d));
    }
    for (int c = 0; c < 3; ++c) {
        float value = p & RP_SHADE ? HM(HM(hr(tex[c]), hr(col[c])), hr(light[c])) : tex[c] * col[c] * light[c];
        if (p & RP_FOG) {
            float fh = hr(fog);
            src[c] = HA(HM(hr(value), fh), HM(hr(P.fog[c]), HS(1, fh)));
        } else
            src[c] = value * fog + P.fog[c] * (1 - fog);
    }
    return 1;
}

/* triangle()'s shading and write with raster_prec.h's float16 stages (any
 * of them) */
__device__ __forceinline__ void pixel_h(const tri &T, const envp &P, int pr, int render_pass, const float q[3],
                                        float distance, const float tex[4], float z, unsigned char rgb[3], float &depth)
{
    float src[3], alpha;
    if ((pr & RP_HALF) == RP_HALF ? !shade_all_h(T, P, pr == RP_FAST, render_pass, q, distance, tex, src, &alpha)
                                  : !shade_h(T, P, pr, render_pass, q, distance, tex, src, &alpha)) return;
    for (int c = 0; c < 3; ++c) {
        if (render_pass) {
            float src8 = byte_(src[c]) / 255.0f;
            float alpha8 = byte_(alpha) / 255.0f;
            rgb[c] = byte_(src8 * alpha8 + (rgb[c] / 255.0f) * (1.0f - alpha8));
        } else {
            rgb[c] = byte_(src[c]);
        }
    }
    if (!render_pass) depth = z;
}
/* The pixel functions below run only where raster's cover_mask says the
 * triangle may draw, a subset of its box: the box test each of their C
 * functions starts with (its loop over the box) is the caller's. */

/* raster.c triangle(), for the one pixel (px, py) */
__device__ __forceinline__ void pixel(const tri &T, const envp &P, const texes &X, int pr, int px, int py,
                                      unsigned char rgb[3], float &depth)
{
    float sx = px + 0.5f, sy = py + 0.5f;
    float e0 = edge_xy(T.sx[1], T.sy[1], T.sx[2], T.sy[2], sx, sy);
    float e1 = edge_xy(T.sx[2], T.sy[2], T.sx[0], T.sy[0], sx, sy);
    float e2 = edge_xy(T.sx[0], T.sy[0], T.sx[1], T.sy[1], sx, sy);
    if (e0 < 0 || e1 < 0 || e2 < 0) return;
    /* top_left(a, b): dy < 0 || (dy == 0 && dx > 0) */
#define TL(a, b) ((T.sy[b] - T.sy[a]) < 0 || ((T.sy[b] - T.sy[a]) == 0 && (T.sx[b] - T.sx[a]) > 0))
    if ((e0 == 0 && !TL(1, 2)) || (e1 == 0 && !TL(2, 0)) || (e2 == 0 && !TL(0, 1))) return;
#undef TL
    float area = T.area;
    /* raster_prec.h RP_RCP (not the sky's): T holds 1 / area and 1 / w (the triangle's writer) */
    const bool rcp = (pr & RP_RCP) && !(T.mode & 15);
    float b[3], bw[3] = {0, 0, 0};
    if (rcp) {
        b[0] = edge_xy(T.x[1], T.y[1], T.x[2], T.y[2], sx, sy) * area;
        b[1] = edge_xy(T.x[2], T.y[2], T.x[0], T.y[0], sx, sy) * area;
        b[2] = edge_xy(T.x[0], T.y[0], T.x[1], T.y[1], sx, sy) * area;
    } else {
        b[0] = edge_xy(T.x[1], T.y[1], T.x[2], T.y[2], sx, sy) / area;
        b[1] = edge_xy(T.x[2], T.y[2], T.x[0], T.y[0], sx, sy) / area;
        b[2] = edge_xy(T.x[0], T.y[0], T.x[1], T.y[1], sx, sy) / area;
    }
    float z = b[0] * T.d[0] + b[1] * T.d[1] + b[2] * T.d[2];
    if (z > depth) return;
    float iw;
    if (rcp) {
        for (int i = 0; i < 3; ++i) bw[i] = b[i] * T.w[i];
        iw = bw[0] + bw[1] + bw[2];
    } else
        iw = b[0] / T.w[0] + b[1] / T.w[1] + b[2] / T.w[2];
    if (iw <= 0) return;
    int sky_mode = T.mode & 15, prepass = (T.mode >> 4) & 1, render_pass = (T.mode >> 5) & 1;
    const raster_obs_sky &k = P.sky;
    if (sky_mode) {
        if (sky_mode == 6) {
            float q[3] = {b[0] / T.w[0] / iw, b[1] / T.w[1] / iw, b[2] / T.w[2] / iw};
            float alpha = q[0] * T.c[0][3] + q[1] * T.c[1][3] + q[2] * T.c[2][3];
            alpha = byte_(alpha) / 255.0f;
            for (int c = 0; c < 3; ++c) {
                float src = q[0] * T.c[0][c] + q[1] * T.c[1][c] + q[2] * T.c[2][c];
                float src8 = byte_(src) / 255.0f;
                rgb[c] = byte_(src8 * alpha + (rgb[c] / 255.0f) * (1.0f - alpha));
            }
            return;
        }
        if (sky_mode == 8) {
            int p = byte_(T.c[0][0]) * byte_(T.c[0][3]);
            int add = (p + 128 + ((p + 128) >> 8)) >> 8;
            for (int c = 0; c < 3; ++c) {
                int d = rgb[c] + add;
                rgb[c] = (unsigned char)(d > 255 ? 255 : d);
            }
            return;
        }
        if (sky_mode == 4 || sky_mode == 5) {
            float u = (b[0] / T.w[0] * T.u[0] + b[1] / T.w[1] * T.u[1] + b[2] / T.w[2] * T.u[2]) / iw;
            float t = (b[0] / T.w[0] * T.v[0] + b[1] / T.w[1] * T.v[1] + b[2] / T.w[2] * T.v[2]) / iw;
            int w = sky_mode == 4 ? 32 : 128, h = sky_mode == 4 ? 32 : 64;
            const unsigned char *tex0 = sky_mode == 4 ? X.sun : X.moon;
            int tx = f2i(floorf(u * w)), ty = f2i(floorf(t * h));
            tx = wrap(tx, w); ty = wrap(ty, h);
            const unsigned char *tex = tex0 + ((size_t)ty * w + tx) * 4;
            for (int c = 0; c < 3; ++c) rgb[c] = byte_(rgb[c] / 255.0f + tex[c] / 255.0f);
            return;
        }
        if (sky_mode == 7) {
            float u = (b[0] / T.w[0] * T.u[0] + b[1] / T.w[1] * T.u[1] + b[2] / T.w[2] * T.u[2]) / iw;
            float t = (b[0] / T.w[0] * T.v[0] + b[1] / T.w[1] * T.v[1] + b[2] / T.w[2] * T.v[2]) / iw;
            int tx = f2i(floorf(u * 128.0f)), ty = f2i(floorf(t * 128.0f));
            tx = wrap(tx, 128); ty = wrap(ty, 128);
            const unsigned char *tex = X.end_sky + ((size_t)ty * 128 + tx) * 3;
            for (int c = 0; c < 3; ++c) rgb[c] = byte_((tex[c] / 255.0f) * (40.0f / 255.0f));
            return;
        }
        float distance = (b[0] / T.w[0] * T.fog[0] + b[1] / T.w[1] * T.fog[1] + b[2] / T.w[2] * T.fog[2]) / iw;
        float fog = sky_mode == 1 || sky_mode == 2 ? clamp01(1.0f - distance / k.sky_far) : 1.0f;
        if (sky_mode == 3) {
            float u = (b[0] / T.w[0] * T.u[0] + b[1] / T.w[1] * T.u[1] + b[2] / T.w[2] * T.u[2]) / iw;
            float t = (b[0] / T.w[0] * T.v[0] + b[1] / T.w[1] * T.v[1] + b[2] / T.w[2] * T.v[2]) / iw;
            int tx = f2i(floorf(u * 256.0f)), ty = f2i(floorf(t * 256.0f));
            tx = wrap(tx, 256); ty = wrap(ty, 256);
            const unsigned char *tex = X.clouds + ((size_t)ty * 256 + tx) * 4;
            if (tex[3] < 32) return;
            if (prepass) { depth = z; return; }
            fog = clamp01((k.sky_far - distance) / (k.sky_far * 0.25f));
            for (int c = 0; c < 3; ++c) {
                float col = (tex[c] / 255.0f) * k.cloud_color[c] * T.c[0][0];
                float src = col * fog + P.fog[c] * (1.0f - fog);
                float src8 = byte_(src) / 255.0f;
                rgb[c] = byte_(src8 * 0.8f + (rgb[c] / 255.0f) * 0.2f);
            }
            return;
        }
        const float *base = sky_mode == 1 ? k.sky : k.sky_bottom;
        for (int c = 0; c < 3; ++c) rgb[c] = byte_(base[c] * fog + P.fog[c] * (1.0f - fog));
        return;
    }
    float q[3], ri = 0;
    if (rcp) {
        ri = 1.0f / iw;
        for (int i = 0; i < 3; ++i) q[i] = bw[i] * ri;
    } else {
        q[0] = b[0] / T.w[0] / iw; q[1] = b[1] / T.w[1] / iw; q[2] = b[2] / T.w[2] / iw;
    }
    float u = q[0] * T.u[0] + q[1] * T.u[1] + q[2] * T.u[2];
    float t = q[0] * T.v[0] + q[1] * T.v[1] + q[2] * T.v[2];
    /* raster_texture_sample at level 0 (texel) */
    int tx = f2i(floorf(u * X.aw)), ty = f2i(floorf(t * X.ah));
    tx = wrap(tx, X.aw); ty = wrap(ty, X.ah);
    const unsigned char *p = X.atlas + ((size_t)ty * X.aw + tx) * 4;
    float tex[4];
    for (int c = 0; c < 4; ++c) tex[c] = p[c] / 255.0f;
    if (pr & RP_HALF) {
        float distance = rcp ? (bw[0] * T.fog[0] + bw[1] * T.fog[1] + bw[2] * T.fog[2]) * ri
                             : (b[0] / T.w[0] * T.fog[0] + b[1] / T.w[1] * T.fog[1] + b[2] / T.w[2] * T.fog[2]) / iw;
        pixel_h(T, P, pr, render_pass, q, distance, tex, z, rgb, depth);
        return;
    }
    float col[4] = {0, 0, 0, 0};
    float lc[2] = {0, 0}, light[3];
    for (int i = 0; i < 3; ++i) {
        for (int c = 0; c < 4; ++c) col[c] += q[i] * T.c[i][c];
        for (int c = 0; c < 2; ++c) lc[c] += q[i] * T.l[i][c];
    }
    light_sample(P.lm, lc[0], lc[1], light);
    float alpha = tex[3] * col[3];
    if (alpha <= (render_pass ? 0.1f : 0.5f)) return;
    float distance = rcp ? (bw[0] * T.fog[0] + bw[1] * T.fog[1] + bw[2] * T.fog[2]) * ri
                             : (b[0] / T.w[0] * T.fog[0] + b[1] / T.w[1] * T.fog[1] + b[2] / T.w[2] * T.fog[2]) / iw;
    float fog;
    if (P.fogm == 9729) fog = clamp01((P.foge - distance) / (P.foge - P.fogs));
    else if (P.fogm == 2048) fog = clamp01(glibc_expf(-P.fogd * distance));
    else {
        float d = P.fogd * distance;
        fog = clamp01(glibc_expf(-d * d));
    }
    for (int c = 0; c < 3; ++c) {
        float value = tex[c] * col[c] * light[c];
        float src = value * fog + P.fog[c] * (1 - fog);
        if (render_pass) {
            float src8 = byte_(src) / 255.0f;
            float alpha8 = byte_(alpha) / 255.0f;
            rgb[c] = byte_(src8 * alpha8 + (rgb[c] / 255.0f) * (1.0f - alpha8));
        } else {
            rgb[c] = byte_(src);
        }
    }
    if (!render_pass) depth = z;
}

/* raster_entities.c light_sample: GL_CLAMP's black border past the edge */
__device__ void e_light(const uint32_t *lm, float block, float sky, float out[3])
{
    float x = block / 16.0f, y = sky / 16.0f;
    if (x > 15.5f) x = 15.5f;
    if (y > 15.5f) y = 15.5f;
    if (x > 15.0f && x < 15.0f + 1.0f / 256.0f) x = 15.0f;
    if (y > 15.0f && y < 15.0f + 1.0f / 256.0f) y = 15.0f;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    int x0 = f2i(x), y0 = f2i(y);
    int x1 = x0 + 1, y1 = y0 + 1;
    float fx = x - x0, fy = y - y0;
#define LM(i) ((unsigned)(i) < 256u ? lm[i] : 0u)
    uint32_t c[4] = {LM(y0 * 16 + (x0 > 15 ? 15 : x0)), x1 > 15 ? 0u : LM(y0 * 16 + x1),
                     y1 > 15 ? 0u : LM(y1 * 16 + (x0 > 15 ? 15 : x0)),
                     x1 > 15 || y1 > 15 ? 0u : LM(y1 * 16 + x1)};
#undef LM
    for (int i = 0; i < 3; ++i) {
        int shift = 8 * (2 - i);
        float a = ((c[0] >> shift) & 255) * (1 - fx) + ((c[1] >> shift) & 255) * fx;
        float b = ((c[2] >> shift) & 255) * (1 - fx) + ((c[3] >> shift) & 255) * fx;
        out[i] = (a * (1 - fy) + b * fy) / 255.0f;
    }
}

/* raster_entities.c light_sample_h (raster_prec.h RP_LIGHT) */
__device__ void e_light_h(const uint32_t *lm, float block, float sky, float out[3])
{
    float x = HM(block, 0.0625f), y = HM(sky, 0.0625f);
    if (x > 15.5f) x = 15.5f;
    if (y > 15.5f) y = 15.5f;
    if (x > 15.0f && x < 15.0f + 1.0f / 256.0f) x = 15.0f;
    if (y > 15.0f && y < 15.0f + 1.0f / 256.0f) y = 15.0f;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    int x0 = f2i(x), y0 = f2i(y);
    int x1 = x0 + 1, y1 = y0 + 1;
    float fx = hr(x - x0), fy = hr(y - y0), gx = HS(1, fx), gy = HS(1, fy);
#define LM(i) ((unsigned)(i) < 256u ? lm[i] : 0u)
    uint32_t c[4] = {LM(y0 * 16 + (x0 > 15 ? 15 : x0)), x1 > 15 ? 0u : LM(y0 * 16 + x1),
                     y1 > 15 ? 0u : LM(y1 * 16 + (x0 > 15 ? 15 : x0)),
                     x1 > 15 || y1 > 15 ? 0u : LM(y1 * 16 + x1)};
#undef LM
    for (int i = 0; i < 3; ++i) {
        int shift = 8 * (2 - i);
        float a = HA(HM((float)((c[0] >> shift) & 255), gx), HM((float)((c[1] >> shift) & 255), fx));
        float b = HA(HM((float)((c[2] >> shift) & 255), gx), HM((float)((c[3] >> shift) & 255), fx));
        out[i] = hr(HA(HM(a, gy), HM(b, fy)) / 255.0f);
    }
}

/* raster_entities.c interp_h */
__device__ __forceinline__ float interp_h(const float qh[3], float a0, float a1, float a2)
{
    float a = HM(qh[0], hr(a0));
    a = HA(a, HM(qh[1], hr(a1)));
    return HA(a, HM(qh[2], hr(a2)));
}

__device__ __forceinline__ int tl(float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay;
    return dy < 0 || (dy == 0 && dx > 0);
}

/* raster_entities.c's PI: a vertex attribute's perspective interpolation,
 * exact or by the reciprocals (RP_RCP) */
#define PI(A, S) (rcp ? (bw[0] * T.A[0] S + bw[1] * T.A[1] S + bw[2] * T.A[2] S) * ri \
                      : (b[0] / T.w[0] * T.A[0] S + b[1] / T.w[1] * T.A[1] S + b[2] / T.w[2] * T.A[2] S) / iw)

/* e_pixel past the alpha test with raster_prec.h's float16 stages (any of
 * them) */
__device__ __forceinline__ void e_pixel_h(const tri &T, const texes &X, const raster_obs_estate &st, int pr, const float b[3],
                                          float iw, bool rcp, const float bw[3], float ri, float z, const unsigned char *ptex,
                                          unsigned char rgb[3], float &depth)
{
    float distance = PI(fog, );
    float ff = 1.0f;
    if (st.fogm == 9729) ff = clamp01((st.foge - distance) / (st.foge - st.fogs));
    else if (st.fogm == 2048) ff = clamp01(glibc_expf(-st.fogd * distance));
    else if (st.fogm == 2049) {
        float d = st.fogd * distance;
        ff = clamp01(glibc_expf(-d * d));
    }
    /* every float16 stage on (the fast mode): the device's own halves,
     * the operations raster_triangle rounds, in its order */
    const bool allh = (pr & RP_HALF) == RP_HALF;
    float hsrc[3], valpha = 1.0f;
    if (allh) {
        const __half q0 = __float2half_rn(rcp ? bw[0] * ri : b[0] / T.w[0] / iw),
                     q1 = __float2half_rn(rcp ? bw[1] * ri : b[1] / T.w[1] / iw),
                     q2 = __float2half_rn(rcp ? bw[2] * ri : b[2] / T.w[2] / iw);
        const __half one = __ushort_as_half((unsigned short)0x3c00);
#define IHH(a0, a1, a2) __hadd_rn(__hadd_rn(__hmul_rn(q0, a0), __hmul_rn(q1, a1)), __hmul_rn(q2, a2))
#define IH(a0, a1, a2) IHH(__float2half_rn(a0), __float2half_rn(a1), __float2half_rn(a2))
        /* packed (prec RP_FAST): e_tri stored the colour and light as halves */
        const bool packed = pr == RP_FAST;
        const __half2 *ch = tri_ch(T), *lh = tri_lh(T);
        __half light[3];
        if (st.no_light) light[0] = light[1] = light[2] = one;
        else {
            /* e_light_h */
            const uint32_t *lm = X.lm[X.lm_base + st.lm];
            const __half s16 = __ushort_as_half((unsigned short)0x2c00);
            float x = __half2float(__hmul_rn(packed ? IHH(__low2half(lh[0]), __low2half(lh[1]), __low2half(lh[2]))
                                                    : IH(T.l[0][0], T.l[1][0], T.l[2][0]), s16));
            float y = __half2float(__hmul_rn(packed ? IHH(__high2half(lh[0]), __high2half(lh[1]), __high2half(lh[2]))
                                                    : IH(T.l[0][1], T.l[1][1], T.l[2][1]), s16));
            if (x > 15.5f) x = 15.5f;
            if (y > 15.5f) y = 15.5f;
            if (x > 15.0f && x < 15.0f + 1.0f / 256.0f) x = 15.0f;
            if (y > 15.0f && y < 15.0f + 1.0f / 256.0f) y = 15.0f;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            int x0 = f2i(x), y0 = f2i(y);
            int x1 = x0 + 1, y1 = y0 + 1;
            __half fx = __float2half_rn(x - x0), fy = __float2half_rn(y - y0);
            __half gx = __hsub_rn(one, fx), gy = __hsub_rn(one, fy);
#define LM(i) ((unsigned)(i) < 256u ? lm[i] : 0u)
            uint32_t c[4] = {LM(y0 * 16 + (x0 > 15 ? 15 : x0)), x1 > 15 ? 0u : LM(y0 * 16 + x1),
                             y1 > 15 ? 0u : LM(y1 * 16 + (x0 > 15 ? 15 : x0)),
                             x1 > 15 || y1 > 15 ? 0u : LM(y1 * 16 + x1)};
#undef LM
            for (int i = 0; i < 3; ++i) {
                int shift = 8 * (2 - i);
#define CB(k) __ushort2half_rn((unsigned short)((c[k] >> shift) & 255))
                __half a = __hadd_rn(__hmul_rn(CB(0), gx), __hmul_rn(CB(1), fx));
                __half bb = __hadd_rn(__hmul_rn(CB(2), gx), __hmul_rn(CB(3), fx));
#undef CB
                light[i] = __float2half_rn(__half2float(__hadd_rn(__hmul_rn(a, gy), __hmul_rn(bb, fy))) / 255.0f);
            }
        }
        __half dh = st.unlit ? one : IH(T.dif[0], T.dif[1], T.dif[2]);
        __half vcol[3] = {one, one, one};
        if (st.vertex_color) {
            if (packed) {
                vcol[0] = IHH(__low2half(ch[0]), __low2half(ch[2]), __low2half(ch[4]));
                vcol[1] = IHH(__high2half(ch[0]), __high2half(ch[2]), __high2half(ch[4]));
                vcol[2] = IHH(__low2half(ch[1]), __low2half(ch[3]), __low2half(ch[5]));
                valpha = __half2float(IHH(__high2half(ch[1]), __high2half(ch[3]), __high2half(ch[5])));
            } else {
                for (int c = 0; c < 3; ++c) vcol[c] = IH(T.c[0][c], T.c[1][c], T.c[2][c]);
                valpha = __half2float(IH(T.c[0][3], T.c[1][3], T.c[2][3]));
            }
        }
#undef IH
#undef IHH
        const __half fh = __float2half_rn(ff), gh = __hsub_rn(one, fh);
        for (int c = 0; c < 3; ++c) {
            __half shade = st.overlay ? (st.overlay_has_rgb ? __hmul_rn(__float2half_rn(st.overlay_rgb[c]), dh) :
                                         c == 0 ? __hmul_rn(__float2half_rn(st.overlay_brightness), dh) : __ushort_as_half(0))
                                      : __hmul_rn(__float2half_rn(st.material[c]), dh);
            if (st.vertex_color) shade = __hmul_rn(shade, vcol[c]);
            shade = __float2half_rn(clamp01(__half2float(shade)));
            __half val = st.overlay ? (st.overlay_lightmap ? __hmul_rn(shade, light[c]) : shade)
                                    : __hmul_rn(__hmul_rn(__float2half_rn(ptex[c] / 255.0f), light[c]), shade);
            hsrc[c] = __half2float(__hadd_rn(__hmul_rn(val, fh), __hmul_rn(__float2half_rn(st.fog[c]), gh)));
        }
    }
    float qh[3] = {0, 0, 0};
    if ((pr & RP_HALF) && !allh)
        for (int i = 0; i < 3; ++i) qh[i] = hr(rcp ? bw[i] * ri : b[i] / T.w[i] / iw);
    float light[3], diffuse = 1.0f, vcol[3] = {1.0f, 1.0f, 1.0f};
    if (allh) {}
    else if (st.no_light) light[0] = light[1] = light[2] = 1.0f;
    else if (pr & RP_LIGHT)
        e_light_h(X.lm[X.lm_base + st.lm], interp_h(qh, T.l[0][0], T.l[1][0], T.l[2][0]),
                  interp_h(qh, T.l[0][1], T.l[1][1], T.l[2][1]), light);
    else {
        float l0 = PI(l, [0]);
        float l1 = PI(l, [1]);
        e_light(X.lm[X.lm_base + st.lm], l0, l1, light);
    }
    if (!allh) {
        diffuse = pr & RP_SHADE ? interp_h(qh, T.dif[0], T.dif[1], T.dif[2])
                                : PI(dif, );
        if (st.unlit) diffuse = 1.0f;
    }
    if (allh) {}
    else if (st.vertex_color && (pr & RP_COLOR)) {
        for (int c = 0; c < 3; ++c) vcol[c] = interp_h(qh, T.c[0][c], T.c[1][c], T.c[2][c]);
        valpha = interp_h(qh, T.c[0][3], T.c[1][3], T.c[2][3]);
    } else if (st.vertex_color) {
        for (int c = 0; c < 3; ++c)
            vcol[c] = PI(c, [c]);
        valpha = PI(c, [3]);
    }
    for (int c = 0; c < 3; ++c) {
        float shade, val, src;
        if (allh) src = hsrc[c];
        else {
        if (pr & RP_SHADE) {
            float dh = hr(diffuse);
            shade = st.overlay ? (st.overlay_has_rgb ? HM(hr(st.overlay_rgb[c]), dh) :
                                  c == 0 ? HM(hr(st.overlay_brightness), dh) : 0.0f)
                               : HM(hr(st.material[c]), dh);
            if (st.vertex_color) shade = HM(shade, hr(vcol[c]));
            shade = clamp01(shade);
            val = st.overlay ? (st.overlay_lightmap ? HM(shade, hr(light[c])) : shade)
                             : HM(HM(hr(ptex[c] / 255.0f), hr(light[c])), shade);
        } else {
            shade = st.overlay ? (st.overlay_has_rgb ? st.overlay_rgb[c] * diffuse :
                                  c == 0 ? st.overlay_brightness * diffuse : 0.0f)
                               : st.material[c] * diffuse;
            if (st.vertex_color) shade *= vcol[c];
            shade = clamp01(shade);
            val = st.overlay ? (st.overlay_lightmap ? shade * light[c] : shade)
                             : (ptex[c] / 255.0f) * light[c] * shade;
        }
        if (pr & RP_FOG) {
            float fh = hr(ff);
            src = HA(HM(hr(val), fh), HM(hr(st.fog[c]), HS(1.0f, fh)));
        } else
            src = val * ff + st.fog[c] * (1.0f - ff);
        }
        if (st.overlay) src = src * st.overlay_alpha + rgb[c] / 255.0f * (1.0f - st.overlay_alpha);
        if (st.blend == 5 || st.blend == 6) {
            int s8 = byte_(src), f8 = s8;
            if (st.blend == 6) {
                float va = PI(al, );
                f8 = byte_(va * ptex[3] / 255.0f);
            }
            int prod = s8 * f8;
            int add = (prod + 128 + ((prod + 128) >> 8)) >> 8;
            int d = rgb[c] + add;
            rgb[c] = (unsigned char)(d > 255 ? 255 : d);
            continue;
        }
        if (st.blend == 4) src = fminf(1.0f, src * valpha + rgb[c] / 255.0f);
        else if (st.blend == 2 || st.blend == 7) src = fminf(1.0f, src + rgb[c] / 255.0f);
        else if (st.blend) {
            float va = st.vertex_alpha
                ? PI(al, )
                : st.alpha;
            float a = va * ptex[3] / 255.0f;
            src = src * a + rgb[c] / 255.0f * (1.0f - a);
        }
        rgb[c] = byte_(src);
    }
    if (!st.overlay && (!st.blend || st.blend == 3 || st.blend == 6 || st.blend == 7) && !st.depth_equal) depth = z;
}


/* raster_entities.c raster_triangle, for one pixel */
__device__ __forceinline__ void e_pixel(const tri &T, const texes &X, int pr, int px, int py, unsigned char rgb[3], float &depth)
{
    const raster_obs_estate &st = X.estate[T.aux];
    float sx = px + 0.5f, sy = py + 0.5f;
    int e0, e1, e2;
    if (pr & RP_EDGE) {
        e0 = edge_fsign(edge_xy(T.sx[1], T.sy[1], T.sx[2], T.sy[2], sx, sy));
        e1 = edge_fsign(edge_xy(T.sx[2], T.sy[2], T.sx[0], T.sy[0], sx, sy));
        e2 = edge_fsign(edge_xy(T.sx[0], T.sy[0], T.sx[1], T.sy[1], sx, sy));
    } else {
        e0 = edge_sign(T.sx[1], T.sy[1], T.sx[2], T.sy[2], sx, sy);
        e1 = edge_sign(T.sx[2], T.sy[2], T.sx[0], T.sy[0], sx, sy);
        e2 = edge_sign(T.sx[0], T.sy[0], T.sx[1], T.sy[1], sx, sy);
    }
    if (e0 < 0 || (e0 == 0 && !tl(T.sx[1], T.sy[1], T.sx[2], T.sy[2]))) return;
    if (e1 < 0 || (e1 == 0 && !tl(T.sx[2], T.sy[2], T.sx[0], T.sy[0]))) return;
    if (e2 < 0 || (e2 == 0 && !tl(T.sx[0], T.sy[0], T.sx[1], T.sy[1]))) return;
    float area = T.area;
    /* raster_prec.h RP_RCP: T holds 1 / area and 1 / w (e_tri) */
    const bool rcp = (pr & RP_RCP) != 0;
    float b[3], bw[3] = {0, 0, 0}, iw, ri = 0;
    if (rcp) {
        b[0] = edge_xy(T.x[1], T.y[1], T.x[2], T.y[2], sx, sy) * area;
        b[1] = edge_xy(T.x[2], T.y[2], T.x[0], T.y[0], sx, sy) * area;
        b[2] = edge_xy(T.x[0], T.y[0], T.x[1], T.y[1], sx, sy) * area;
        for (int i = 0; i < 3; ++i) bw[i] = b[i] * T.w[i];
        iw = bw[0] + bw[1] + bw[2];
        ri = 1.0f / iw;
    } else {
        b[0] = edge_xy(T.x[1], T.y[1], T.x[2], T.y[2], sx, sy) / area;
        b[1] = edge_xy(T.x[2], T.y[2], T.x[0], T.y[0], sx, sy) / area;
        b[2] = edge_xy(T.x[0], T.y[0], T.x[1], T.y[1], sx, sy) / area;
        iw = b[0] / T.w[0] + b[1] / T.w[1] + b[2] / T.w[2];
    }
    float z = st.linear_depth
                  ? b[0] * T.d[0] + b[1] * T.d[1] + b[2] * T.d[2]
                  : PI(d, );
    if (z < 0.0f || (st.depth_equal ? fabsf(z - depth) > 1e-7f :
                     st.overlay ? fabsf(z - depth) > 1e-7f :
                     st.blend || st.depth_lequal ? z > depth : z >= depth)) return;
    float u = PI(u, );
    float vc = PI(v, );
    const dtex &tx = X.tex[T.tex];
    int tu = f2i(floorf(u * tx.w)), tv = f2i(floorf(vc * tx.h));
    if (st.clamp_texture) {
        if (tu < 0) tu = 0; else if (tu >= tx.w) tu = tx.w - 1;
        if (tv < 0) tv = 0; else if (tv >= tx.h) tv = tx.h - 1;
    } else {
        tu %= tx.w; tv %= tx.h;
        if (tu < 0) tu += tx.w;
        if (tv < 0) tv += tx.h;
    }
    const unsigned char *ptex = X.pool + tx.off + (size_t)(tv * tx.w + tu) * 4;
    if (!st.overlay && !st.no_alpha) {
        float ref = st.alpha_cut ? st.alpha_cut * (1.0f / 255.0f) : (st.alpha_ref > 0.0f ? st.alpha_ref : 0.1f);
        if (ptex[3] * (1.0f / 255.0f) <= ref) return;
    }
    if (pr & RP_HALF) {
        e_pixel_h(T, X, st, pr, b, iw, rcp, bw, ri, z, ptex, rgb, depth);
        return;
    }
    float l0 = PI(l, [0]);
    float l1 = PI(l, [1]);
    float light[3];
    if (st.no_light) light[0] = light[1] = light[2] = 1.0f;
    else e_light(X.lm[X.lm_base + st.lm], l0, l1, light);
    float diffuse = PI(dif, );
    if (st.unlit) diffuse = 1.0f;
    float distance = PI(fog, );
    float ff = 1.0f;
    if (st.fogm == 9729) ff = clamp01((st.foge - distance) / (st.foge - st.fogs));
    else if (st.fogm == 2048) ff = clamp01(glibc_expf(-st.fogd * distance));
    else if (st.fogm == 2049) {
        float d = st.fogd * distance;
        ff = clamp01(glibc_expf(-d * d));
    }
    float vcol[3] = {1.0f, 1.0f, 1.0f}, valpha = 1.0f;
    if (st.vertex_color) {
        for (int c = 0; c < 3; ++c)
            vcol[c] = PI(c, [c]);
        valpha = PI(c, [3]);
    }
    for (int c = 0; c < 3; ++c) {
        float shade = st.overlay ? (st.overlay_has_rgb ? st.overlay_rgb[c] * diffuse :
                                    c == 0 ? st.overlay_brightness * diffuse : 0.0f)
                                 : st.material[c] * diffuse;
        if (st.vertex_color) shade *= vcol[c];
        shade = clamp01(shade);
        float val = st.overlay ? (st.overlay_lightmap ? shade * light[c] : shade)
                               : (ptex[c] / 255.0f) * light[c] * shade;
        float src = val * ff + st.fog[c] * (1.0f - ff);
        if (st.overlay) src = src * st.overlay_alpha + rgb[c] / 255.0f * (1.0f - st.overlay_alpha);
        if (st.blend == 5 || st.blend == 6) {
            int s8 = byte_(src), f8 = s8;
            if (st.blend == 6) {
                float va = PI(al, );
                f8 = byte_(va * ptex[3] / 255.0f);
            }
            int prod = s8 * f8;
            int add = (prod + 128 + ((prod + 128) >> 8)) >> 8;
            int d = rgb[c] + add;
            rgb[c] = (unsigned char)(d > 255 ? 255 : d);
            continue;
        }
        if (st.blend == 4) src = fminf(1.0f, src * valpha + rgb[c] / 255.0f);
        else if (st.blend == 2 || st.blend == 7) src = fminf(1.0f, src + rgb[c] / 255.0f);
        else if (st.blend) {
            float va = st.vertex_alpha
                ? PI(al, )
                : st.alpha;
            float a = va * ptex[3] / 255.0f;
            src = src * a + rgb[c] / 255.0f * (1.0f - a);
        }
        rgb[c] = byte_(src);
    }
    if (!st.overlay && (!st.blend || st.blend == 3 || st.blend == 6 || st.blend == 7) && !st.depth_equal) depth = z;
}

#undef PI

/* raster_overlay.c quant */
__device__ __forceinline__ unsigned char quant(float v)
{
    if (v <= 0) return 0;
    if (v >= 1) return 255;
    float y = v * 255.0f;
    if (y != y) return 0;
    return (unsigned char)__float2int_rn(y);
}

/* raster_overlay.c crack_triangle, for one pixel (the atlas at level 0) */
__device__ void crack_pixel(const tri &T, const texes &X, int px, int py, unsigned char rgb[3], float &depth)
{
    const raster_obs_crack &k = X.crack[T.aux];
    float sx = px + 0.5f, sy = py + 0.5f;
    float e0 = edge_xy(T.x[1], T.y[1], T.x[2], T.y[2], sx, sy), e1 = edge_xy(T.x[2], T.y[2], T.x[0], T.y[0], sx, sy),
          e2 = edge_xy(T.x[0], T.y[0], T.x[1], T.y[1], sx, sy);
    if (e0 < 0 || e1 < 0 || e2 < 0) return;
    if ((e0 == 0 && !tl(T.x[1], T.y[1], T.x[2], T.y[2])) || (e1 == 0 && !tl(T.x[2], T.y[2], T.x[0], T.y[0])) ||
        (e2 == 0 && !tl(T.x[0], T.y[0], T.x[1], T.y[1]))) return;
    float area = T.area;
    float ba = e0 / area, bb = e1 / area, bc = e2 / area;
    float z = ba * T.d[0] + bb * T.d[1] + bc * T.d[2] - 0.00002f;
    if (z > depth + 1e-6f) return;
    /* texcoord() */
    float wa = edge_xy(T.x[1], T.y[1], T.x[2], T.y[2], sx, sy) / area / T.w[0];
    float wb = edge_xy(T.x[2], T.y[2], T.x[0], T.y[0], sx, sy) / area / T.w[1];
    float wc = edge_xy(T.x[0], T.y[0], T.x[1], T.y[1], sx, sy) / area / T.w[2];
    float ws = wa + wb + wc;
    float u = (wa * T.u[0] + wb * T.u[1] + wc * T.u[2]) / ws;
    float v = (wa * T.v[0] + wb * T.v[1] + wc * T.v[2]) / ws;
    int tx = f2i(floorf(u * X.aw)), ty = f2i(floorf(v * X.ah));
    tx = wrap(tx, X.aw); ty = wrap(ty, X.ah);
    const unsigned char *p = X.atlas + ((size_t)ty * X.aw + tx) * 4;
    float rgba[4];
    for (int c = 0; c < 4; ++c) rgba[c] = p[c] / 255.0f;
    if (rgba[3] * 0.5f <= 0.1f) return;
    float qa = ba / T.w[0], qb = bb / T.w[1], qc = bc / T.w[2];
    float d = (qa * T.fog[0] + qb * T.fog[1] + qc * T.fog[2]) / (qa + qb + qc);
    float fog = 1.0f;
    if (k.fog_on) {
        int known = 1;
        float f = 0;
        if (k.fogm == 9729) f = (k.foge - d) / (k.foge - k.fogs);
        else if (k.fogm == 2048) f = glibc_expf(-k.fogd * d);
        else if (k.fogm == 2049) { float e = k.fogd * d; f = glibc_expf(-e * e); }
        else known = 0;
        if (known) fog = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    }
    for (int ch = 0; ch < 3; ++ch) {
        float dst = rgb[ch] / 255.0f;
        float src = k.fog_on ? rgba[ch] * fog + k.fog[ch] * (1.0f - fog) : rgba[ch];
        rgb[ch] = quant(2.0f * src * dst);
    }
}

/* raster_tileent.c portal_triangle, for one pixel */
__device__ __forceinline__ void portal_pixel(const tri &T, const texes &X, int pr, int px, int py, unsigned char rgb[3], float &depth)
{
    const raster_obs_portal &p = X.portal[T.aux];
    float sx = px + 0.5f, sy = py + 0.5f;
    int e0, e1, e2;
    if (pr & RP_EDGE) {
        e0 = edge_fsign(edge_xy(T.sx[1], T.sy[1], T.sx[2], T.sy[2], sx, sy));
        e1 = edge_fsign(edge_xy(T.sx[2], T.sy[2], T.sx[0], T.sy[0], sx, sy));
        e2 = edge_fsign(edge_xy(T.sx[0], T.sy[0], T.sx[1], T.sy[1], sx, sy));
    } else {
        e0 = edge_sign(T.sx[1], T.sy[1], T.sx[2], T.sy[2], sx, sy);
        e1 = edge_sign(T.sx[2], T.sy[2], T.sx[0], T.sy[0], sx, sy);
        e2 = edge_sign(T.sx[0], T.sy[0], T.sx[1], T.sy[1], sx, sy);
    }
    if (e0 < 0 || (e0 == 0 && !tl(T.sx[1], T.sy[1], T.sx[2], T.sy[2]))) return;
    if (e1 < 0 || (e1 == 0 && !tl(T.sx[2], T.sy[2], T.sx[0], T.sy[0]))) return;
    if (e2 < 0 || (e2 == 0 && !tl(T.sx[0], T.sy[0], T.sx[1], T.sy[1]))) return;
    float area = T.area;
    float b[3] = {edge_xy(T.x[1], T.y[1], T.x[2], T.y[2], sx, sy) / area,
                  edge_xy(T.x[2], T.y[2], T.x[0], T.y[0], sx, sy) / area,
                  edge_xy(T.x[0], T.y[0], T.x[1], T.y[1], sx, sy) / area};
    float iw = b[0] / T.w[0] + b[1] / T.w[1] + b[2] / T.w[2];
    float z = (b[0] / T.w[0] * T.d[0] + b[1] / T.w[1] * T.d[1] + b[2] / T.w[2] * T.d[2]) / iw;
    if (z < 0.0f || z > depth) return;
    float ps = (b[0] / T.w[0] * T.u[0] + b[1] / T.w[1] * T.u[1] + b[2] / T.w[2] * T.u[2]) / iw;
    float pt = (b[0] / T.w[0] * T.v[0] + b[1] / T.w[1] * T.v[1] + b[2] / T.w[2] * T.v[2]) / iw;
    float pq = (b[0] / T.w[0] * T.dif[0] + b[1] / T.w[1] * T.dif[1] + b[2] / T.w[2] * T.dif[2]) / iw;
    float u = ps / pq, vv = pt / pq;
    const dtex &tx = X.tex[T.tex];
    int tu = f2i(floorf(u * tx.w)), tv = f2i(floorf(vv * tx.h));
    tu %= tx.w; tv %= tx.h;
    if (tu < 0) tu += tx.w;
    if (tv < 0) tv += tx.h;
    const unsigned char *texel = X.pool + tx.off + ((size_t)tv * tx.w + tu) * 4;
    float alpha = p.color[3] * (texel[3] / 255.0f);
    if (!(alpha > 0.1f)) return;
    float dist = (b[0] / T.w[0] * T.fog[0] + b[1] / T.w[1] * T.fog[1] + b[2] / T.w[2] * T.fog[2]) / iw;
    float fog = 1.0f;
    if (p.fogm == 9729) fog = (p.foge - dist) / (p.foge - p.fogs);
    else if (p.fogm == 2048) fog = glibc_expf(-p.fogd * dist);
    else if (p.fogm == 2049) { float dd = p.fogd * dist; fog = glibc_expf(-dd * dd); }
    fog = fog < 0.0f ? 0.0f : fog > 1.0f ? 1.0f : fog;
    for (int ch = 0; ch < 3; ++ch) {
        float col = p.color[ch] * (texel[ch] / 255.0f) * p.light[ch];
        col = col * fog + p.fog[ch] * (1.0f - fog);
        int src = byte_(col), dst = rgb[ch], out;
        if (p.additive) out = src + dst > 255 ? 255 : src + dst;
        else {
            int a8 = byte_(alpha), p0 = src * a8, p1 = dst * (255 - a8);
            out = ((p0 + 128 + ((p0 + 128) >> 8)) >> 8) + ((p1 + 128 + ((p1 + 128) >> 8)) >> 8);
            if (out > 255) out = 255;
        }
        rgb[ch] = (unsigned char)out;
    }
    depth = z;
}

/* raster_overlay.c line, for one pixel: the black outline fogged toward
 * the fog colour, blended at alpha 0.4, depth tested, never written */
__device__ void line_pixel(const tri &T, const texes &X, int px, int py, unsigned char rgb[3], float depth)
{
    const raster_obs_line &l = X.line[T.aux];
    float ax = T.x[0], ay = T.y[0], bx = T.x[1], by = T.y[1];
    float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    float qx = px + 0.5f - ax, qy = py + 0.5f - ay;
    float t = (qx * dx + qy * dy) / len2;
    if (t < 0 || t >= 1) return;
    float cross = fabsf(qx * dy - qy * dx);
    if (cross > sqrtf(len2)) return;
    float z = T.d[0] + t * (T.d[1] - T.d[0]);
    if (z > depth) return;
    float qa = (1.0f - t) / T.w[0], qb = t / T.w[1];
    float d = (qa * T.fog[0] + qb * T.fog[1]) / (qa + qb);
    /* fog_factor */
    float fog = 1.0f;
    if (l.fog_on) {
        int known = 1;
        float f = 0;
        if (l.fogm == 9729) f = (l.foge - d) / (l.foge - l.fogs);
        else if (l.fogm == 2048) f = glibc_expf(-l.fogd * d);
        else if (l.fogm == 2049) { float e = l.fogd * d; f = glibc_expf(-e * e); }
        else known = 0;
        if (known) fog = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    }
    for (int c = 0; c < 3; ++c) {
        float src = l.fog_on ? l.fog[c] * (1.0f - fog) : 0.0f;
        rgb[c] = quant(quant(src) / 255.0f * 0.4f + rgb[c] / 255.0f * 0.6f);
    }
}


/* The pixels of a warp's 8 x 4 block (bit (y - wy0) * 8 + x - wx0) that
 * triangle T may draw: every pixel function starts with T's box, and the
 * triangles' then with their edges: a small box's pixels are tested with
 * the same edge functions on the same values (without the tie rule: a
 * superset), a larger box, a depth clear, a snap and a line give the box.
 * In a heavy tile most triangles are a pixel or less and meet no pixel
 * centre (7.7 to 22% of those whose box met a warp's met one). */
__device__ __forceinline__ uint32_t cover_mask(const tri &T, int wx0, int wy0)
{
    int x0 = max((int)T.x0, wx0), x1 = min((int)T.x1, wx0 + 7), y0 = max((int)T.y0, wy0), y1 = min((int)T.y1, wy0 + 3);
    if (x0 > x1 || y0 > y1) return 0;
    uint32_t row = ((1u << (x1 - x0 + 1)) - 1) << (x0 - wx0), box = 0;
    for (int y = y0; y <= y1; ++y) box |= row << ((y - wy0) * 8);
    if ((x1 - x0 + 1) * (y1 - y0 + 1) > COVER_TEST_MAX) return box;
    uint32_t cov = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (may_cover(T, x, y)) cov |= 1u << ((y - wy0) * 8 + x - wx0);
    return cov;
}

/* PM: the frames' precision (dev.cuh RASTER_PM_*): every one exact
 * (the fast stages compiled out), any (each environment's own mask), every
 * one fast (compiled in) */
template <int PM>
__global__ void __launch_bounds__(RASTER_THREADS, RASTER_MINB)
raster(const envp *P, int e0, const tri *tris, const uint32_t *vals, const uint32_t *key_start,
         const uint32_t *key_end, const uint32_t *order, int W, int H, int tiles_x, int ntiles,
         const unsigned char *atlases, size_t atlas_bytes, texes X, unsigned char *out, float *depth_out)
{
    /* each warp's group: up to RASTER_GROUP of the triangles it visits,
     * their records (cp.async), their indices and their cover masks */
    __shared__ __align__(16) uint4 slots[RASTER_THREADS / 32][RASTER_GROUP][TRI_QUADS];
    __shared__ uint32_t gidx[RASTER_THREADS / 32][RASTER_GROUP], gcov[RASTER_THREADS / 32][RASTER_GROUP];
    uint32_t key = order[blockIdx.x / RASTER_SPLIT];
    int tile = (int)(key % (uint32_t)ntiles), ge = (int)(key / (uint32_t)ntiles); /* ge: the environment in the group */
    int e = e0 + ge;
    const envp &PE = P[e];
    /* each warp an 8 x 4 block of the tile's pixels (wx0, wy0 its corner) */
    int wid = blockIdx.x % RASTER_SPLIT * (RASTER_THREADS / 32) + threadIdx.x / 32, lane = threadIdx.x % 32;
    int wx0 = (tile % tiles_x) * TILE + (wid & 1) * 8, wy0 = (tile / tiles_x) * TILE + (wid >> 1) * 4;
    int px = wx0 + (lane & 7), py = wy0 + (lane >> 3);
    const int pr = PM == RASTER_PM_EXACT ? 0 : PM == RASTER_PM_FAST ? (int)RP_FAST : PE.prec;
    X.atlas = atlases + (size_t)PE.slot * atlas_bytes;
    X.lm_base = PE.lm_base;
    unsigned char rgb[3];
    float depth = 1.0f;
    for (int c = 0; c < 3; ++c) rgb[c] = byte_(PE.fog[c]);
    uint32_t s = key_start[key], n = key_end[key] - s;
    bool live = px < W && py < H, snapped = false;
    float *dout = depth_out + ((size_t)e * H + py) * W + px;
    uint32_t tn = lane < n ? vals[s + lane] : 0;
    for (uint32_t b = 0; b < n; b += RASTER_BATCH) {
        uint32_t m = n - b < RASTER_BATCH ? n - b : RASTER_BATCH;
        uint32_t t = tn;
        /* lane i: the batch's triangle i (t) and which of the warp's
         * pixels it may cover (cover_mask); the warp visits the triangles
         * that may cover one, in order, and each pixel function runs only
         * where they may. Each warp walks the list alone, reading the
         * batch through L1 (the block's other warps read the same
         * triangles): staged in shared memory for the block, the batch
         * waited for its slowest warp at two barriers, 62% of the stall
         * samples. The next batch's triangles load now and their lines
         * (the ones cover_mask reads) go to L1 while this batch is drawn. */
        tn = b + RASTER_BATCH + lane < n ? vals[s + b + RASTER_BATCH + lane] : 0;
        uint32_t cov = lane < m ? cover_mask(tris[t], wx0, wy0) : 0;
        /* a triangle the warp will visit: the rest of its record (the
         * pixel functions read most of it) */
        if (cov) asm volatile("prefetch.global.L1 [%0];" ::"l"((const char *)(tris + t) + 128));
        if (b + RASTER_BATCH + lane < n) {
            const char *q = (const char *)(tris + tn);
            asm volatile("prefetch.global.L1 [%0];" ::"l"(q));
            asm volatile("prefetch.global.L1 [%0];" ::"l"(q + offsetof(tri, y)));
        }
        /* the visits, a group at a time: the group's records copied into
         * the warp's slots, then each lane walks the triangles that may
         * cover its own pixel, in order (lane/clipclimb). A pixel's
         * triangles are drawn in the order they were before, and a pixel
         * function reads and writes its own pixel alone, so every byte is
         * the same; a step runs each lane's next triangle, where the warp
         * visiting one triangle at a time ran 6 of 32 lanes. */
        unsigned todo = __ballot_sync(0xffffffffu, cov != 0);
        int w = threadIdx.x / 32;
        while (todo) {
            unsigned g = 0, rest = todo;
            for (int k = 0; k < RASTER_GROUP && rest; ++k) { g |= rest & (0u - rest); rest &= rest - 1; }
            todo = rest;
            int cnt = __popc(g);
            if (g >> lane & 1) {
                int r = __popc(g & ((1u << lane) - 1));
                gidx[w][r] = t;
                gcov[w][r] = cov;
            }
            __syncwarp();
            for (int q = lane; q < cnt * TRI_QUADS; q += 32) {
                int j = q / TRI_QUADS, c = q - j * TRI_QUADS;
                uint32_t d = (uint32_t)__cvta_generic_to_shared(&slots[w][j][c]);
                asm volatile("cp.async.ca.shared.global [%0], [%1], 16;" ::"r"(d), "l"((const uint4 *)(tris + gidx[w][j]) + c) : "memory");
            }
            asm volatile("cp.async.commit_group;" ::: "memory");
            unsigned mine = 0;
            for (int j = 0; j < cnt; ++j) mine |= (gcov[w][j] >> lane & 1) << j;
            if (!live) mine = 0;
            asm volatile("cp.async.wait_group 0;" ::: "memory");
            __syncwarp();
            while (mine) {
                int k = __ffs(mine) - 1;
                mine &= mine - 1;
                const tri &T = *(const tri *)slots[w][k];
                switch (T.mode >> 8) {
                case TK_TERRAIN: pixel(T, PE, X, pr, px, py, rgb, depth); break;
                case TK_ENTITY: e_pixel(T, X, pr, px, py, rgb, depth); break;
                case TK_CRACK: crack_pixel(T, X, px, py, rgb, depth); break;
                case TK_PORTAL: portal_pixel(T, X, pr, px, py, rgb, depth); break;
                case TK_LINE: line_pixel(T, X, px, py, rgb, depth); break;
                case TK_SNAP: *dout = depth; snapped = true; break;
                default: depth = 1.0f; break;
                }
            }
            __syncwarp();   /* the slots are the next group's */
        }
    }
    if (live) {
        unsigned char *o = (unsigned char *)PE.out + ((size_t)py * W + px) * 3;
        o[0] = rgb[0]; o[1] = rgb[1]; o[2] = rgb[2];
        if (!snapped) *dout = depth;
    }
}

/* the (environment, tile) keys, most pairs first, for raster's blocks:
 * a block's time is about its tile's triangles, and one of a few thousand
 * in a tile took the whole launch when the grid reached it last. Every
 * block writes its own tile alone, so the order changes no byte (the order
 * within a bin is the atomics'). Bins: a quarter octave each, 0 empty. */
enum { ORDER_BINS = 129 };
__device__ __forceinline__ uint32_t order_bin(uint32_t n)
{
    if (!n) return 0;
    uint32_t l = 31 - __clz(n), q = l >= 2 ? (n >> (l - 2)) & 3 : (n << (2 - l)) & 3;
    return 1 + l * 4 + q;
}

__global__ void __launch_bounds__(1024)
tile_order(const uint32_t *key_start, const uint32_t *key_end, uint32_t nkeys, uint32_t *order)
{
    __shared__ uint32_t at[ORDER_BINS];
    for (uint32_t i = threadIdx.x; i < ORDER_BINS; i += blockDim.x) at[i] = 0;
    __syncthreads();
    for (uint32_t k = threadIdx.x; k < nkeys; k += blockDim.x) atomicAdd(&at[order_bin(key_end[k] - key_start[k])], 1u);
    __syncthreads();
    if (threadIdx.x == 0) {
        uint32_t sum = 0;
        for (int b = ORDER_BINS - 1; b >= 0; --b) { uint32_t c = at[b]; at[b] = sum; sum += c; }
    }
    __syncthreads();
    for (uint32_t k = threadIdx.x; k < nkeys; k += blockDim.x) order[atomicAdd(&at[order_bin(key_end[k] - key_start[k])], 1u)] = k;
}

#define RASTER_INST(PM) \
    template __global__ void raster<PM>(const envp *P, int e0, const tri *tris, const uint32_t *vals, \
        const uint32_t *key_start, const uint32_t *key_end, const uint32_t *order, int W, int H, int tiles_x, int ntiles, \
        const unsigned char *atlases, size_t atlas_bytes, texes X, unsigned char *out, float *depth_out);
RASTER_INST(RASTER_PM_EXACT)
RASTER_INST(RASTER_PM_ANY)
RASTER_INST(RASTER_PM_FAST)

/* the changed atlas tiles into their environments' atlases */
__global__ void atlas_patch(const uint32_t *idx, const unsigned char *data, uint32_t n,
                              unsigned char *atlases, size_t atlas_bytes, int aw)
{
    uint32_t i = blockIdx.x;
    if (i >= n) return;
    uint32_t env = idx[i * 2], tile = idx[i * 2 + 1];
    int tiles_x = aw / 16;
    int x0 = (tile % tiles_x) * 16, y0 = (tile / tiles_x) * 16;
    for (int j = threadIdx.x; j < 16 * 16 * 4; j += blockDim.x) {
        int y = j / 64, x = j % 64;
        atlases[(size_t)env * atlas_bytes + ((size_t)(y0 + y) * aw) * 4 + x0 * 4 + x] = data[(size_t)i * 1024 + j];
    }
}

/* The translucent quads' order (raster.c's qsort by water_quad_compare:
 * the farthest first, equal distances by offset), one block a section: a
 * section of at most WATER_SORT quads sorts (distance, quad) keys in shared
 * memory (bitonic), a larger one ranks each quad against every other; both
 * give each quad the place the rank (b > a) || (b == a && k < q) names. */
enum { WATER_SORT = 4096 };
__global__ void __launch_bounds__(256)
water_order(const envp *P, const ddraw *D, const wjob *jobs, const int32_t *mesh, int32_t *orders, float *dist)
{
    const wjob j = jobs[blockIdx.x];
    const ddraw &d = D[j.draw];
    const envp &p = P[d.env];
    float x = (float)p.cam[0] - (float)(d.cx * 16), y = (float)p.cam[1] - (float)(d.s * 16),
          z = (float)p.cam[2] - (float)(d.cz * 16);
    uint32_t n = d.nquads;
    __shared__ unsigned long long sk[WATER_SORT];
    const bool small = n <= WATER_SORT;
    uint32_t m = 1;
    while (m < n) m <<= 1;
    float *ds = dist + j.dist;
    for (uint32_t q = threadIdx.x; q < (small ? m : n); q += blockDim.x) {
        if (q >= n) { sk[q] = ~0ull; continue; }
        float sum[3] = {0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            const int32_t *v = mesh + d.mesh + ((int64_t)q * 4 + i) * 8;
            sum[0] += __int_as_float(v[0]) - x;
            sum[1] += __int_as_float(v[1]) - y;
            sum[2] += __int_as_float(v[2]) - z;
        }
        float dx = sum[0] * 0.25f, dy = sum[1] * 0.25f, dz = sum[2] * 0.25f;
        float dd = dx * dx + dy * dy + dz * dz;
        /* a square's bits order as the unsigned key does: the farthest
         * first is their complement ascending, then the quad */
        if (small) sk[q] = (unsigned long long)~__float_as_uint(dd) << 32 | q;
        else ds[q] = dd;
    }
    __syncthreads();
    if (small) {
        for (uint32_t k = 2; k <= m; k <<= 1)
            for (uint32_t h = k >> 1; h > 0; h >>= 1) {
                for (uint32_t i = threadIdx.x; i < m; i += blockDim.x) {
                    uint32_t l = i ^ h;
                    if (l > i) {
                        unsigned long long a = sk[i], b = sk[l];
                        if ((a > b) == ((i & k) == 0)) { sk[i] = b; sk[l] = a; }
                    }
                }
                __syncthreads();
            }
        for (uint32_t i = threadIdx.x; i < n; i += blockDim.x) orders[d.order + i] = (int32_t)(uint32_t)sk[i] * 4;
        return;
    }
    for (uint32_t q = threadIdx.x; q < n; q += blockDim.x) {
        float a = ds[q];
        uint32_t rank = 0;
        for (uint32_t k = 0; k < n; ++k) {
            float b = ds[k];
            rank += (b > a) || (b == a && k < q);
        }
        orders[d.order + rank] = (int32_t)q * 4;
    }
}
