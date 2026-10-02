/* The device renderer's shared declarations (lane/cudasplit): the types
 * the host (host.cu) and the device code (geom.cuh's units,
 * raster.cu, sort.cu) all use and every kernel, so they
 * compile as separate units (-rdc) and an edit to one rebuilds only it. */
#ifndef NETHERITE_RENDER_DEV_CUH
#define NETHERITE_RENDER_DEV_CUH

#include "render.h"

extern "C" {
#include "../../engine/raster_obs.h"
#include "../../engine/raster_prec.h"
#include "../../engine/meshfeed.h"
#include "../meshing/meshing.h"
}

#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_fp16.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>

/* a failed CUDA call ends the process, or, once render_fail_throws(1)
 * (cuda/render/framedev.cu), throws a render_error that unwinds to the
 * plugin's call (and play draws in C) */
struct render_error {};
[[noreturn]] void render_fail(void);
void render_fail_throws(int on);
#define CK_FAIL render_fail()
#define CK(x) do { cudaError_t e_ = (x); if (e_ != cudaSuccess) { \
    fprintf(stderr, "render: %s: %s (%s:%d)\n", #x, cudaGetErrorString(e_), __FILE__, __LINE__); CK_FAIL; } } while (0)

enum { TILE = 16, TILE_PX = TILE * TILE };

/* The sky's units, in the order raster_live_frame records them: every slot
 * a quad (two triangles) or one triangle of the fan; a slot the frame does
 * not draw makes nothing. */
enum {
    U_PLANE1 = 0,                       /* draw_sky(1): 13 x 13 quads */
    U_RISE = U_PLANE1 + 169,            /* draw_sunrise: 16 triangles */
    U_SUN = U_RISE + 16,
    U_MOON = U_SUN + 1,
    U_STARS = U_MOON + 1,               /* raster_sky2_stars: one quad each */
    MAX_STARS = 1500,
    U_PLANE2 = U_STARS + MAX_STARS,     /* draw_sky(2) */
    U_CLOUDS = U_PLANE2 + 169,          /* draw_clouds: 2 passes x 64 cells x 34 */
    SKY_UNITS = U_CLOUDS + 2 * 64 * 34,
};

struct envp {
    float proj[16], mv[16];
    double cam[3];
    int prec;                           /* raster_prec.h: the stages drawn fast (0: exact); with
                                         * the matrices and the camera, what quads copies */
    float fog[3], fogs, foge, fogd;
    int fogm;
    uint32_t lm[256];
    struct raster_obs_sky sky;
    uint32_t unit_base, nunits;         /* the environment's units in the render */
    uint32_t draw_base, ndraws;
    uint32_t nstars;
    /* its recorded passes' tables in the render's arrays */
    uint32_t estate_base, tex_base, lm_base, crack_base, equad_base, portal_base, line_base;
    /* its resident slot (its atlas) and where its frame goes */
    uint32_t slot;
    unsigned long long out;
};

/* a unit the generic kernel runs: a sky slot, a section quad the lean
 * kernel handed over, a recorded entity quad, a crack triangle, a depth
 * clear */
enum { XU_SKY, XU_QUAD, XU_EQUAD, XU_CRACK, XU_CLEAR, XU_PORTAL, XU_LINE, XU_SNAP };
struct xunit { uint32_t g, kind, a, b; };

/* a texture the recorded quads sample: its bytes in the pool */
struct dtex { unsigned long long off; int w, h; };

struct ddraw {
    int cx, s, cz, flags;
    uint32_t quad_base, nquads;         /* units SKY_UNITS + quad_base.. */
    int water;
    int env;
    uint32_t unit0;                     /* the global unit of its first quad */
    int64_t mesh;                       /* int32 index of the quads in the mesh arena */
    int64_t order;                      /* int32 index of the order, or -1 */
};

/* clip_triangle's vertex */
struct vtx {
    float x, y, sx, sy, w, depth, u, v, c[4], l[2], fog;
    float clip[4];
};

/* One recorded triangle, after its rasterizer's own setup (the two-sided
 * back faces swapped): what the pixel loop reads. mode's low byte is
 * raster.c's (sky_mode | cloud_prepass << 4 | render_pass << 5); bits 8 on
 * name the rasterizer (TK_*). An entity triangle keeps its diffuse and
 * glColor alpha in dif and al, its state and texture in aux and tex; a
 * crack keeps its z in d, its eye distance in fog and its crack in aux. */
enum { TK_TERRAIN = 0, TK_ENTITY = 1, TK_CRACK = 2, TK_CLEAR = 3, TK_PORTAL = 4, TK_LINE = 5, TK_SNAP = 6 };
/* The fields the binning (pairs) and cover_mask read lead the record
 * (lane/coalesce): the box, the mode and the screen positions in its
 * first 48 bytes, a crack's x and y in the next 16, so a thread's loads
 * of them meet two sectors where they met three */
struct __align__(16) tri {
    short x0, x1, y0, y1;
    uint32_t mode;
    float area;
    float sx[3], sy[3], x[3], y[3];
    float w[3], d[3], u[3], v[3], c[3][4], l[3][2], fog[3];
    float dif[3], al[3];
    uint32_t aux, tex;
    uint32_t pad[3];                    /* to 16 bytes: written and loaded as uint4s */
};
static_assert(sizeof(tri) % 16 == 0, "tri is moved as uint4s");

/* raster_prec.h's fast mode (a frame's every stage: prec RP_FAST) keeps a
 * terrain (not a sky) or entity triangle's per-vertex colour and light
 * coordinates as the halves its stages round them to (the same bits):
 * vertex i's colour as the __half2s (r g), (b a) at c's start (ch[2 i],
 * ch[2 i + 1]), its light as the __half2 (block, sky) at l's (lh[i]) */
static __device__ __forceinline__ __half2 *tri_ch(tri &T) { return (__half2 *)&T.c[0][0]; }
static __device__ __forceinline__ __half2 *tri_lh(tri &T) { return (__half2 *)&T.l[0][0]; }
static __device__ __forceinline__ const __half2 *tri_ch(const tri &T) { return (const __half2 *)&T.c[0][0]; }
static __device__ __forceinline__ const __half2 *tri_lh(const tri &T) { return (const __half2 *)&T.l[0][0]; }
enum { TRI_WORDS = sizeof(tri) / 4, TRI_QUADS = sizeof(tri) / 16 };

/* A recorded entity quad as the device gets it: of each vertex only what
 * the device reads before e_screen writes the rest (the clip coordinates,
 * the fog distance, the texture coordinates, the light and the diffuse
 * shade); a quad whose state reads the vertex colour or alpha
 * (vertex_color, vertex_alpha, blend 6) keeps those in its extra record,
 * the others leave them zero, unread. 172 bytes against raster_obs_equad's
 * 360: the largest part of a render's upload. */
struct cvtx { float clip[4], fogcoord, u, v, light[2], diffuse; };
struct cquad { int state, tex, extra; cvtx v[4]; };
struct cextra { float color[4][4], alpha[4]; };

/* the most blocks a launch over a list the device counts takes (its
 * threads walk the list) */
enum { GEN_GRID = 1024 };

/* the types the host builds for the kernels */

/* the recorded passes' arrays, for the generic kernel */
struct recs {
    const cquad *equad;
    const cextra *eextra;
    const raster_obs_estate *estate;
    const raster_obs_crack *crack;
    const raster_obs_portal *portal;
    const raster_obs_line *line;
};

struct texes {
    const unsigned char *atlas;         /* this environment's */
    int aw, ah;
    const unsigned char *sun, *moon, *clouds, *end_sky;
    /* the recorded passes' */
    const unsigned char *pool;
    const dtex *tex;
    const raster_obs_estate *estate;
    const uint32_t (*lm)[256];
    const raster_obs_crack *crack;
    const raster_obs_portal *portal;
    const raster_obs_line *line;
    uint32_t lm_base;                   /* this environment's */
};

/* raster.c live_quad_distance and water_quad_compare over a translucent
 * section the device meshed (raster_live_device_mesh): each quad's
 * distance from the camera, then its place in the order qsort gives (far
 * first, equal distances by offset), one block a section */
struct wjob { uint32_t draw, dist; };

/* The one-trip tail's sizes (render.one_trip): the triangles and pairs
 * the render made are known only on the device, so every step past the
 * count reads them there, and the host learns them with the frames. W[0]
 * the triangles, W[1] the pairs, W[2] 1 when either passed the tail's
 * bounds (the frames are then void: the render runs again the usual way),
 * W[3] and W[4] the most triangles and pairs one frame made (the next
 * render's bounds), W[5] the clipped units. */
enum { TRIP_TRIS, TRIP_PAIRS, TRIP_OVER, TRIP_MAXT, TRIP_MAXP, TRIP_NCLIP, TRIP_WORDS };

enum { RASTER_BATCH = 32 };
/* raster's block: RASTER_THREADS of a tile's TILE_PX pixels (its warps
 * walk the tile alone), RASTER_SPLIT blocks a tile, at least RASTER_MINB
 * blocks an SM: on sm_120 the 128-thread launch is faster than 256,
 * while 64 threads gave no further gain (lane/renderclimb). 6 blocks hold
 * it at 80 registers (lane/clipclimb: the lanes' own walks took 97 and 4
 * blocks an SM on the 3090 at 3). RASTER_GROUP: the most triangles a
 * warp stages in shared memory at once, each lane then walking its own
 * pixel's (8 measured best of 6 to 16). */
enum { RASTER_THREADS = 128, RASTER_SPLIT = TILE_PX / RASTER_THREADS, RASTER_MINB = 6, RASTER_GROUP = 8 };
/* raster tests the pixels of a triangle's box on a warp's block when there
 * are at most this many (cover_mask) */
enum { COVER_TEST_MAX = 12 };
/* the binning tests the pixels of a triangle's box on a tile when there are
 * at most this many (tile_meets) */
enum { PAIR_TEST_MAX = 16 };

/* ------------------------------------------------------------ exact helpers */

/* x86's cvttss2si: a float outside int's range (or NaN) is INT_MIN */
static __device__ __forceinline__ int f2i(float f)
{
    if (!(f >= -2147483648.0f && f < 2147483648.0f)) return INT_MIN;
    return (int)f;
}

static __device__ __forceinline__ float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

/* byte(): lrintf, round to nearest even; NaN is INT_MIN, whose low byte is 0 */
static __device__ __forceinline__ unsigned char byte_(float x)
{
    x = clamp01(x);
    float y = x * 255.0f;
    if (y != y) return 0;
    return (unsigned char)__float2int_rn(y);
}

/* glibc 2.43's expf on x86-64 with FMA (the ifunc the dev host takes): its table,
 * constants and the five fused steps of __expf_fma, checked against the
 * host's expf on every negative float (the fog's only arguments). */
static __constant__ unsigned long long EXP2F_T[32] = {
    0x3ff0000000000000ULL, 0x3fefd9b0d3158574ULL, 0x3fefb5586cf9890fULL, 0x3fef9301d0125b51ULL,
    0x3fef72b83c7d517bULL, 0x3fef54873168b9aaULL, 0x3fef387a6e756238ULL, 0x3fef1e9df51fdee1ULL,
    0x3fef06fe0a31b715ULL, 0x3feef1a7373aa9cbULL, 0x3feedea64c123422ULL, 0x3feece086061892dULL,
    0x3feebfdad5362a27ULL, 0x3feeb42b569d4f82ULL, 0x3feeab07dd485429ULL, 0x3feea47eb03a5585ULL,
    0x3feea09e667f3bcdULL, 0x3fee9f75e8ec5f74ULL, 0x3feea11473eb0187ULL, 0x3feea589994cce13ULL,
    0x3feeace5422aa0dbULL, 0x3feeb737b0cdc5e5ULL, 0x3feec49182a3f090ULL, 0x3feed503b23e255dULL,
    0x3feee89f995ad3adULL, 0x3feeff76f2fb5e47ULL, 0x3fef199bdd85529cULL, 0x3fef3720dcef9069ULL,
    0x3fef5818dcfba487ULL, 0x3fef7c97337b9b5fULL, 0x3fefa4afa2a490daULL, 0x3fefd0765b6e4540ULL};

static __device__ float glibc_expf(float x)
{
    uint32_t ux = __float_as_uint(x);
    uint32_t abstop = (ux >> 20) & 0x7ff;
    if (abstop >= 0x42b) {
        if (ux == 0xff800000u) return 0.0f;
        if (abstop >= 0x7f8) return x + x;
        if (x > 0x1.62e42ep6f) return __uint_as_float(0x7f800000u);
        if (x < -0x1.9fe368p6f) return 0.0f;
        if (x < -0x1.9d1d9ep6f) return 0x1p-149f;
    }
    double xd = (double)x;
    const double invln2n = 0x1.71547652b82fep+0 * 32, shift = 0x1.8p+52;
    double kd = __fma_rn(invln2n, xd, shift);
    unsigned long long ki = (unsigned long long)__double_as_longlong(kd);
    kd -= shift;
    double r = __fma_rn(invln2n, xd, -kd);
    unsigned long long t = EXP2F_T[ki % 32] + (ki << 47);
    double s = __longlong_as_double((long long)t);
    double z = __fma_rn(0x1.c6af84b912394p-5 / 32 / 32 / 32, r, 0x1.ebfce50fac4f3p-3 / 32 / 32);
    double r2 = r * r;
    double y = __fma_rn(0x1.62e42ff0c52d6p-1 / 32, r, 1.0);
    y = __fma_rn(z, r2, y);
    y = y * s;
    return (float)y;
}


/* raster_prec.h's float16: the device's half operations (round to nearest
 * even, never contracted: the _rn forms), each the C side's float
 * operation rounded to half (rp_h); a division in float, then rounded */
static __device__ __forceinline__ float hr(float x) { return __half2float(__float2half_rn(x)); }
/* on values that are halves already (the C side's rp_h(a * b) and so on) */
static __device__ __forceinline__ float HM(float a, float b)
{
    return __half2float(__hmul_rn(__float2half_rn(a), __float2half_rn(b)));
}
static __device__ __forceinline__ float HA(float a, float b)
{
    return __half2float(__hadd_rn(__float2half_rn(a), __float2half_rn(b)));
}
static __device__ __forceinline__ float HS(float a, float b)
{
    return __half2float(__hsub_rn(__float2half_rn(a), __float2half_rn(b)));
}

static __device__ __forceinline__ float edge_xy(float ax, float ay, float bx, float by, float x, float y)
{
    return (x - ax) * (by - ay) - (y - ay) * (bx - ax);
}

/* ------------------------------------------------------------ the kernels (geom_quads.cu, geom_equads.cu,
 * geom_gen.cu, raster.cu) */

template <bool WRITE>
__global__ void __launch_bounds__(128)
quads(const envp *P, const ddraw *draws, uint32_t draw0, const int32_t *mesh, const int32_t *orders, int W, int H,
        uint32_t *counts, xunit *clip_list, uint32_t *nclip,
        const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);

template <bool WRITE>
__global__ void __launch_bounds__(128)
equads(const envp *P, int n, const xunit *list, uint32_t nlist, uint32_t u0, uint32_t u1, recs R, int W, int H,
         uint32_t *counts, xunit *clip_list, uint32_t *nclip, const uint32_t *offsets, uint32_t tri_base,
         tri *out, uint32_t *tiles);

template <bool WRITE>
__global__ void __launch_bounds__(128)
generic(const envp *P, int n, const xunit *list, uint32_t nlist, const uint32_t *dn, uint32_t u0, uint32_t u1,
          const ddraw *draws, const int32_t *mesh, const int32_t *orders, const float *stars, recs R, int W, int H,
          uint32_t *counts, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);

__global__ void __launch_bounds__(256)
pairs(const tri *tris, uint32_t ntris, const uint32_t *tri_env, const uint32_t *pair_off,
                        int tiles_x, int ntiles, uint32_t sentinel, uint32_t *keys, uint32_t *vals);

__global__ void gather(const uint32_t *src, const uint32_t *idx, uint32_t n, uint32_t *dst);

__global__ void tri_envs(const uint32_t *env_tri0, int nenv, uint32_t ntris, uint32_t *tri_env);

__global__ void key_runs(const uint32_t *keys, uint32_t npairs, const uint32_t *w, uint32_t *key_start, uint32_t *key_end);

__global__ void trip_tris(const uint32_t *offsets, const uint32_t *unit_base, int n, uint32_t tris_cap,
                            uint32_t *env_tri0, uint32_t *w);

__global__ void trip_squash(uint32_t *offsets, uint32_t units, const uint32_t *w);

__global__ void trip_tri_envs(const uint32_t *env_tri0, int nenv, const uint32_t *w, uint32_t *tri_env);

__global__ void trip_pairs_n(const uint32_t *pair_off, uint32_t pairs_cap, uint32_t *w);

__global__ void trip_env_max(const uint32_t *env_tri0, const uint32_t *pair_off, int n, const uint32_t *nclip,
                             uint32_t *w);

__global__ void __launch_bounds__(256)
trip_pairs(const tri *tris, const uint32_t *w, const uint32_t *tri_env, const uint32_t *pair_off,
                             int tiles_x, int ntiles, uint32_t bound, uint32_t sentinel, uint32_t *keys, uint32_t *vals);

__global__ void trip_zero_tail(uint32_t *tiles, uint32_t n, const uint32_t *w);

enum { RASTER_PM_EXACT, RASTER_PM_ANY, RASTER_PM_FAST };
template <int PM>
__global__ void __launch_bounds__(RASTER_THREADS, RASTER_MINB)
raster(const envp *P, int e0, const tri *tris, const uint32_t *vals, const uint32_t *key_start,
         const uint32_t *key_end, const uint32_t *order, int W, int H, int tiles_x, int ntiles,
         const unsigned char *atlases, size_t atlas_bytes, texes X, unsigned char *out, float *depth_out);

__global__ void __launch_bounds__(1024)
tile_order(const uint32_t *key_start, const uint32_t *key_end, uint32_t nkeys, uint32_t *order);

__global__ void atlas_patch(const uint32_t *idx, const unsigned char *data, uint32_t n,
                              unsigned char *atlases, size_t atlas_bytes, int aw);

__global__ void __launch_bounds__(256)
water_order(const envp *P, const ddraw *D, const wjob *jobs, const int32_t *mesh, int32_t *orders, float *dist);

/* sort.cu: cub's device-wide scan and sort, the one unit that
 * includes cub (its headers cost every unit that has them seconds) */
cudaError_t rs_exclusive_sum(void *temp, size_t &bytes, uint32_t *in, uint32_t *out, int n, cudaStream_t st);
cudaError_t rs_sort_pairs(void *temp, size_t &bytes, uint32_t *keys_in, uint32_t *keys_out, uint32_t *vals_in,
                          uint32_t *vals_out, int n, int bit0, int bit1, cudaStream_t st);

#endif
