/* The ray cast's kernels (rcdev.h): runtime/raycast.c's walk and its
 * helpers ported (the fast path's arithmetic, operation for operation:
 * --fmad=false and IEEE division and square root, as every part here), each
 * cell, light and biome read from the env's ring (rchead.h) instead of the
 * client world's chunks. The mirror rule (cuda/mirror.tsv) holds it to
 * raycast.c: pool_gate --raycast-device, every frame and seen record equal.
 *
 * Two instances (lane/castf32, runtime/raycast.h "The two instances"): rc_cast and walk are templates on the scalar,
 * launched by the head's fp. FP64 (double): lane/casthot's kernel, its air loop deciding on float shadows of its
 * doubles; FP32 (float): raycast.c's FP32 instance, every operation a float's in raycast.h's order, the air loop's
 * decisions on the float t's themselves (no shadows, no bounds, nothing in double). The head carries FP32's floats
 * widened (exactly); a block converts them once (E, struct rce). */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../../runtime/rchead.h"
#include "kern.h"
#include <cuda_fp16.h>

/* The FP16 instance's scalar (lane/castexact, raycast.h "The instances"): an IEEE half, each operation the float
 * operation rounded to half (exact for + - * / and sqrt: 24 >= 2 x 11 + 2, so it is half arithmetic's own result and
 * raycast_f16.c's _Float16 with gcc's excess precision 16), conversions from double and float rounded once, to float,
 * int (truncated) and double exactly. */
struct h16 {
    __half v;
    h16() = default;
    __device__ h16(double d) : v(__double2half(d)) {}
    __device__ h16(float f) : v(__float2half_rn(f)) {}
    __device__ h16(int i) : v(__int2half_rn(i)) {}
    __device__ explicit operator float() const { return __half2float(v); }
    __device__ explicit operator double() const { return (double)__half2float(v); }
    __device__ explicit operator int() const { return (int)__half2float(v); }
};
static __device__ __forceinline__ float hf(h16 a) { return __half2float(a.v); }
/* RC_H16 (lane/cxf16): 1, + - * and the compares by the half instructions themselves (HADD, HMUL, HSETP: IEEE
 * half's correctly rounded result, the float operation rounded to half by the bound above, so the same bits; the _rn
 * forms are never contracted to an HFMA, as --fmad=false); 0, each in float and rounded (lane/castexact's). The
 * division and the square root stay float's rounded once (__hdiv and hsqrt are not promised correctly rounded) */
#ifndef RC_H16
#define RC_H16 1
#endif
static __device__ __forceinline__ h16 hh(__half v)
{
    h16 r;
    r.v = v;
    return r;
}
#if RC_H16
static __device__ __forceinline__ h16 operator+(h16 a, h16 b) { return hh(__hadd_rn(a.v, b.v)); }
static __device__ __forceinline__ h16 operator-(h16 a, h16 b) { return hh(__hsub_rn(a.v, b.v)); }
static __device__ __forceinline__ h16 operator*(h16 a, h16 b) { return hh(__hmul_rn(a.v, b.v)); }
static __device__ __forceinline__ h16 operator-(h16 a) { return hh(__hneg(a.v)); }
static __device__ __forceinline__ bool operator<(h16 a, h16 b) { return __hlt(a.v, b.v); }
static __device__ __forceinline__ bool operator<=(h16 a, h16 b) { return __hle(a.v, b.v); }
static __device__ __forceinline__ bool operator>(h16 a, h16 b) { return __hgt(a.v, b.v); }
static __device__ __forceinline__ bool operator>=(h16 a, h16 b) { return __hge(a.v, b.v); }
static __device__ __forceinline__ bool operator==(h16 a, h16 b) { return __heq(a.v, b.v); }
static __device__ __forceinline__ bool operator!=(h16 a, h16 b) { return __hneu(a.v, b.v); }
#else
static __device__ __forceinline__ h16 operator+(h16 a, h16 b) { return h16(hf(a) + hf(b)); }
static __device__ __forceinline__ h16 operator-(h16 a, h16 b) { return h16(hf(a) - hf(b)); }
static __device__ __forceinline__ h16 operator*(h16 a, h16 b) { return h16(hf(a) * hf(b)); }
static __device__ __forceinline__ h16 operator-(h16 a) { return h16(-hf(a)); }
static __device__ __forceinline__ bool operator<(h16 a, h16 b) { return hf(a) < hf(b); }
static __device__ __forceinline__ bool operator<=(h16 a, h16 b) { return hf(a) <= hf(b); }
static __device__ __forceinline__ bool operator>(h16 a, h16 b) { return hf(a) > hf(b); }
static __device__ __forceinline__ bool operator>=(h16 a, h16 b) { return hf(a) >= hf(b); }
static __device__ __forceinline__ bool operator==(h16 a, h16 b) { return hf(a) == hf(b); }
static __device__ __forceinline__ bool operator!=(h16 a, h16 b) { return hf(a) != hf(b); }
#endif
static __device__ __forceinline__ h16 operator/(h16 a, h16 b) { return h16(__fdiv_rn(hf(a), hf(b))); }

#define RC_TAN_HALF 0.7002075382097097

/* the kernel's compile-time knobs (lane/casthot's grid: every combination built with -DRC_X=v and ranked on the
 * dumps in one window, out/perf/casthot.tsv); the defaults are the ranked best */
#ifndef RC_BS
#define RC_BS 128       /* threads a block (one env's pixels a block) */
#endif
#ifndef RC_MINB
#define RC_MINB 4       /* __launch_bounds__' blocks an SM: the register cap 65536 / (RC_BS RC_MINB) */
#endif
#ifndef RC_MINB32
#define RC_MINB32 6     /* the FP32 instance's (lane/castf32: 78 registers, under 6's cap of 85; 4, 5, 6 ranked in one
                         * window: 6 0.3 to 0.9% ahead on the three dumps) */
#endif
#ifndef RC_MINB16
#define RC_MINB16 6     /* the FP16 instance's (lane/cxf16: 4, 6, 8 ranked in one window on the stored casts at 16x16
                         * to 128x128: 6 1.3 to 6.3% ahead of 4, 8 behind both) */
#endif
#ifndef RC_PF
#define RC_PF 0         /* 1: the air loop decides the next step and loads its cell's id ahead of this cell's tests */
#endif
#ifndef RC_HX
#define RC_HX 0         /* 1: a cube hit makes only the two coordinates its face's texel reads */
#endif
#ifndef RC_TILE
#define RC_TILE 1       /* 1: a block a 16 x 8 tile of pixels, its warps 8 x 4 (else 128 pixels of rows; a warp's
                         * rays closer together walk more of the same cells and steps) */
#endif
#ifndef RC_KSH
#define RC_KSH 0        /* 1: the kind table (4 KB) in each block's shared memory, else read-only global loads */
#endif
#ifndef RC_BRICKJ
#define RC_BRICKJ 0     /* 1 (lane/castexact): the float instances' air loop jumps a 4 x 4 x 4 brick of air (dry eye) */
#endif

/* the window's setup reads a ring column's first 16 bytes as one int4 */
static_assert(offsetof(struct rcd_col, cx) == 0 && offsetof(struct rcd_col, cz) == 4 && offsetof(struct rcd_col, exists) == 8 &&
                  offsetof(struct rcd_col, mask) == 12 && offsetof(struct rcd_col, present) == 14 && sizeof(struct rcd_col) % 16 == 0,
              "struct rcd_col's head");

/* the tables' pointers in constant memory (lane/casthot: as a __device__ struct each table read began with a global
 * load of its pointer, a step of every hit's load chain); the tables themselves read through the read-only path */
__constant__ struct rcd_tabdev T;

/* raycast.c's BI: the bound a face lies on (DOWN y0, UP y1, NORTH z0, SOUTH z1, WEST x0, EAST x1), by selects */
static __device__ __forceinline__ int RC_BI_OF(int f) { return f == 0 ? 1 : f == 1 ? 4 : f == 2 ? 2 : f == 3 ? 5 : f == 4 ? 0 : 3; }

/* the block's env (every thread of a block casts for one env) */
struct rcd_ctx {
    const struct rcd_col *cols;     /* the env's ring */
    const struct rcd_sec *secs;
    int R, w;
    int ch;                         /* the frame's bytes a pixel (client.channels: 3, or 1 the luma) */
    unsigned long long *seen_key;   /* [RCD_SEEN] */
    int *seen_n;                    /* [RCD_SEEN] */
    int *seen_xyz;                  /* [w * h][3] */
    /* the eye's per-cast constants the rays share (lane/rcsplit: made once a block, not in double on every thread):
     * o in float, |o in float| (infinite past 4e6: the shadows' bound term, every decision in double then), and
     * floor(o) with o - floor(o) in float (the float jump's coordinate) */
    float ofl[3], oab[3], ofr[3];
    int oi[3];
#if NETH_RC_HEAD_U8
    const uint32_t *lm;             /* packed ARGB8 lightmap (global memory) */
#else
    const float (*lm)[3];           /* the head's lightmap and entity boxes, read where they are (global memory) */
#endif
    const struct rcd_ent *ent;
};
__shared__ struct rcd_ctx X;
/* the head as a block keeps it in shared memory (lane/casthot): struct rcd_head without its lightmap (3 KB) and entity
 * boxes (5 KB), so four blocks an SM take 16 KB of the 128 KB the SM shares between shared memory and the L1 that
 * caches the sections' cells (a whole head each took the 64 KB carveout, 61% of the cells' loads hitting) */
struct rch {
    double ox, oy, oz;
    double fx, fy, fz, rx, ry, rz, ux, uy, uz, aspect;
    double rnext;
    int32_t eye_x, eye_y, eye_z;
    int32_t liquid, range, no_sky, dim, fancy, top;
    int32_t ok;
    int32_t seen_on;
    int16_t seen_ids[RCD_SEEN];
    int32_t cx0, cz0, ncx, ncz;
    int32_t fp, fp_pad_;
    /* rcd_head's lm here */
    float sky[3], fog[3];
    int32_t exp_fog, uw;
    float uw_rgb[3];
    float fs, fe, fd, sky_end, fspan;
    int32_t nent;
};
#define RCH_A offsetof(struct rcd_head, lm)        /* rcd_head's bytes before the lightmap */
#define RCH_B offsetof(struct rcd_head, sky)       /* and from sky to the boxes */
#define RCH_E offsetof(struct rcd_head, ent)
static_assert(offsetof(struct rch, sky) == RCH_A && sizeof(struct rch) == RCH_A + (RCH_E - RCH_B) && RCH_A % 4 == 0 &&
                  RCH_B % 4 == 0 && RCH_E % 4 == 0 && offsetof(struct rch, nent) == RCH_A + offsetof(struct rcd_head, nent) - RCH_B,
              "struct rch is struct rcd_head without lm and ent");
__shared__ struct rch H;
/* the cast's window as the block reads it: each column's ring index (-1: no chunk, raycast.c's col[c] NULL) and
 * which of its sections have storage (made once a block: the ring's column found without a division a section) */
__shared__ int16_t WCOL[RCD_RMAX * RCD_RMAX];
__shared__ uint16_t WPRES[RCD_RMAX * RCD_RMAX];
__shared__ uint16_t WMASK[RCD_RMAX * RCD_RMAX];  /* the column's mask (struct chunk's), 0 for no chunk */
#define RC_DT 128
__shared__ double DA[RC_DT][3], DB[RC_DT][3];     /* f + sx r a column, sy u a block's row (frames up to RC_DT wide) */
__shared__ float DAf[RC_DT][3], DBf[RC_DT][3];    /* the FP32 instance's */
__shared__ h16 DAh[RC_DT][3], DBh[RC_DT][3];      /* the FP16 instance's */
#if RC_KSH
__shared__ __align__(16) uint8_t KSH[4096];        /* T.kind (RC_KSH) */
#endif
#ifdef RC_STATS
/* the stats build's counters a pixel (bench.cu --stats): air steps, section jumps, the general path's turns, the
 * outer loop's turns */
__device__ unsigned *rc_stats;
#define RC_STAT(c) (++st[c])
#define RC_STATS_PARAM , unsigned *st
#define RC_STATS_ARG , st
#else
#define RC_STAT(c) ((void)0)
#define RC_STATS_PARAM
#define RC_STATS_ARG
#endif

/* A section's cells in the device's ring are in Morton order (lane/casthot): the bits of x, z and y interleaved, x's
 * lowest, so a 32-byte sector holds a 4 x 4 x 2 brick (x, z, y) of cells where world.h's x << 8 | z << 4 | y put one
 * in a single x column (2 of z, 16 of y): a warp's rays fan out sideways and its loads spread over 3.6 sectors each.
 * rc_scatter puts each staged section (world.h's order) in this order, the nibble arrays by the same index, and the
 * dump (rcdev_dump) and rcbench go back and forth by rc_sec_order. A step along an axis moves its bits alone
 * (rc_mstep: the other axes' bits held, the axis' own incremented or decremented through them). */
#define RC_MX 0x249
#define RC_MZ 0x492
#define RC_MY 0x924
static __host__ __device__ __forceinline__ int rc_spread4(int v)
{
    v = (v | v << 4) & 0x0C3;
    return (v | v << 2) & 0x249;
}
static __host__ __device__ __forceinline__ int rc_compact4(int v)
{
    v &= 0x249;
    v = (v | v >> 2) & 0x0C3;
    return (v | v >> 4) & 0x00F;
}
#define SEC_XYZ(x, y, z) (rc_spread4((x) & 15) | rc_spread4((z) & 15) << 1 | rc_spread4((y) & 15) << 2)
/* the Morton index m's cell in world.h's order */
static __host__ __device__ __forceinline__ int rc_canon(int m) { return rc_compact4(m) << 8 | rc_compact4(m >> 1) << 4 | rc_compact4(m >> 2); }
/* one step of s (+1 or -1) along the axis with mask M (its coordinate stays inside the section: a step out of it
 * is followed by SEC_XYZ again) */
static __device__ __forceinline__ int rc_mstep(int ci, int M, int s)
{
    return ((((ci & M) | (s > 0 ? ~M & 0xfff : 0)) + s) & M) | (ci & ~M);
}
#define chunk_sec_metas(s) ((s)->metas)
#define chunk_sec_id(s, i) ((s)->ids[i])

static __device__ __forceinline__ int nibble_get(const uint8_t *a, int i) { return (__ldg(&a[i >> 1]) >> ((i & 1) << 2)) & 15; }

static __device__ __forceinline__ int ring_index(int R, int cx, int cz)
{
    int a = cx % R, b = cz % R;
    return (a < 0 ? a + R : a) * R + (b < 0 ? b + R : b);
}

/* raycast.c's col_of: the cell's column in the cast's window, -1 outside */
static __device__ __forceinline__ int col_of(const struct rch *k, int x, int z)
{
    unsigned dx = (unsigned)((x >> 4) - k->cx0), dz = (unsigned)((z >> 4) - k->cz0);
    return dx < (unsigned)k->ncx && dz < (unsigned)k->ncz ? (int)(dx * (unsigned)k->ncz + dz) : -1;
}

/* the window's column c as raycast.c's k->col[c]: NULL for no chunk */
static __device__ __forceinline__ const struct rcd_col *colw(const struct rch *k, int c)
{
    int ri = WCOL[c];
    return ri >= 0 ? X.cols + ri : NULL;
}

/* raycast.c's k->sec[si] (si = c * 16 + section): NULL for no storage */
static __device__ __forceinline__ const struct rcd_sec *secw(const struct rch *k, int si)
{
    if (si < 0) return NULL;
    int c = si >> 4, ri = WCOL[c];
    return ri >= 0 && (WPRES[c] >> (si & 15) & 1) ? X.secs + (size_t)ri * 16 + (si & 15) : NULL;
}

static __device__ __forceinline__ const struct rcd_sec *sec_fast(const struct rch *k, int x, int y, int z)
{
    int c = col_of(k, x, z);
    return c >= 0 ? secw(k, c * 16 + (y >> 4)) : NULL;
}

static __device__ __forceinline__ int cell_fast(const struct rch *k, int x, int y, int z, int *meta)
{
    const struct rcd_sec *s = sec_fast(k, x, y, z);
    if (s == NULL) return *meta = 0, 0;
    int i = SEC_XYZ(x & 15, y, z & 15);
    int id = s->ids[i];
    *meta = id ? nibble_get(s->metas, i) : 0;
    return id;
}

/* raycast.c's light_fast (its chunk_light over the window's column and section); a cell outside the window (no ray in
 * range reaches one) reads as a missing chunk. The column's mask comes from the block's window (WMASK, lane/casthot:
 * it was a global load of the ring's column ahead of the section's, two steps of a hit's load chain), and its height
 * map is read only where a cell has no section */
static __device__ __forceinline__ void light_fast(const struct rch *k, int x, int y, int z, int *sky, int *block)
{
    if (y < 0) { *sky = 0, *block = 0; return; }
    if (y > 255) y = 255;
    const int c = col_of(k, x, z), ri = c >= 0 ? WCOL[c] : -1;
    if (ri < 0) { *sky = k->no_sky ? 0 : 15, *block = 0; return; }
    const int sb = y >> 4, has = WMASK[c] >> sb & 1;
    if (has && (WPRES[c] >> sb & 1))
    {
        const struct rcd_sec *s = X.secs + (size_t)ri * 16 + sb;
        const int i = SEC_XYZ(x & 15, y, z & 15);
        *sky = k->no_sky ? 0 : nibble_get(s->sky, i);
        *block = nibble_get(s->block, i);
        return;
    }
    *block = 0;
    *sky = !k->no_sky && !has && y >= __ldg(&X.cols[ri].height[(z & 15) << 4 | (x & 15)]) ? 15 : 0;
}

static __device__ __forceinline__ int biome_fast(const struct rch *k, int x, int z)
{
    int c = col_of(k, x, z);
    if (c < 0) return 0;
    const struct rcd_col *ch = colw(k, c);
    return ch ? ch->biome[(z & 15) << 4 | (x & 15)] : 0;
}

/* the reference's reads are raycast.c's alone: the device casts the fast path */
#define cell_ref cell_fast
#define light_ref light_fast
#define biome_ref biome_fast

/* raycast_seen's note for the pixel (pi, pj): the count, and the nearest
 * (the first in row order at equal distance, as the CPU's scan keeps) */
template <typename R>
static __device__ __forceinline__ void seen_note(const struct rch *k, int id, int x, int y, int z, R t, int pi, int pj)
{
    for (int q = 0; q < RCD_SEEN; ++q)
        if (k->seen_ids[q] == id)
        {
            int p = pj * X.w + pi;
            float d = (float)t;
            atomicAdd(&X.seen_n[q], 1);
            X.seen_xyz[3 * p] = x, X.seen_xyz[3 * p + 1] = y, X.seen_xyz[3 * p + 2] = z;
            atomicMin(&X.seen_key[q], (unsigned long long)__float_as_uint(d) << 32 | (unsigned)p);
            return;
        }
}

/* ------------------------------------------------ raycast.c's walk, ported */

/* The fast path's air loop in float (lane/rcdev: the 3090 runs double at 1/64 of float, and the loop's steps were
 * a chain of double operations with one or two lanes of a warp still walking). An axis' next boundary t is a pure
 * function of the cell index, ((double)(i + (s > 0)) - o) * id (raycast.c DDA_TX: every step and jump leaves it
 * so), so the loop keeps the indices and each axis' float shadow f of that double, with a bound e on |f - t|:
 * f = ((float)(i + (s > 0)) - (float)o) * (float)id is within u (3 |f| + |o| |id|) of t (u float's unit roundoff,
 * the index exact in float below 2^24, the o of a cast within 4e6 of 0), and e takes twice that. A comparison the
 * bounds settle is the doubles' comparison; one they do not is made in double; the doubles themselves are made
 * again from the indices when the loop leaves. So every decision, and every value the loop hands on, is the
 * double loop's (pool_gate --raycast-device holds it so). */
#define RC_UF 1.2e-7f
static __device__ __forceinline__ float rc_shadow(int i, int s, float of, float idf)
{
    return s ? ((float)(i + (s > 0)) - of) * idf : INFINITY;
}
static __device__ __forceinline__ float rc_bound(float f, int s, float oe) { return s ? (fabsf(f) * 3.0f + oe) * RC_UF : 0.0f; }
static __device__ __forceinline__ double rc_exact(int i, int s, double o, double id)
{
    return s ? ((double)(i + (s > 0)) - o) * id : INFINITY;
}
/* the same from the direction d: raycast.c's 1 / d (idx) made here, for a decision a ray seldom needs in double */
static __device__ __forceinline__ double rc_exactd(int i, int s, double o, double d)
{
    return s ? ((double)(i + (s > 0)) - o) * (1.0 / d) : INFINITY;
}
/* a <= b for the doubles the shadows stand for, b >= a's ... */
#define RC_LE(fa, ea, fb, eb, EXA, EXB) ((fa) + (ea) < (fb) - (eb) ? 1 : (fa) - (ea) > (fb) + (eb) ? 0 : (EXA) <= (EXB))
#define RC_GE(fa, ea, fb, eb, EXA, B) ((fa) - (ea) > (fb) + (eb) ? 1 : (fa) + (ea) < (fb) - (eb) ? 0 : (EXA) >= (B))

static __device__ __forceinline__ void rgb01(int32_t c, float *o)
{
    o[0] = (float)(c >> 16 & 255) / 255.0f;
    o[1] = (float)(c >> 8 & 255) / 255.0f;
    o[2] = (float)(c & 255) / 255.0f;
}

static __device__ __forceinline__ int liquid_of(int id) { return id == 8 || id == 9 ? 8 : id == 10 || id == 11 ? 10 : 0; }

/* a biome colour table's entry (the tables in global memory, read only) */
static __device__ __forceinline__ float3 rc_rgb(const float (*tab)[3], int bio)
{
    return make_float3(__ldg(&tab[bio][0]), __ldg(&tab[bio][1]), __ldg(&tab[bio][2]));
}
static __device__ __forceinline__ float3 rc_rgb01(int32_t c)
{
    float o[3];
    rgb01(c, o);
    return make_float3(o[0], o[1], o[2]);
}

/* the block's colour multiplier on a face, 0..1 (lane/casthot: by value; a pointer into a local array, a constant or
 * a table made every tint and lit read generic loads and the array local memory) */
static __device__ __forceinline__ float3 tint_of(const struct rch *k, int id, int meta, int side, int x, int y, int z, int fast)
{
    const float3 W = make_float3(1.0f, 1.0f, 1.0f);
    int t = __ldg(&T.tint[id]);
    if (t == TN_NONE) return W;
    int bio = fast ? biome_fast(k, x, z) : biome_ref(k, x, z);
    switch (t)
    {
    case TN_GRASS_TOP: return side == 1 ? rc_rgb(T.grass, bio) : W;
    case TN_GRASS: return rc_rgb(T.grass, bio);
    case TN_FOLIAGE: return rc_rgb(T.foliage, bio);
    case TN_OLDLEAF:
        if ((meta & 3) == 1) return rc_rgb01(6396257);
        if ((meta & 3) == 2) return rc_rgb01(8431445);
        return rc_rgb(T.foliage, bio);
    case TN_WATER: return rc_rgb(T.water, bio);
    case TN_TALLGRASS: return meta == 0 ? W : rc_rgb(T.grass, bio);
    case TN_STEM: return rc_rgb01(meta * 32 << 16 | (255 - meta * 8) << 8 | meta * 4);
    case TN_DPLANT:
    {
        int v = meta & 7;
        if (meta & 8)
        {
            int m2 = 0;
            (void)(fast ? cell_fast(k, x, y - 1, z, &m2) : cell_ref(k, x, y - 1, z, &m2));
            v = m2 & 7;
        }
        return v == 2 || v == 3 ? rc_rgb(T.grass, bio) : W;
    }
    }
    return W;
}

/* e to the X (X <= 0) in plain arithmetic, the same on every machine (a
 * libm's exp need not be): 2^n times a degree-8 series of the rest */
static __device__ __forceinline__ double exp_neg(double x)
{
    if (!(x > -40.0)) return 0.0;
    int n = (int)(x * 1.4426950408889634 - 0.5);
    double r = x - (double)n * 0.6931471805599453;
    double p = 1.0 + r * (1.0 + r * (0.5 + r * (1.0 / 6.0 + r * (1.0 / 24.0 + r * (1.0 / 120.0 + r * (1.0 / 720.0 + r * (1.0 / 5040.0 + r * (1.0 / 40320.0))))))));
    return p * __longlong_as_double((long long)((uint64_t)(n + 1023) << 52));
}

/* the FP32 instance's (raycast.c exp_neg32, raycast.h's definition): every operation a float's */
static __device__ __forceinline__ float exp_neg32(float x)
{
    if (!(x > -40.0f)) return 0.0f;
    int n = (int)(x * 1.44269502f - 0.5f);
    float r = (x - (float)n * 0.693145752f) - (float)n * 1.42860677e-6f;
    float p = 1.0f + r * (1.0f + r * (0.5f + r * (0.166666672f + r * (0.0416666679f + r * (0.00833333377f + r * 0.00138888892f)))));
    return p * __int_as_float((n + 127) << 23);
}
static __device__ __forceinline__ float rc_fog_exp(double x) { return (float)exp_neg(x); }
static __device__ __forceinline__ float rc_fog_exp(float x) { return exp_neg32(x); }
static __device__ __forceinline__ float rc_fog_exp(h16 x) { return exp_neg32(hf(x)); }

/* vanilla's face shading: DOWN, UP, NORTH, SOUTH, WEST, EAST (by selects: a table indexed by the face was a load) */
static __device__ __forceinline__ float SHADE_OF(int f) { return f == 0 ? 0.5f : f == 1 ? 1.0f : f < 4 ? 0.8f : 0.6f; }

/* a lit, fogged colour (0..255) from a texel or flat colour */
template <typename R>
static __device__ __forceinline__ void lit(const struct rch *k, float r, float g, float b, float3 tint, int sky, int block, int emit,
                       float shade, R depth, float out[3])
{
    if (block < emit) block = emit;
#if NETH_RC_HEAD_U8
    const uint32_t u = __ldg(&X.lm[sky * 16 + block]);
    out[0] = r * tint.x * ((float)((u >> 16) & 255) / 255.0f) * shade;
    out[1] = g * tint.y * ((float)((u >> 8) & 255) / 255.0f) * shade;
    out[2] = b * tint.z * ((float)(u & 255) / 255.0f) * shade;
#else
    const float *l = X.lm[sky * 16 + block];
    out[0] = r * tint.x * __ldg(&l[0]) * shade;
    out[1] = g * tint.y * __ldg(&l[1]) * shade;
    out[2] = b * tint.z * __ldg(&l[2]) * shade;
#endif
    float f;
    if (k->exp_fog) f = rc_fog_exp(-(R)k->fd * depth);
    else
    {
        f = (k->fe - (float)depth) * k->fspan;
        f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    }
    out[0] = out[0] * f + k->fog[0] * (1.0f - f);
    out[1] = out[1] * f + k->fog[1] * (1.0f - f);
    out[2] = out[2] * f + k->fog[2] * (1.0f - f);
}

/* the eye in the instance's scalar (raycast.h "The two instances"): FP64 the head's own doubles, the origin cell 0 (so
 * every expression is the reference's); FP32 the eye's offset in its cell, the origin cell the eye's (a cell i is the
 * float (i - b), exact), the camera and the range's limit as floats */
template <typename R> struct rce {
    R ox, oy, oz, fx, fy, fz, rnext;
    R rx, ry, rz, ux, uy, uz, aspect;   /* the camera's other axes and aspect (the direction tables') */
    int bx, by, bz;
};
#define RC_K(x) ((R)(x))
#define RC_CX(E, i) ((R)((i) - (E)->bx))
#define RC_CY(E, i) ((R)((i) - (E)->by))
#define RC_CZ(E, i) ((R)((i) - (E)->bz))

/* a face's texel at local (lx, ly, lz) in the cell: u and v as the
 * renderer's faces map them (DOWN and UP: x, z; NORTH: -x, -y; SOUTH: x,
 * -y; WEST: z, -y; EAST: -z, -y) */
template <typename R>
static __device__ __forceinline__ uint32_t texel(uint16_t spr, int face, R lx, R ly, R lz)
{
    R u, v;
    switch (face)
    {
    case 0: case 1: u = lx, v = lz; break;
    case 2: u = RC_K(1.0) - lx, v = RC_K(1.0) - ly; break;
    case 3: u = lx, v = RC_K(1.0) - ly; break;
    case 4: u = lz, v = RC_K(1.0) - ly; break;
    default: u = RC_K(1.0) - lz, v = RC_K(1.0) - ly; break;
    }
    int iu = (int)(u * RC_K(16.0)), iv = (int)(v * RC_K(16.0));
    iu = iu < 0 ? 0 : iu > 15 ? 15 : iu;
    iv = iv < 0 ? 0 : iv > 15 ? 15 : iv;
    return __ldg(&T.tex[spr][iv * 16 + iu]);
}

#define TEX_A(t) ((t) >> 24)
#define TEX_PASS(t) (TEX_A(t) > 25u)

/* the face a ray along +axis or -axis enters through */
static __device__ __forceinline__ int entry_face(int axis, int s)
{
    return axis == 0 ? (s > 0 ? 4 : 5) : axis == 1 ? (s > 0 ? 0 : 1) : (s > 0 ? 2 : 3);
}

/* the DDA's state: the cell, each axis' step, its reciprocal direction and
 * its next boundary's t (from the cell's index alone, so a jump over cells
 * lands where cell-by-cell steps would) */
template <typename R> struct dda {
    int ix, iy, iz, sx, sy, sz;
    R idx, idy, idz, tx, ty, tz;
    R dx, dy, dz;               /* the direction: a jump's first guess at an axis' cell */
};

#define DDA_TX(k, g) ((RC_CX(k, (g)->ix + ((g)->sx > 0)) - (k)->ox) * (g)->idx)
#define DDA_TY(k, g) ((RC_CY(k, (g)->iy + ((g)->sy > 0)) - (k)->oy) * (g)->idy)
#define DDA_TZ(k, g) ((RC_CZ(k, (g)->iz + ((g)->sz > 0)) - (k)->oz) * (g)->idz)

/* The fixed instance's order (FX, raycast.c rc_fx_cmp; raycast.h "The instances"): the sign of t_a - t_b for the
 * exact t = (b - o) / d of two boundaries (b their coordinates in the frame, o and d on RC_FIXF and RC_FIXG bits): the
 * floats' order where they are more than 2^-20 apart relative, the 64-bit products' where closer */
/* (lane/cxfix's knobs, raycast.c's RC_FXNEAR too: which pairs go to the integers, any superset of the floats' 2^-20
 * giving the same bytes, the integers being exact: 0 the relative test |ta - tb| <= (|ta| + |tb|) 2^-20; 1 the two
 * floats within RC_FXULP units in the last place, on their bits ordered as integers (-0 next to +0), which holds every
 * pair the relative test does at 64 or more (2^-19 of the larger is at most 64 ulps of the smaller's binade), the same
 * bytes and 0.2 to 1.9% slower under RC_FXSTEP on the stored casts (DEVLOG lane/cxfix), so 0 stays;
 * RC_FXNOINL 1: the 64-bit products out of line, a call where the pair is near) */
#if !defined(RC_FXNEAR)
#define RC_FXNEAR 0
#endif
#if !defined(RC_FXULP)
#define RC_FXULP 128
#endif
#if !defined(RC_FXNOINL)
#define RC_FXNOINL 0
#endif
#if RC_FXNOINL
static __device__ __noinline__ int rc_fx_exact(int ba, float oa, float da, int bb, float ob, float db)
#else
static __device__ __forceinline__ int rc_fx_exact(int ba, float oa, float da, int bb, float ob, float db)
#endif
{
    const long long pa = (long long)ba * (1ll << RC_FIXF) - (long long)(oa * (float)(1 << RC_FIXF));
    const long long pb = (long long)bb * (1ll << RC_FIXF) - (long long)(ob * (float)(1 << RC_FIXF));
    const long long qa = (long long)(da * (float)(1 << RC_FIXG)), qb = (long long)(db * (float)(1 << RC_FIXG));
    const long long x = pa * qb - pb * qa;
    const int s = (x > 0) - (x < 0);
    return (qa > 0) == (qb > 0) ? s : -s;
}
#if RC_FXNEAR == 1
/* a float's bits as an integer in the floats' order (each next float one more; -0 is -1, +0 0) */
static __device__ __forceinline__ int rc_ford(float x)
{
    const int b = __float_as_int(x);
    return b ^ ((b >> 31) & 0x7fffffff);
}
#endif
static __device__ __forceinline__ int rc_fx_cmp(float ta, int ba, float oa, float da, float tb, int bb, float ob, float db)
{
#if RC_FXNEAR == 1
    if ((unsigned)(rc_ford(ta) - rc_ford(tb)) + (unsigned)RC_FXULP > 2u * (unsigned)RC_FXULP) return (ta > tb) - (ta < tb);
#else
    const float g = (fabsf(ta) + fabsf(tb)) * 0x1p-20f;
    if (!(fabsf(ta - tb) <= g) || !(g < INFINITY)) return (ta > tb) - (ta < tb);
#endif
    return rc_fx_exact(ba, oa, da, bb, ob, db);
}
/* (RC_FXSTEP, lane/cxfix) the air loop's step: whether the pair goes to the integers (rc_fx_cmp's test alone), and the
 * step's two decisions by rc_fx_cmp out of line, for a step where some pair does; elsewhere every rc_fx_cmp is the
 * floats' order, so the floats' compares are its decisions and the step stays FP32's, without branches */
#if !defined(RC_FXSTEP)
#define RC_FXSTEP 1
#endif
#if RC_FXSTEP
static __device__ __forceinline__ int rc_fx_near(float ta, float tb)
{
#if RC_FXNEAR == 1
    return (unsigned)(rc_ford(ta) - rc_ford(tb)) + (unsigned)RC_FXULP <= 2u * (unsigned)RC_FXULP;
#else
    const float g = (fabsf(ta) + fabsf(tb)) * 0x1p-20f;
    return fabsf(ta - tb) <= g && g < INFINITY;
#endif
}
static __device__ __noinline__ void rc_fx_step(float tx, float ty, float tz, int ux, int uy, int uz, float ox, float oy,
                                               float oz, float dx, float dy, float dz, int *stx, int *sty)
{
    *stx = rc_fx_cmp(tx, ux, ox, dx, ty, uy, oy, dy) <= 0 && rc_fx_cmp(tx, ux, ox, dx, tz, uz, oz, dz) <= 0;
    *sty = !*stx && rc_fx_cmp(ty, uy, oy, dy, tz, uz, oz, dz) <= 0;
}
#endif
/* t_a <= t_b (LE) or < (LT): FX the exact order, else the scalars' */
template <bool FX, typename R>
static __device__ __forceinline__ int rc_tle(R ta, int ba, R oa, R da, R tb, int bb, R ob, R db)
{
    if constexpr (FX) return rc_fx_cmp((float)ta, ba, (float)oa, (float)da, (float)tb, bb, (float)ob, (float)db) <= 0;
    else return ta <= tb;
}
template <bool FX, typename R>
static __device__ __forceinline__ int rc_tlt(R ta, int ba, R oa, R da, R tb, int bb, R ob, R db)
{
    if constexpr (FX) return rc_fx_cmp((float)ta, ba, (float)oa, (float)da, (float)tb, bb, (float)ob, (float)db) < 0;
    else return ta < tb;
}
#define DDA_BX(k, g) ((g)->ix + ((g)->sx > 0) - (k)->bx)
#define DDA_BY(k, g) ((g)->iy + ((g)->sy > 0) - (k)->by)
#define DDA_BZ(k, g) ((g)->iz + ((g)->sz > 0) - (k)->bz)

/* one step to the next cell: its entry t and the axis crossed (ties x, then y, then z) */
template <typename R, bool FX = false>
static __device__ __forceinline__ R dda_step(const struct rce<R> *k, struct dda<R> *g, int *axis)
{
    R t;
    if (rc_tle<FX>(g->tx, DDA_BX(k, g), k->ox, g->dx, g->ty, DDA_BY(k, g), k->oy, g->dy) &&
        rc_tle<FX>(g->tx, DDA_BX(k, g), k->ox, g->dx, g->tz, DDA_BZ(k, g), k->oz, g->dz))
    {
        t = g->tx;
        g->ix += g->sx;
        g->tx = DDA_TX(k, g);
        *axis = 0;
    }
    else if (rc_tle<FX>(g->ty, DDA_BY(k, g), k->oy, g->dy, g->tz, DDA_BZ(k, g), k->oz, g->dz))
    {
        t = g->ty;
        g->iy += g->sy;
        g->ty = DDA_TY(k, g);
        *axis = 1;
    }
    else
    {
        t = g->tz;
        g->iz += g->sz;
        g->tz = DDA_TZ(k, g);
        *axis = 2;
    }
    return t;
}

/* where cell-by-cell steps along one axis (origin O, 1 / direction ID,
 * direction D, step S) from cell I stop when the box's exit event at TE
 * comes: past every boundary crossed before it (at it too when WIN, the
 * axis winning the tie), never past LAST. The ray's coordinate at TE
 * guesses the cell; the boundaries' own t settle it, as the steps would.
 * B: the origin cell on the axis (raycast.c's: 0 in FP64). FP64 trusts the
 * guess 1e-6 clear of a boundary (the boundaries' t carry errors near 1e-14
 * of a block, the guess near 1e-8 at the world's edge), FP32 1/1024 */
static __device__ __forceinline__ double rc_adv_eps(double) { return 1e-6; }
static __device__ __forceinline__ float rc_adv_eps(float) { return 0.0009765625f; }
static __device__ __forceinline__ h16 rc_adv_eps(h16) { return h16(0.5f); }   /* (FP16: never trusted, raycast.c's) */
static __device__ __forceinline__ int adv_crossed(int c, int win) { return c < 0 || (c == 0 && win); }
/* (bE, oE, dE: the exit event's boundary, offset and direction, for FX's exact order) */
template <typename R, bool FX = false>
static __device__ __forceinline__ int dda_advance(R o, R id, R d, int i, int s, int last, R tE, int win, int B, int bE = 0,
                                                  R oE = R(0), R dE = R(0))
{
    const R eps = rc_adv_eps(R(0));
    R p = o + tE * d;
    int f = (int)p;
    if ((R)f > p) --f;
    R fr = p - (R)f;
    f += B;
    int in = s > 0 ? f >= i && f <= last : f <= i && f >= last;
    if (in && fr > eps && fr < RC_K(1.0) - eps) return f;
    if (s > 0) f = f < i ? i : f > last ? last : f;
    else f = f > i ? i : f < last ? last : f;
#define ADV_T(j) (((R)((j) + (s > 0) - B) - o) * id)
#define ADV_CROSSED(j) (FX ? adv_crossed(rc_fx_cmp((float)ADV_T(j), (j) + (s > 0) - B, (float)o, (float)d, (float)tE, bE, (float)oE, (float)dE), win) \
                           : (ADV_T(j) < tE || (ADV_T(j) == tE && win)))
    while (f != last && ADV_CROSSED(f)) f += s;
    while (f != i && !ADV_CROSSED(f - s)) f -= s;
#undef ADV_CROSSED
#undef ADV_T
    return f;
}

/* on to the last cell of the aligned box of side B (16 a section, a power of
 * two) holding the current cell before the ray leaves it, in one jump: the
 * box's exit is the least of the three boundary events, each other axis
 * advanced past every event the steps would have taken before it */
template <typename R, bool FX = false>
static __device__ __forceinline__ void dda_jump(const struct rce<R> *k, struct dda<R> *g, int B)
{
    R tE = INFINITY, oE = R(0), dE = R(0);
    int aE = -1, bE = 0;
    int lx = g->sx > 0 ? g->ix | (B - 1) : g->ix & ~(B - 1);
    int ly = g->sy > 0 ? g->iy | (B - 1) : g->iy & ~(B - 1);
    int lz = g->sz > 0 ? g->iz | (B - 1) : g->iz & ~(B - 1);
    if (g->sx)
    {
        R e = (RC_CX(k, lx + (g->sx > 0)) - k->ox) * g->idx;
        tE = e, aE = 0, bE = lx + (g->sx > 0) - k->bx, oE = k->ox, dE = g->dx;
    }
    if (g->sy)
    {
        R e = (RC_CY(k, ly + (g->sy > 0)) - k->oy) * g->idy;
        const int b = ly + (g->sy > 0) - k->by;
        if (rc_tlt<FX>(e, b, k->oy, g->dy, tE, bE, oE, dE)) tE = e, aE = 1, bE = b, oE = k->oy, dE = g->dy;
    }
    if (g->sz)
    {
        R e = (RC_CZ(k, lz + (g->sz > 0)) - k->oz) * g->idz;
        const int b = lz + (g->sz > 0) - k->bz;
        if (rc_tlt<FX>(e, b, k->oz, g->dz, tE, bE, oE, dE)) tE = e, aE = 2, bE = b, oE = k->oz, dE = g->dz;
    }
    if (aE == 0) g->ix = lx, g->tx = tE;
    else if (g->sx) g->ix = dda_advance<R, FX>(k->ox, g->idx, g->dx, g->ix, g->sx, lx, tE, aE > 0, k->bx, bE, oE, dE), g->tx = DDA_TX(k, g);
    if (aE == 1) g->iy = ly, g->ty = tE;
    else if (g->sy) g->iy = dda_advance<R, FX>(k->oy, g->idy, g->dy, g->iy, g->sy, ly, tE, aE > 1, k->by, bE, oE, dE), g->ty = DDA_TY(k, g);
    if (aE == 2) g->iz = lz, g->tz = tE;
    else if (g->sz) g->iz = dda_advance<R, FX>(k->oz, g->idz, g->dz, g->iz, g->sz, lz, tE, 0, k->bz, bE, oE, dE), g->tz = DDA_TZ(k, g);
}

/* the dda's reciprocals and boundary t from its cells, for the paths that read them (every step and jump leaves
 * them functions of the cells, so making them late gives the values early ones would have) */
template <typename R>
static __device__ __forceinline__ void rc_fill(const struct rce<R> *k, struct dda<R> *g, R idx, R idy, R idz)
{
    g->idx = idx, g->idy = idy, g->idz = idz;
    g->tx = g->sx ? DDA_TX(k, g) : INFINITY;
    g->ty = g->sy ? DDA_TY(k, g) : INFINITY;
    g->tz = g->sz ? DDA_TZ(k, g) : INFINITY;
}

/* -1, 0 or 1 by d's sign bits (raycast.c's d > 0 ? 1 : d < 0 ? -1 : 0, -0 0), in integer arithmetic */
static __device__ __forceinline__ int rc_sign(double d)
{
    long long b = __double_as_longlong(d);
    return (b << 1) == 0 ? 0 : b < 0 ? -1 : 1;
}
static __device__ __forceinline__ int rc_sign(h16 d)
{
    const float f = hf(d);
    return f > 0.0f ? 1 : f < 0.0f ? -1 : 0;
}
static __device__ __forceinline__ int rc_sign(float d)
{
    int b = __float_as_int(d);
    return (b << 1) == 0 ? 0 : b < 0 ? -1 : 1;
}

/* dda_jump's landing over a box of 16 in float (lane/rcsplit): the exit axis from the boundaries' shadows (the
 * doubles where the bounds do not settle it), and each other axis' cell from the ray's coordinate at the exit,
 * q = (o - floor(o)) + tE d in float with its error bound: where q's fraction is clear of a boundary by more than
 * that bound and dda_advance's 1e-6, dda_advance would take the same guess (its own fraction is inside then too);
 * anywhere else, dda_advance itself on the exact tE. So the cells are dda_jump's. */
struct rc_jray {
    int sx, sy, sz, oix, oiy, oiz;
    double ox, oy, oz, dx, dy, dz;
    float oxf, oyf, ozf, idxf, idyf, idzf, oxe, oye, oze, qx, qy, qz, dxf, dyf, dzf;
};
static __device__ __forceinline__ int rc_land(const struct rc_jray *r, int a, int i, int last, float fE, float bE)
{
    const int s = a == 0 ? r->sx : a == 1 ? r->sy : r->sz;
    const float q0 = a == 0 ? r->qx : a == 1 ? r->qy : r->qz, df = a == 0 ? r->dxf : a == 1 ? r->dyf : r->dzf;
    const int oi = a == 0 ? r->oix : a == 1 ? r->oiy : r->oiz;
    float q = q0 + fE * df;
    float err = (bE + fabsf(fE) * 2.4e-7f) * fabsf(df) + (fabsf(q) + 2.0f) * 2.4e-7f + 1e-6f;
    float fl = floorf(q), fr = q - fl;
    int f = oi + (int)fl;
    int in = s > 0 ? f >= i && f <= last : f <= i && f >= last;
    if (in && fr > err && fr < 1.0f - err) return f;
    return -0x7fffffff;
}
static __device__ __forceinline__ void rc_jump(const struct rc_jray *r, int *ix, int *iy, int *iz)
{
    const int sx = r->sx, sy = r->sy, sz = r->sz;
    int lx = sx > 0 ? *ix | 15 : *ix & ~15;
    int ly = sy > 0 ? *iy | 15 : *iy & ~15;
    int lz = sz > 0 ? *iz | 15 : *iz & ~15;
    float fE = INFINITY, bE = 0.0f;
    int aE = -1;
    if (sx) fE = rc_shadow(lx, sx, r->oxf, r->idxf), bE = rc_bound(fE, sx, r->oxe), aE = 0;
    if (sy)
    {
        float f = rc_shadow(ly, sy, r->oyf, r->idyf), b = rc_bound(f, sy, r->oye);
        /* e < tE: y takes it (ties stay with the earlier axis) */
        if (aE < 0 || !RC_LE(fE, bE, f, b, rc_exactd(lx, sx, r->ox, r->dx), rc_exactd(ly, sy, r->oy, r->dy)))
            fE = f, bE = b, aE = 1;
    }
    if (sz)
    {
        float f = rc_shadow(lz, sz, r->ozf, r->idzf), b = rc_bound(f, sz, r->oze);
        if (aE < 0 || !RC_LE(fE, bE, f, b, (aE == 0 ? rc_exactd(lx, sx, r->ox, r->dx) : rc_exactd(ly, sy, r->oy, r->dy)),
                             rc_exactd(lz, sz, r->oz, r->dz)))
            fE = f, bE = b, aE = 2;
    }
    int nx = *ix, ny = *iy, nz = *iz;
    if (aE == 0) nx = lx;
    else if (sx) nx = rc_land(r, 0, *ix, lx, fE, bE);
    if (aE == 1) ny = ly;
    else if (sy) ny = rc_land(r, 1, *iy, ly, fE, bE);
    if (aE == 2) nz = lz;
    else if (sz) nz = rc_land(r, 2, *iz, lz, fE, bE);
    if (nx == -0x7fffffff || ny == -0x7fffffff || nz == -0x7fffffff)
    {
        /* a cell the float guess does not settle: the exact tE and dda_advance for the axes not settled */
        double tE = aE == 0 ? rc_exactd(lx, sx, r->ox, r->dx) : aE == 1 ? rc_exactd(ly, sy, r->oy, r->dy)
                                                                          : rc_exactd(lz, sz, r->oz, r->dz);
        if (nx == -0x7fffffff) nx = dda_advance(r->ox, 1.0 / r->dx, r->dx, *ix, sx, lx, tE, aE > 0, 0);
        if (ny == -0x7fffffff) ny = dda_advance(r->oy, 1.0 / r->dy, r->dy, *iy, sy, ly, tE, aE > 1, 0);
        if (nz == -0x7fffffff) nz = dda_advance(r->oz, 1.0 / r->dz, r->dz, *iz, sz, lz, tE, 0, 0);
    }
    *ix = nx, *iy = ny, *iz = nz;
}

/* (RC_H2, lane/cxh2) where a ray's first air loop ended, made by walk2's packed loop for walk to go on from: the
 * cell, its next boundaries' t, the step's t and axis, the cell's id and meta, the loop's in_water, cull and
 * eye_was_liquid, the section; kind 0 a block (walk's block), 2 the run ended with nothing found (the entity or the
 * miss, by t as walk's loop decides), 3 the general path (the loop not entered: nothing changed); and the nearest
 * entity box (walk's loop over them, done there) */
template <typename R> struct rc_res {
    int ix, iy, iz, axis, id, meta, inw, cl, ewl, kind, ent, eface;
    R tx, ty, tz, t, te;
    const struct rcd_sec *cur;
};

/* one ray: the same arithmetic for both casts; fast only changes how a cell
 * is read and crosses air without the general per-cell work (an empty
 * section in one jump); FX: the fixed instance's exact order (float's walk otherwise); rs (FP16's packed variant,
 * else NULL): the first air loop's end and the entity box, from walk2 */
template <typename R, bool FX = false>
static __device__ __forceinline__ void walk(const struct rch *k, const struct rce<R> *E, const R *d, int pi, int pj, int fast,
                                            const struct rc_res<R> *rs, uint8_t *out RC_STATS_PARAM)
{
    /* FP64: lane/casthot's walk, the air loop's decisions on float shadows of its doubles; FP32: the float walk */
    constexpr bool F64 = sizeof(R) == 8;
    R dx = d[0], dy = d[1], dz = d[2];
    R fwd = dx * E->fx + dy * E->fy + dz * E->fz;
    struct dda<R> g;
    g.ix = k->eye_x, g.iy = k->eye_y, g.iz = k->eye_z;
    g.sx = rc_sign(dx), g.sy = rc_sign(dy), g.sz = rc_sign(dz);
    g.dx = dx, g.dy = dy, g.dz = dz;
    const int sx = g.sx, sy = g.sy, sz = g.sz;
    /* raycast.c's 1 / d, made at the first path that reads it (RC_HID: the entity boxes, a decision the shadows leave
     * open, the general path, a cell's exit; FP32 at once); FP64's last step's t divides alone (T_LAST). g's
     * reciprocals and t are made with them (gt) */
    R idx = 0, idy = 0, idz = 0;
    int hid = 0, gt = 0;
#define RC_HID() (hid ? 0 : (idx = sx ? RC_K(1.0) / dx : RC_K(0.0), idy = sy ? RC_K(1.0) / dy : RC_K(0.0), idz = sz ? RC_K(1.0) / dz : RC_K(0.0), hid = 1))
#define RC_GT() (gt ? 0 : (RC_HID(), rc_fill(E, &g, idx, idy, idz), gt = 1))
    if constexpr (!F64) RC_HID();
    R range = (R)k->range;
    /* FP64's air loop shadows (above): each axis' o and 1 / d in float (lane/rcsplit: the float quotient, within 2 u
     * of the double's rounding; rc_bound's 2 u (3 |f| + |o| |id|) covers the 4 u |f| + u |o| |id| that leaves), and
     * the bound's |o| |id| term (infinite, so every decision in double, for an o past 4e6) */
    float oxf = 0.0f, oyf = 0.0f, ozf = 0.0f, idxf = 0.0f, idyf = 0.0f, idzf = 0.0f, oxe = 0.0f, oye = 0.0f, oze = 0.0f;
    struct rc_jray jr;
    if constexpr (F64)
    {
        oxf = X.ofl[0], oyf = X.ofl[1], ozf = X.ofl[2];
        idxf = sx ? 1.0f / (float)dx : 0.0f, idyf = sy ? 1.0f / (float)dy : 0.0f, idzf = sz ? 1.0f / (float)dz : 0.0f;
        oxe = 2.0f * X.oab[0] * fabsf(idxf), oye = 2.0f * X.oab[1] * fabsf(idyf), oze = 2.0f * X.oab[2] * fabsf(idzf);
        jr.sx = sx, jr.sy = sy, jr.sz = sz;
        jr.ox = E->ox, jr.oy = E->oy, jr.oz = E->oz, jr.dx = dx, jr.dy = dy, jr.dz = dz;
        jr.oxf = oxf, jr.oyf = oyf, jr.ozf = ozf, jr.idxf = idxf, jr.idyf = idyf, jr.idzf = idzf, jr.oxe = oxe, jr.oye = oye, jr.oze = oze;
        jr.oix = X.oi[0], jr.oiy = X.oi[1], jr.oiz = X.oi[2];
        jr.qx = X.ofr[0], jr.qy = X.ofr[1], jr.qz = X.ofr[2];
        jr.dxf = (float)dx, jr.dyf = (float)dy, jr.dzf = (float)dz;
    }

    /* the nearest entity box this pixel can see (the head's boxes in the frame's coordinates: FP32's floats widened) */
    R te = INFINITY;
    int ent = -1, eface = 1;
    if (rs) te = rs->te, ent = rs->ent, eface = rs->eface;
    else for (int e = 0; e < k->nent; ++e)
    {
        const struct rcd_ent *en = &X.ent[e];
        const int2 ri = __ldg((const int2 *)&en->i0), rj = __ldg((const int2 *)&en->j0);   /* the boxes are 8-aligned */
        if (pi < ri.x || pi > ri.y || pj < rj.x || pj > rj.y) continue;
        R tn = -INFINITY, tf = INFINITY;
        int ax = -1;
        RC_HID();
        const R o3[3] = {E->ox, E->oy, E->oz}, i3[3] = {idx, idy, idz};
        const int s3[3] = {sx, sy, sz};
        int miss = 0;
        for (int a = 0; a < 3 && !miss; ++a)
        {
            const R b0 = (R)__ldg(&en->box[a]), b1 = (R)__ldg(&en->box[a + 3]);
            if (!s3[a])
            {
                if (o3[a] < b0 || o3[a] > b1) miss = 1;
                continue;
            }
            R t0 = (b0 - o3[a]) * i3[a], t1 = (b1 - o3[a]) * i3[a];
            if (t0 > t1) { R s = t0; t0 = t1; t1 = s; }
            if (t0 > tn) tn = t0, ax = a;
            if (t1 < tf) tf = t1;
        }
        if (miss || ax < 0 || tn > tf || tn <= RC_K(0.0) || tn >= te) continue;
        te = tn, ent = e, eface = entry_face(ax, s3[ax]);
    }

    float acc[3] = {0, 0, 0}, trans = 1.0f, col[3];
    int in_water = 0, cull = -1;
    const int eye_liquid = k->liquid;
    int eye_was_liquid = eye_liquid != 0;
    /* the fast path's runs: y past ytop ends one; t past tlim (the entity's, or
     * the range's: t > range) too; the cell index a step on each axis
     * enters a section at (e*) */
    const unsigned ytop = sy >= 0 ? (unsigned)k->top : 256u;
    const R tlim = te < E->rnext ? te : E->rnext;
    const float tlf = (float)tlim, tle = fabsf(tlf) * RC_UF + 1e-30f;   /* (FP64's air loop) */
    const int ex = sx > 0 ? 0 : 15, ey = sy > 0 ? 0 : 15, ez = sz > 0 ? 0 : 15;
    /* the eye in water: a ray going up out of it meets the surface (the general path) */
    const int up_out = eye_liquid == 8 && sy > 0;
    /* the fast path's section of the current cell */
    const struct rcd_sec *cs = NULL;
    int csx = INT32_MIN, csy = INT32_MIN, csz = INT32_MIN;
    if (fast && g.iy >= k->top && sy >= 0) goto miss;
    for (int rsd = 0;; rsd = 1)
    {
        int axis, id, meta = 0;
        R t;
        RC_STAT(3);
        if constexpr (!F64)
        {
            if (rs && !rsd)
            {
                /* (RC_H2) the first air loop ran in walk2: its end as the loop below would leave it */
                in_water = rs->inw, cull = rs->cl, eye_was_liquid = rs->ewl;
                if (rs->kind == 3) goto general;
                g.ix = rs->ix, g.iy = rs->iy, g.iz = rs->iz, g.idx = idx, g.idy = idy, g.idz = idz;
                g.tx = rs->tx, g.ty = rs->ty, g.tz = rs->tz, gt = 1;
                t = rs->t, axis = rs->axis, id = rs->id, meta = rs->meta;
                cs = rs->cur, csx = g.ix >> 4, csy = g.iy >> 4, csz = g.iz >> 4;
                if (rs->kind == 0) goto block;
                if (t >= te) goto entity;
                goto miss;
            }
            if (fast && eye_liquid != 10 && (unsigned)g.iy <= 255u)
            {
                /* FP32: the air loop of raycast.c's walk in plain floats: each axis' next boundary t from its cell
                 * (DDA_TX's expression), the step's axis by the same compares (ties x, then y, then z), on the chosen
                 * axis by selects (lane/casthot's form), an empty section crossed by dda_jump; the same steps and
                 * decisions in the same order as FP64's below: the step, the top (y), the range's end, a section's
                 * boundary, the cell */
                int ix = g.ix, iy = g.iy, iz = g.iz;
                /* each axis' next boundary in the frame's coordinates, u = (i - b) + (s > 0), a whole number in a
                 * float (|u| < 2^24: exact), stepped by s: (u - o) id is DDA_TX's expression, the same bits */
                R ux = RC_CX(E, ix + (sx > 0)), uy = RC_CY(E, iy + (sy > 0)), uz = RC_CZ(E, iz + (sz > 0));
                const R fsx = (R)sx, fsy = (R)sy, fsz = (R)sz;
                R tx = sx ? (ux - E->ox) * idx : (R)INFINITY;
                R ty = sy ? (uy - E->oy) * idy : (R)INFINITY;
                R tz = sz ? (uz - E->oz) * idz : (R)INFINITY;
                int found = 0, inw = in_water, cl = cull, ewl = eye_was_liquid, si, ci;
                int c0 = col_of(k, ix, iz);
                si = c0 * 16 + (iy >> 4);
                const struct rcd_sec *cur = c0 >= 0 ? secw(k, si) : NULL;
                ci = SEC_XYZ(ix & 15, iy, iz & 15);
                t = RC_K(0.0);
                int jump = 0;
#if RC_BRICKJ
                int lastb = -1;     /* the brick last tested (ci >> 6) in this section */
#endif
                if (cur == NULL)
                {
                    /* the current cell's section is empty: the cells after it in it are air */
                    if (ewl && up_out) goto general;
                    jump = 1;
                }
                for (;;)
                {
                    if (jump)
                    {
                        RC_STAT(1);
                        jump = 0;
                        inw = 0, cl = -1, ewl = 0;
                        struct dda<R> j = g;
                        j.ix = ix, j.iy = iy, j.iz = iz, j.idx = idx, j.idy = idy, j.idz = idz, j.tx = tx, j.ty = ty, j.tz = tz;
                        dda_jump<R, FX>(E, &j, 16);
                        ix = j.ix, iy = j.iy, iz = j.iz, tx = j.tx, ty = j.ty, tz = j.tz;
                        ux = RC_CX(E, ix + (sx > 0)), uy = RC_CY(E, iy + (sy > 0)), uz = RC_CZ(E, iz + (sz > 0));
                    }
                    RC_STAT(0);
                    /* (FX: each pair by the exact order, the boundaries' coordinates u whole numbers; RC_FXSTEP: the
                     * floats' three compares as FP32's, and the exact ones out of line where any pair is near) */
#if RC_FXSTEP
                    int stx, sty;
                    if constexpr (FX)
                    {
                        stx = tx <= ty && tx <= tz, sty = !stx && ty <= tz;
                        if (__builtin_expect(rc_fx_near((float)tx, (float)ty) | rc_fx_near((float)tx, (float)tz) |
                                             rc_fx_near((float)ty, (float)tz), 0))
                            rc_fx_step((float)tx, (float)ty, (float)tz, (int)ux, (int)uy, (int)uz, (float)E->ox, (float)E->oy,
                                       (float)E->oz, (float)dx, (float)dy, (float)dz, &stx, &sty);
                    }
                    else stx = tx <= ty && tx <= tz, sty = !stx && ty <= tz;
#else
                    const int stx = rc_tle<FX>(tx, (int)ux, E->ox, dx, ty, (int)uy, E->oy, dy) &&
                                    rc_tle<FX>(tx, (int)ux, E->ox, dx, tz, (int)uz, E->oz, dz),
                              sty = !stx && rc_tle<FX>(ty, (int)uy, E->oy, dy, tz, (int)uz, E->oz, dz);
#endif
                    const int a = stx ? 0 : sty ? 1 : 2;
                    const int s = a == 0 ? sx : a == 1 ? sy : sz;
                    const int ia = (a == 0 ? ix : a == 1 ? iy : iz) + s;
                    const R ta = a == 0 ? tx : a == 1 ? ty : tz;
                    const R oa = a == 0 ? E->ox : a == 1 ? E->oy : E->oz, ida = a == 0 ? idx : a == 1 ? idy : idz;
                    const R nu = (a == 0 ? ux : a == 1 ? uy : uz) + (a == 0 ? fsx : a == 1 ? fsy : fsz);
                    const R nt = (nu - oa) * ida;
                    t = ta, axis = a;
                    if (a == 0) ix = ia, tx = nt, ux = nu;
                    if (a == 1) iy = ia, ty = nt, uy = nu;
                    if (a == 2) iz = ia, tz = nt, uz = nu;
                    const int top = a == 1 && (unsigned)iy >= ytop;
                    ci = rc_mstep(ci, a == 0 ? RC_MX : a == 1 ? RC_MY : RC_MZ, s);
                    const int bnd = (ia & 15) == (a == 0 ? ex : a == 1 ? ey : ez);
                    if (top || t >= tlim) break;
                    if (bnd)
                    {
                        /* into another section */
                        ci = SEC_XYZ(ix & 15, iy, iz & 15);
#if RC_BRICKJ
                        lastb = -1;
#endif
                        int c = col_of(k, ix, iz);
                        si = c * 16 + (iy >> 4);
                        cur = c >= 0 ? secw(k, si) : NULL;
                        if (cur == NULL)
                        {
                            if (ewl && up_out && axis == 1)
                            {
                                id = 0, meta = 0, found = 1;
                                break;
                            }
                            jump = 1;
                            continue;
                        }
                    }
                    id = __ldg(&cur->ids[ci]);
                    const int up = ewl && up_out && axis == 1, wet = id == 8 || id == 9;
                    if (id == 0 && !up)
                    {
                        inw = 0, cl = -1, ewl = 0;
#if RC_BRICKJ
                        /* a 4 x 4 x 4 brick of air (in Morton order its 64 cells are 64 bytes, ci's above the low six
                         * bits) entered with the eye dry: the steps through it read 0 and change nothing, so the ray
                         * goes to its last cell in one jump (dda_jump's, where the steps land) when the brick's exit
                         * comes before the ray's limit (no step inside could have ended the run) */
                        if (eye_liquid == 0 && (ci >> 6) != lastb)
                        {
                            lastb = ci >> 6;
                            const uint4 *bp = (const uint4 *)(cur->ids + (ci & ~63));
                            const uint4 b0 = __ldg(bp), b1 = __ldg(bp + 1), b2 = __ldg(bp + 2), b3 = __ldg(bp + 3);
                            if (!(b0.x | b0.y | b0.z | b0.w | b1.x | b1.y | b1.z | b1.w | b2.x | b2.y | b2.z | b2.w | b3.x | b3.y |
                                  b3.z | b3.w))
                            {
                                struct dda<R> j = g;
                                j.ix = ix, j.iy = iy, j.iz = iz, j.idx = idx, j.idy = idy, j.idz = idz, j.tx = tx, j.ty = ty, j.tz = tz;
                                dda_jump<R, FX>(E, &j, 4);
                                const R te4 = j.tx < j.ty ? (j.tx < j.tz ? j.tx : j.tz) : (j.ty < j.tz ? j.ty : j.tz);
                                if (te4 < tlim)
                                {
                                    ix = j.ix, iy = j.iy, iz = j.iz, tx = j.tx, ty = j.ty, tz = j.tz;
                                    ux = RC_CX(E, ix + (sx > 0)), uy = RC_CY(E, iy + (sy > 0)), uz = RC_CZ(E, iz + (sz > 0));
                                    ci = SEC_XYZ(ix & 15, iy, iz & 15);
                                }
                            }
                        }
#endif
                        continue;
                    }
                    /* the eye's own water: seen through */
                    if (id != 0 && eye_liquid && wet)
                    {
                        cl = -1, ewl = 1;
                        continue;
                    }
                    /* on through the water the ray is already in (its surface was blended) */
                    if (id != 0 && !up && inw && wet)
                    {
                        ewl = 0;
                        continue;
                    }
                    meta = id ? nibble_get(chunk_sec_metas(cur), ci) : 0;
                    found = 1;
                    break;
                }
                in_water = inw, cull = cl, eye_was_liquid = ewl;
                g.ix = ix, g.iy = iy, g.iz = iz, g.idx = idx, g.idy = idy, g.idz = idz, g.tx = tx, g.ty = ty, g.tz = tz, gt = 1;
                cs = cur, csx = ix >> 4, csy = iy >> 4, csz = iz >> 4;
                if (found) goto block;
                if (t >= te) goto entity;
                goto miss;
            }
        }
        else if (fast && eye_liquid != 10 && (unsigned)g.iy <= 255u)
        {
            /* air (and water the ray sees through) cell by cell, an empty
             * section in one jump, until a block, the ray's end or something
             * the general path decides; the DDA in locals, the section and
             * cell index worked out again only as the ray enters a section
             * (a jump leaves the ray on the section's last cell, so the next
             * step enters one). inw, cl and ewl are in_water, cull and
             * eye_was_liquid as the general path would leave them. 4x4x4
             * bricks of air crossed in one jump cost more than they saved
             * (lane/raycast: 1.91 M instructions a 32x32 cast against 1.65). */
            int ix = g.ix, iy = g.iy, iz = g.iz;
            const double ox = E->ox, oy = E->oy, oz = E->oz;
            float fx = rc_shadow(ix, sx, oxf, idxf), fy = rc_shadow(iy, sy, oyf, idyf), fz = rc_shadow(iz, sz, ozf, idzf);
            float bx = rc_bound(fx, sx, oxe), by = rc_bound(fy, sy, oye), bz = rc_bound(fz, sz, oze);
            float pt = 0.0f, pb = 0.0f;     /* the shadow and bound of t, the boundary the last step crossed */
            int la = -1;                    /* that step's axis, -1: none yet (t 0) */
#define T_LAST() (la == 0 ? rc_exact(ix - sx, sx, ox, hid ? idx : 1.0 / dx) : la == 1 ? rc_exact(iy - sy, sy, oy, hid ? idy : 1.0 / dy) : la == 2 ? rc_exact(iz - sz, sz, oz, hid ? idz : 1.0 / dz) : 0.0)
            int found = 0, inw = in_water, cl = cull, ewl = eye_was_liquid, si, ci;
            int c0 = col_of(k, ix, iz);
            si = c0 * 16 + (iy >> 4);
            const struct rcd_sec *cur = c0 >= 0 ? secw(k, si) : NULL;
            ci = SEC_XYZ(ix & 15, iy, iz & 15);
            t = 0.0;
            /* structured (lane/rcsplit): no goto into or out of the loop's body, so a warp's lanes reconverge at
             * every step (with raycast.c's labels they walked one at a time, 1.6 lanes of 32 an instruction); the
             * same steps and decisions in the same order: the step, the top (y), the range's end, a section's
             * boundary (its section, or the jump over an empty one at the next turn), the cell */
            int jump = 0;
#if RC_PF
            int pre = 0, pstx = 0, psty = 0, pid = 0;   /* the next step decided, and its id where it stays in the section */
#endif
            if (cur == NULL)
            {
                /* the current cell's section is empty: the cells after it in it are air */
                if (ewl && up_out) goto general;
                jump = 1;
            }
            for (;;)
            {
                if (jump)
                {
                    RC_STAT(1);
                    jump = 0;
#if RC_PF
                    pre = 0;
#endif
                    inw = 0, cl = -1, ewl = 0;
                    rc_jump(&jr, &ix, &iy, &iz);
                    fx = rc_shadow(ix, sx, oxf, idxf), fy = rc_shadow(iy, sy, oyf, idyf), fz = rc_shadow(iz, sz, ozf, idzf);
                    bx = rc_bound(fx, sx, oxe), by = rc_bound(fy, sy, oye), bz = rc_bound(fz, sz, oze);
                }
                RC_STAT(0);
                int bnd, top = 0;
                /* the step's axis (lane/casthot): each pair's float difference against its bounds' sum, one test a
                 * step for all three pairs and no branch on a decided pair. fl(|fa - fb|) > fl(ea + eb) leaves the exact
                 * |fa - fb| above (1 - 2u) (ea + eb), and each shadow's error is under 2/3 of its bound (rc_bound's
                 * 2 u (3 |f| + |o| |id|) against the 4 u |f| + u |o| |id| a float quotient leaves), so the doubles'
                 * order is the floats'. A pair the test leaves open (or an axis the ray does not move on, an
                 * infinite shadow) takes RC_LE's own comparisons, its doubles where they are open too. */
                int stx, sty;
#define RC_DECIDE(STX, STY)                                                                                                    \
    do {                                                                                                                       \
        const float dxy = fx - fy, dxz = fx - fz, dyz = fy - fz;                                                               \
        if (fabsf(dxy) > bx + by && fabsf(dxz) > bx + bz && fabsf(dyz) > by + bz)                                              \
            STX = dxy < 0.0f && dxz < 0.0f, STY = dyz < 0.0f;                                                                  \
        else                                                                                                                   \
        {                                                                                                                      \
            STX = RC_LE(fx, bx, fy, by, (RC_HID(), rc_exact(ix, sx, ox, idx)), (RC_HID(), rc_exact(iy, sy, oy, idy))) &&      \
                  RC_LE(fx, bx, fz, bz, (RC_HID(), rc_exact(ix, sx, ox, idx)), (RC_HID(), rc_exact(iz, sz, oz, idz)));        \
            STY = !STX && RC_LE(fy, by, fz, bz, (RC_HID(), rc_exact(iy, sy, oy, idy)), (RC_HID(), rc_exact(iz, sz, oz, idz))); \
        }                                                                                                                      \
    } while (0)
#if RC_PF
                if (pre) stx = pstx, sty = psty;
                else
#endif
                RC_DECIDE(stx, sty);
                {
                    /* the step on the chosen axis by selects: a warp's lanes step different axes, and a branch a
                     * step ran each axis' body in turn */
                    const int a = stx ? 0 : sty ? 1 : 2;
                    const int s = a == 0 ? sx : a == 1 ? sy : sz;
                    const int ia = (a == 0 ? ix : a == 1 ? iy : iz) + s;
                    const float fa = a == 0 ? fx : a == 1 ? fy : fz, ba = a == 0 ? bx : a == 1 ? by : bz;
                    const float of = a == 0 ? oxf : a == 1 ? oyf : ozf, idf = a == 0 ? idxf : a == 1 ? idyf : idzf;
                    const float oe = a == 0 ? oxe : a == 1 ? oye : oze;
                    const float nf = rc_shadow(ia, s, of, idf), nb = rc_bound(nf, s, oe);
                    pt = fa, pb = ba, la = a, axis = a;
                    if (a == 0) ix = ia, fx = nf, bx = nb;
                    if (a == 1) iy = ia, fy = nf, by = nb;
                    if (a == 2) iz = ia, fz = nf, bz = nb;
                    top = a == 1 && (unsigned)iy >= ytop;
                    ci = rc_mstep(ci, a == 0 ? RC_MX : a == 1 ? RC_MY : RC_MZ, s);
                    bnd = (ia & 15) == (a == 0 ? ex : a == 1 ? ey : ez);
                }
                const float dtl = pt - tlf;
                if (top || (fabsf(dtl) > pb + tle ? dtl > 0.0f : RC_GE(pt, pb, tlf, tle, T_LAST(), tlim))) break;
                if (bnd)
                {
                    /* into another section */
                    ci = SEC_XYZ(ix & 15, iy, iz & 15);
                    int c = col_of(k, ix, iz);
                    si = c * 16 + (iy >> 4);
                    cur = c >= 0 ? secw(k, si) : NULL;
                    if (cur == NULL)
                    {
                        if (ewl && up_out && axis == 1)
                        {
                            id = 0, meta = 0, found = 1;
                            break;
                        }
                        jump = 1;
                        continue;
                    }
                }
#if RC_PF
                /* the next step's axis and, inside this section, its cell's id, ahead of this cell's tests (the
                 * decision reads only the shadows, which the tests do not change) */
                id = pre && !bnd ? pid : __ldg(&cur->ids[ci]);
                RC_DECIDE(pstx, psty);
                pre = 1;
                {
                    const int nb = pstx ? ((ix + sx) & 15) == ex : psty ? ((iy + sy) & 15) == ey : ((iz + sz) & 15) == ez;
                    const int nci = pstx ? rc_mstep(ci, RC_MX, sx) : psty ? rc_mstep(ci, RC_MY, sy) : rc_mstep(ci, RC_MZ, sz);
                    if (!nb) pid = __ldg(&cur->ids[nci]);
                }
#else
                id = __ldg(&cur->ids[ci]);
#endif
                const int up = ewl && up_out && axis == 1, wet = id == 8 || id == 9;
#ifdef RC_STATS
                if (id == 0)
                {
                    /* the stats build: is the step's 4x4x4 brick all air? */
                    int e = 1;
                    for (int q = 0; q < 64 && e; ++q)
                        e = cur->ids[SEC_XYZ((ix & 12) + (q & 3), (iy & 12) + (q >> 2 & 3), (iz & 12) + (q >> 4))] == 0;
                    if (e) RC_STAT(4);
                }
#endif
                if (id == 0 && !up)
                {
                    inw = 0, cl = -1, ewl = 0;
                    continue;
                }
                /* the eye's own water: seen through */
                if (id != 0 && eye_liquid && wet)
                {
                    cl = -1, ewl = 1;
                    continue;
                }
                /* on through the water the ray is already in (its surface was blended) */
                if (id != 0 && !up && inw && wet)
                {
                    ewl = 0;
                    continue;
                }
                meta = id ? nibble_get(chunk_sec_metas(cur), ci) : 0;
                found = 1;
                break;
            }
            in_water = inw, cull = cl, eye_was_liquid = ewl;
            t = T_LAST();
            g.ix = ix, g.iy = iy, g.iz = iz, gt = 0;
            cs = cur, csx = ix >> 4, csy = iy >> 4, csz = iz >> 4;
            if (found) goto block;
            if (t >= te) goto entity;
            goto miss;
#undef T_LAST
        }
    general:
        RC_STAT(2);
        RC_GT();
        t = dda_step<R, FX>(E, &g, &axis);
        if (t >= te) goto entity;
        if (!(t <= range) || g.iy > 255 || g.iy < 0) goto miss;
        if (fast && g.iy >= k->top && sy >= 0) goto miss;
        if (fast)
        {
            if ((g.ix >> 4) != csx || (g.iy >> 4) != csy || (g.iz >> 4) != csz)
            {
                csx = g.ix >> 4, csy = g.iy >> 4, csz = g.iz >> 4;
                cs = sec_fast(k, g.ix, g.iy, g.iz);
            }
            id = 0;
            if (cs != NULL)
            {
                int ci = SEC_XYZ(g.ix & 15, g.iy, g.iz & 15);
                id = chunk_sec_id(cs, ci);
                if (id) meta = nibble_get(chunk_sec_metas(cs), ci);
            }
        }
        else id = cell_ref(k, g.ix, g.iy, g.iz, &meta);
    block:;
        const int ix = g.ix, iy = g.iy, iz = g.iz;
        /* the cell the ray left */
        const int px = ix - (axis == 0 ? sx : 0), py = iy - (axis == 1 ? sy : 0), pz = iz - (axis == 2 ? sz : 0);
        int face = entry_face(axis, axis == 0 ? sx : axis == 1 ? sy : sz);
        if (eye_was_liquid && eye_liquid == 8 && liquid_of(id) != 8 && axis == 1 && sy > 0 && T.water_spr != 0xffff)
        {
            /* up out of the water the eye is in: the surface from below */
            int sky, block;
            if (fast) light_fast(k, px, py, pz, &sky, &block);
            else light_ref(k, px, py, pz, &sky, &block);
            uint32_t tw = texel(T.water_spr, 1, E->ox + dx * t - RC_CX(E, ix), RC_K(0.0), E->oz + dz * t - RC_CZ(E, iz));
            const float3 tint = tint_of(k, 9, 0, 1, px, py, pz, fast);
            lit(k, (float)(tw & 255), (float)(tw >> 8 & 255), (float)(tw >> 16 & 255), tint, sky, block, 0, SHADE_OF(1), t * fwd, col);
            float a = (float)TEX_A(tw) / 255.0f;
            acc[0] += trans * a * col[0];
            acc[1] += trans * a * col[1];
            acc[2] += trans * a * col[2];
            trans *= 1.0f - a;
        }
        eye_was_liquid = eye_liquid && liquid_of(id) == eye_liquid;
#if RC_KSH
        int kind = KSH[id];
#else
        int kind = __ldg(&T.kind[id]);
#endif
        if (kind == RK_AIR || (eye_liquid && liquid_of(id) == eye_liquid))
        {
            if (kind == RK_AIR) in_water = 0;
            cull = -1;
            if (fast && cs == NULL)
            {
                RC_GT();
                dda_jump<R, FX>(E, &g, 16);
            }
            continue;
        }
        /* the cell's exit, for every kind but the cube */
        R texit = 0;
        if (kind != RK_CUBE)
        {
            RC_GT();
            texit = g.tx < g.ty ? (g.tx < g.tz ? g.tx : g.tz) : (g.ty < g.tz ? g.ty : g.tz);
        }
#if !RC_HX
        R hx = E->ox + dx * t - RC_CX(E, ix), hy = E->oy + dy * t - RC_CY(E, iy), hz = E->oz + dz * t - RC_CZ(E, iz);
#endif
        if (kind == RK_WATER || kind == RK_LAVA)
        {
#if RC_HX
            R hx = E->ox + dx * t - RC_CX(E, ix), hy = E->oy + dy * t - RC_CY(E, iy), hz = E->oz + dz * t - RC_CZ(E, iz);
#endif
            if (kind == RK_WATER && in_water) continue;
            /* the surface: full under the same liquid, else BlockLiquid's
             * height for the level */
            int m2 = 0;
            int above = iy < 255 ? (fast ? cell_fast(k, ix, iy + 1, iz, &m2) : cell_ref(k, ix, iy + 1, iz, &m2)) : 0;
            R h = liquid_of(above) == liquid_of(id) ? RC_K(1.0) : RC_K(1.0) - (R)((meta >= 8 ? 0 : meta) + 1) / RC_K(9.0);
            R th = t;
            int hf = face;
            if (hy > h)
            {
                if (sy >= 0) { cull = -1; continue; }
                th = (RC_CY(E, iy) + h - E->oy) * idy;
                if (th > texit) { cull = -1; continue; }
                hf = 1;
                hx = E->ox + dx * th - RC_CX(E, ix), hy = h, hz = E->oz + dz * th - RC_CZ(E, iz);
            }
            if (th >= te) goto entity;
            int sky, block;
            if (fast) light_fast(k, ix, iy, iz, &sky, &block);
            else light_ref(k, ix, iy, iz, &sky, &block);
            uint16_t spr = kind == RK_WATER ? T.water_spr : T.lava_spr;
            uint32_t tx_ = spr != 0xffff ? texel(spr, hf, hx, hy, hz) : 0xff808080u;
            const float3 tint = tint_of(k, id, meta, hf, ix, iy, iz, fast);
            lit(k, (float)(tx_ & 255), (float)(tx_ >> 8 & 255), (float)(tx_ >> 16 & 255), tint, sky, block, __ldg(&T.emit[id]),
                SHADE_OF(hf), th * fwd, col);
            if (kind == RK_LAVA) goto done;
            float a = (float)TEX_A(tx_) / 255.0f;
            acc[0] += trans * a * col[0];
            acc[1] += trans * a * col[1];
            acc[2] += trans * a * col[2];
            trans *= 1.0f - a;
            in_water = 1, cull = -1;
            continue;
        }
        in_water = 0;
        int key = id | meta << 12;
        const int csame = __ldg(&T.cull_same[id]);
        if (csame && key == cull) continue;
        cull = -1;
        const uint16_t *faces = T.face[k->fancy] + ((size_t)id * 16 + (size_t)meta) * 6;
        if (kind == RK_CUBE)
        {
            uint16_t spr = __ldg(&faces[face]);
#if RC_HX
            /* the hit's two coordinates the face's texel reads (DOWN and UP x, z; NORTH and SOUTH x, y; WEST and EAST
             * z, y), each as raycast.c makes it: o + d t - i */
            const int c1 = face < 4 ? 0 : 2, c2 = face < 2 ? 2 : 1;
            const R v1 = (c1 == 0 ? E->ox : E->oz) + (c1 == 0 ? dx : dz) * t - (c1 == 0 ? RC_CX(E, ix) : RC_CZ(E, iz));
            const R v2 = (c2 == 2 ? E->oz : E->oy) + (c2 == 2 ? dz : dy) * t - (c2 == 2 ? RC_CZ(E, iz) : RC_CY(E, iy));
            const R hx = c1 == 0 ? v1 : RC_K(0.0), hy = c2 == 1 ? v2 : RC_K(0.0), hz = c1 == 2 ? v1 : c2 == 2 ? v2 : RC_K(0.0);
#endif
            uint32_t tt = spr != 0xffff ? texel(spr, face, hx, hy, hz) : 0;
            if (TEX_PASS(tt))
            {
                int sky, block;
                if (fast) light_fast(k, px, py, pz, &sky, &block);
                else light_ref(k, px, py, pz, &sky, &block);
                const float3 tint = tint_of(k, id, meta, face, ix, iy, iz, fast);
                lit(k, (float)(tt & 255), (float)(tt >> 8 & 255), (float)(tt >> 16 & 255), tint, sky, block, __ldg(&T.emit[id]),
                    SHADE_OF(face), t * fwd, col);
                if (k->seen_on) seen_note(k, id, ix, iy, iz, t, pi, pj);
                goto done;
            }
            if (csame) cull = key;
        }
        else if (kind == RK_BOX)
        {
            /* the renderer's bounds (doubles; FP32 each rounded to float, as raycast.c's table) */
            const double *bp = T.bounds + ((size_t)id * 16 + (size_t)meta) * 6;
#define RC_B(i) ((R)bp[i])
            const R o3[3] = {E->ox - RC_CX(E, ix), E->oy - RC_CY(E, iy), E->oz - RC_CZ(E, iz)}, i3[3] = {idx, idy, idz};
            const int s3[3] = {sx, sy, sz};
            R tn = -INFINITY, tf = INFINITY;
            int ax = -1, miss = 0;
            for (int a = 0; a < 3 && !miss; ++a)
            {
                if (!s3[a])
                {
                    if (o3[a] < RC_B(a) || o3[a] > RC_B(a + 3)) miss = 1;
                    continue;
                }
                R t0 = (RC_B(a) - o3[a]) * i3[a], t1 = (RC_B(a + 3) - o3[a]) * i3[a];
                if (t0 > t1) { R s = t0; t0 = t1; t1 = s; }
                if (t0 > tn) tn = t0, ax = a;
                if (t1 < tf) tf = t1;
            }
            if (!miss && ax >= 0 && tn <= tf && tn > RC_K(0.0) && tn <= texit)
            {
                if (tn >= te) goto entity;
                int bf = entry_face(ax, s3[ax]);
                uint16_t spr = faces[bf];
                R bx = E->ox + dx * tn - RC_CX(E, ix), by = E->oy + dy * tn - RC_CY(E, iy), bz = E->oz + dz * tn - RC_CZ(E, iz);
                uint32_t tt = spr != 0xffff ? texel(spr, bf, bx, by, bz) : 0;
                if (TEX_PASS(tt))
                {
                    /* a face on the cell's boundary takes the light of the
                     * cell the ray left; one inside, the cell's own */
                    R bd = RC_B(RC_BI_OF(bf));
#undef RC_B
                    int edge = (bf & 1) ? bd == RC_K(1.0) : bd == RC_K(0.0);
                    int sky, block;
                    int lx = edge ? px : ix, ly = edge ? py : iy, lz = edge ? pz : iz;
                    if (fast) light_fast(k, lx, ly, lz, &sky, &block);
                    else light_ref(k, lx, ly, lz, &sky, &block);
                    const float3 tint = tint_of(k, id, meta, bf, ix, iy, iz, fast);
                    lit(k, (float)(tt & 255), (float)(tt >> 8 & 255), (float)(tt >> 16 & 255), tint, sky, block,
                        __ldg(&T.emit[id]), SHADE_OF(bf), tn * fwd, col);
                    goto done;
                }
            }
        }
        else /* RK_CROSS: the planes x = z and x + z = 1, 0.05 to 0.95 across, the cell's height */
        {
            R lx0 = E->ox - RC_CX(E, ix), lz0 = E->oz - RC_CZ(E, iz), ly0 = E->oy - RC_CY(E, iy);
            R tp[2] = {INFINITY, INFINITY};
            R a1 = dx - dz, a2 = dx + dz;
            if (a1 != RC_K(0.0)) tp[0] = -(lx0 - lz0) / a1;
            if (a2 != RC_K(0.0)) tp[1] = (RC_K(1.0) - lx0 - lz0) / a2;
            if (tp[1] < tp[0]) { R s = tp[0]; tp[0] = tp[1]; tp[1] = s; }
            uint16_t spr = faces[0];
            if (id == 175)
            {
                /* renderBlockDoublePlant: the half's icon by the lower half's variant */
                int v = meta & 7, m2 = 0;
                if (meta & 8)
                {
                    int below = fast ? cell_fast(k, ix, iy - 1, iz, &m2) : cell_ref(k, ix, iy - 1, iz, &m2);
                    v = below == 175 ? m2 & 7 : 6;
                }
                spr = v < 6 ? ((meta & 8) ? T.dp_high[v] : T.dp_low[v]) : 0xffff;
            }
            for (int q = 0; q < 2 && spr != 0xffff; ++q)
            {
                R tq = tp[q];
                if (!(tq >= t && tq <= texit)) continue;
                R qx = lx0 + dx * tq, qy = ly0 + dy * tq;
                if (qx < RC_K(0.05) || qx > RC_K(0.95) || qy < RC_K(0.0) || qy > RC_K(1.0)) continue;
                if (tq >= te) goto entity;
                R u = (qx - RC_K(0.05)) / RC_K(0.9);
                int iu = (int)(u * RC_K(16.0)), iv = (int)((RC_K(1.0) - qy) * RC_K(16.0));
                iu = iu < 0 ? 0 : iu > 15 ? 15 : iu;
                iv = iv < 0 ? 0 : iv > 15 ? 15 : iv;
                uint32_t tt = __ldg(&T.tex[spr][iv * 16 + iu]);
                if (!TEX_PASS(tt)) continue;
                int sky, block;
                if (fast) light_fast(k, ix, iy, iz, &sky, &block);
                else light_ref(k, ix, iy, iz, &sky, &block);
                const float3 tint = tint_of(k, id, meta, 2, ix, iy, iz, fast);
                lit(k, (float)(tt & 255), (float)(tt >> 8 & 255), (float)(tt >> 16 & 255), tint, sky, block, __ldg(&T.emit[id]),
                    1.0f, tq * fwd, col);
                goto done;
            }
        }
    }
entity:
    {
        const struct rcd_ent *en = &X.ent[ent];
        const int lmi = __ldg(&en->lmi);
        lit(k, __ldg(&en->rgb[0]), __ldg(&en->rgb[1]), __ldg(&en->rgb[2]), make_float3(1.0f, 1.0f, 1.0f), lmi >> 4, lmi & 15, 0, SHADE_OF(eface),
            te * fwd, col);
        goto done;
    }
miss:
    if (ent >= 0) goto entity;
    if (k->exp_fog || k->dim == -1) memcpy(col, k->fog, sizeof col);
    else if (k->dim == 1) memcpy(col, T.end_sky, sizeof col);
    else if (dy <= RC_K(0.0)) memcpy(col, k->fog, sizeof col);
    else
    {
        /* the sky plane 16 blocks up, renderSky's fog from 0 to 0.8 of the far plane */
        R z = RC_K(16.0) / dy * fwd;
        float f = (k->sky_end - (float)z) / k->sky_end;
        f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
        col[0] = k->sky[0] * f + k->fog[0] * (1.0f - f);
        col[1] = k->sky[1] * f + k->fog[1] * (1.0f - f);
        col[2] = k->sky[2] * f + k->fog[2] * (1.0f - f);
    }
done:
    {
        uint8_t b[3];
        for (int c = 0; c < 3; ++c)
        {
            float v = acc[c] + trans * col[c];
            if (k->uw) v = v * 0.5f + k->uw_rgb[c] * 0.5f;
            b[c] = (uint8_t)(v >= 255.0f ? 255 : v <= 0.0f ? 0 : (int)(v + 0.5f));
        }
        /* client.channels 1: the luma of the same RGB bytes, BT.601 in 8 bits: (77R + 150G + 29B + 128) >> 8 */
        if (X.ch == 1) out[0] = (uint8_t)((77u * b[0] + 150u * b[1] + 29u * b[2] + 128u) >> 8);
        else out[0] = b[0], out[1] = b[1], out[2] = b[2];
    }
}

/* The FP16 instance two rays a thread (RC_H2 1, lane/cxh2; off by default): the first air loop of two pixels' rays
 * (RC_PAIR) in half2, each lane's arithmetic the scalar walk's (__hadd2_rn, __hsub2_rn, __hmul2_rn round each lane as
 * __hadd_rn, __hsub_rn and __hmul_rn do; the compares by HSET2's masks, ordered as __hle and __hge), so the same bits
 * and no operation of raycast.h's order changed. Both lanes step together: the three compares, the chosen axis' t,
 * its next boundary and t in packed instructions with the masks as selects, and the cells and Morton indices two to
 * a register (walk2's comment); a lane that has ended masked out; a jump over an empty section (rare) by dda_jump on
 * one lane at a time. Where both have ended each goes on in walk from its own end (rs): the hit, the paths past a
 * transparent texel or water, and every later air loop scalar. An eye in lava or outside 0..255 (a whole cast's)
 * walks both scalar. Measured (DEVLOG lane/cxh2): every device cast equal to the host's; the air step about 105
 * warp instructions for two rays where the scalar's is 85 for one, the kernel's instructions the same in all, and the
 * kernel 1.5 to 1.8x the scalar's time at 16x16 to 128x128: two walks a thread one after the other (the hit's shading,
 * the later segments) at 96 registers (41.7% occupancy against 50%), the SMs idle 39% of the kernel's cycles (19%
 * scalar) on the longer tail. So 0. */
#ifndef RC_H2
#define RC_H2 0
#endif
#ifndef RC_MINB2
#define RC_MINB2 10     /* the packed kernel's __launch_bounds__ blocks an SM (64 threads a block; 10: 96 registers, no
                         * spill; 11 and 12 spill) */
#endif
#ifndef RC_PAIR
#define RC_PAIR 1       /* the two pixels of a thread: 0 neighbours in a row (i, i + 1), 1 eight apart (i, i + 8); 1
                         * 3 to 14% faster in one gpu window (lane/cxh2) */
#endif
static __device__ __forceinline__ unsigned h2u(__half2 v)
{
    unsigned u;
    memcpy(&u, &v, 4);
    return u;
}
static __device__ __forceinline__ __half2 u2h(unsigned u)
{
    __half2 v;
    memcpy(&v, &u, 4);
    return v;
}
static __device__ __forceinline__ __half2 hp(h16 a, h16 b) { return __halves2half2(a.v, b.v); }
static __device__ __forceinline__ h16 hl(__half2 v, int L) { return hh(L ? __high2half(v) : __low2half(v)); }
static __device__ __forceinline__ __half2 hset(__half2 v, int L, h16 x)
{
    return L ? __halves2half2(__low2half(v), x.v) : __halves2half2(x.v, __high2half(v));
}
/* m's lanes of a, the others b's; one of three by masks that partition the lanes */
static __device__ __forceinline__ __half2 hsel(__half2 a, __half2 b, unsigned m) { return u2h((h2u(a) & m) | (h2u(b) & ~m)); }
static __device__ __forceinline__ __half2 hsel3(__half2 a, __half2 b, __half2 c, unsigned ma, unsigned mb, unsigned mc)
{
    return u2h((h2u(a) & ma) | (h2u(b) & mb) | (h2u(c) & mc));
}

#ifndef RC_STATS
[[maybe_unused]] static __device__ __forceinline__ void walk2(const struct rch *k, const struct rce<h16> *E, const h16 *d0, const h16 *d1, int p0, int p1,
                                             int w, uint8_t *fb, int ch)
{
    typedef h16 R;
    const int eye_liquid = k->liquid;
#ifndef RC_H2PK
#define RC_H2PK 1       /* (a measurement knob) 0: both lanes scalar, the packed loop never run */
#endif
    const int packed = RC_H2PK && eye_liquid != 10 && (unsigned)k->eye_y <= 255u;
    /* each lane's end, in local memory: written once a lane (an opaque index keeps it out of the registers, which
     * the loop needs: held there, the two records took 30-odd of them through it) */
    struct rc_res<R> rl[2];
    const __half2 hdx = hp(d0[0], d1[0]), hdy = hp(d0[1], d1[1]), hdz = hp(d0[2], d1[2]);
    if (packed)
    {
        /* The integers two to a register too (16-bit lanes, lane 0 low; SWAR): the cell as x and z from the eye's
         * cell and y absolute, each plus B (so a lane never borrows from the other), and the cell's Morton index in
         * its section. A step adds the chosen axis' sign in the lanes that take it; a step out of the section is the
         * Morton index's carry (a + step: the axis' bits all ones before) or borrow (a - step: all zero), the same
         * test as walk's (ia & 15) == e, and the carried index is SEC_XYZ of the new cell; the top is y's step to
         * ytop or below 0 by a lane compare. A step where neither lane meets a section's edge, the top or its limit
         * and both cells are air (the most of them) has no branch but the loop's. */
        const int B = 0x4000;
        const unsigned H = 0x80008000u, ONE = 0x00010001u, LO = 0xffffu, HI = 0xffff0000u;
        int act[2], jump[2], inw[2], cl[2], ewl[2], uo[2];
        const struct rcd_sec *cur[2];
        const int c0 = col_of(k, k->eye_x, k->eye_z);
        const struct rcd_sec *cur0 = c0 >= 0 ? secw(k, c0 * 16 + (k->eye_y >> 4)) : NULL;
        unsigned DXP = 0, DXN = 0, DYP = 0, DYN = 0, DZP = 0, DZN = 0, TOPB = 0;
        __half2 hidx, hidy, hidz;
#pragma unroll
        for (int L = 0; L < 2; ++L)
        {
            int Lo = L;
            asm volatile("" : "+r"(Lo));
            const R dx = L ? d1[0] : d0[0], dy = L ? d1[1] : d0[1], dz = L ? d1[2] : d0[2];
            const int p = L ? p1 : p0, pi = p % w, pj = p / w;
            const int sx = rc_sign(dx), sy = rc_sign(dy), sz = rc_sign(dz);
            const R idx = sx ? RC_K(1.0) / dx : RC_K(0.0), idy = sy ? RC_K(1.0) / dy : RC_K(0.0), idz = sz ? RC_K(1.0) / dz : RC_K(0.0);
            if (L) hidx = hp(hl(hidx, 0), idx), hidy = hp(hl(hidy, 0), idy), hidz = hp(hl(hidz, 0), idz);
            else hidx = hp(idx, idx), hidy = hp(idy, idy), hidz = hp(idz, idz);
            DXP |= (unsigned)(sx > 0) << 16 * L, DXN |= (unsigned)(sx < 0) << 16 * L;
            DYP |= (unsigned)(sy > 0) << 16 * L, DYN |= (unsigned)(sy < 0) << 16 * L;
            DZP |= (unsigned)(sz > 0) << 16 * L, DZN |= (unsigned)(sz < 0) << 16 * L;
            TOPB |= (unsigned)((sy >= 0 ? k->top : 256) + B) << 16 * L;
            /* walk's nearest entity box */
            R te = INFINITY;
            int ent = -1, eface = 1;
            for (int e = 0; e < k->nent; ++e)
            {
                const struct rcd_ent *en = &X.ent[e];
                const int2 ri = __ldg((const int2 *)&en->i0), rj = __ldg((const int2 *)&en->j0);
                if (pi < ri.x || pi > ri.y || pj < rj.x || pj > rj.y) continue;
                R tn = -INFINITY, tf = INFINITY;
                int ax = -1;
                const R o3[3] = {E->ox, E->oy, E->oz}, i3[3] = {idx, idy, idz};
                const int s3[3] = {sx, sy, sz};
                int miss = 0;
                for (int a = 0; a < 3 && !miss; ++a)
                {
                    const R b0 = (R)__ldg(&en->box[a]), b1 = (R)__ldg(&en->box[a + 3]);
                    if (!s3[a])
                    {
                        if (o3[a] < b0 || o3[a] > b1) miss = 1;
                        continue;
                    }
                    R t0 = (b0 - o3[a]) * i3[a], t1 = (b1 - o3[a]) * i3[a];
                    if (t0 > t1) { R s = t0; t0 = t1; t1 = s; }
                    if (t0 > tn) tn = t0, ax = a;
                    if (t1 < tf) tf = t1;
                }
                if (miss || ax < 0 || tn > tf || tn <= RC_K(0.0) || tn >= te) continue;
                te = tn, ent = e, eface = entry_face(ax, s3[ax]);
            }
            rl[Lo].te = te, rl[Lo].ent = ent, rl[Lo].eface = eface;
            rl[Lo].t = te < E->rnext ? te : E->rnext;   /* (the limit, until the lane's end writes its t) */
            /* walk's state at its air loop */
            inw[L] = 0, cl[L] = -1, ewl[L] = eye_liquid != 0;
            uo[L] = eye_liquid == 8 && sy > 0;
            act[L] = 1, jump[L] = 0, cur[L] = cur0;
            rl[Lo].inw = 0, rl[Lo].cl = -1, rl[Lo].ewl = ewl[L], rl[Lo].kind = 2;
            if (k->eye_y >= k->top && sy >= 0) act[L] = 0;   /* (walk's own test before its loop: the miss) */
            else if (cur0 == NULL)
            {
                if (ewl[L] && uo[L]) act[L] = 0, rl[Lo].kind = 3;
                else jump[L] = 1;
            }
        }
        const __half2 lim = hp(rl[0].t, rl[1].t);
        /* the packed state: each axis' next boundary u, its t, the step's sign, the eye */
        const unsigned SXP = DXP * LO, SYP = DYP * LO, SZP = DZP * LO;
        const unsigned mvx = (DXP | DXN) * LO, mvy = (DYP | DYN) * LO, mvz = (DZP | DZN) * LO;   /* the lanes moving */
        const __half2 one2 = hp(RC_K(1.0), RC_K(1.0)), zero2 = hp(RC_K(0.0), RC_K(0.0));
        /* (s as a half: 1, -1 or 0 a lane, (R)s exactly) */
        const __half2 fsx = hsel(one2, hsel(__hneg2(one2), zero2, (DXN * LO)), SXP);
        const __half2 fsy = hsel(one2, hsel(__hneg2(one2), zero2, (DYN * LO)), SYP);
        const __half2 fsz = hsel(one2, hsel(__hneg2(one2), zero2, (DZN * LO)), SZP);
        const __half2 ox = hp(E->ox, E->ox), oy = hp(E->oy, E->oy), oz = hp(E->oz, E->oz);
        const __half2 inf2 = hp(R(INFINITY), R(INFINITY));
        __half2 ux = hp(RC_CX(E, k->eye_x + (int)(DXP & 1)), RC_CX(E, k->eye_x + (int)(DXP >> 16)));
        __half2 uy = hp(RC_CY(E, k->eye_y + (int)(DYP & 1)), RC_CY(E, k->eye_y + (int)(DYP >> 16)));
        __half2 uz = hp(RC_CZ(E, k->eye_z + (int)(DZP & 1)), RC_CZ(E, k->eye_z + (int)(DZP >> 16)));
        __half2 tx = hsel(__hmul2_rn(__hsub2_rn(ux, ox), hidx), inf2, mvx);
        __half2 ty = hsel(__hmul2_rn(__hsub2_rn(uy, oy), hidy), inf2, mvy);
        __half2 tz = hsel(__hmul2_rn(__hsub2_rn(uz, oz), hidz), inf2, mvz);
        unsigned rx = (unsigned)B * ONE, ry = (unsigned)(k->eye_y + B) * ONE, rz = (unsigned)B * ONE;
        unsigned ci = (unsigned)SEC_XYZ(k->eye_x & 15, k->eye_y, k->eye_z & 15) * ONE;
        for (;;)
        {
            /* a lane's jump over the empty section it is in (walk's at its loop's top), one lane at a time */
            for (unsigned jm = (unsigned)jump[0] | (unsigned)jump[1] << 1; jm;)
            {
                const int L = jm & 1 ? 0 : 1, sh = 16 * L;
                jm &= jm - 1;
                struct dda<R> j;
                j.ix = (int)(rx >> sh & LO) - B + k->eye_x, j.iy = (int)(ry >> sh & LO) - B, j.iz = (int)(rz >> sh & LO) - B + k->eye_z;
                j.sx = (int)(DXP >> sh & 1) - (int)(DXN >> sh & 1), j.sy = (int)(DYP >> sh & 1) - (int)(DYN >> sh & 1);
                j.sz = (int)(DZP >> sh & 1) - (int)(DZN >> sh & 1);
                j.idx = hl(hidx, L), j.idy = hl(hidy, L), j.idz = hl(hidz, L);
                j.tx = hl(tx, L), j.ty = hl(ty, L), j.tz = hl(tz, L);
                j.dx = hl(hdx, L), j.dy = hl(hdy, L), j.dz = hl(hdz, L);
                dda_jump<R, false>(E, &j, 16);
                tx = hset(tx, L, j.tx), ty = hset(ty, L, j.ty), tz = hset(tz, L, j.tz);
                ux = hset(ux, L, RC_CX(E, j.ix + (j.sx > 0))), uy = hset(uy, L, RC_CY(E, j.iy + (j.sy > 0)));
                uz = hset(uz, L, RC_CZ(E, j.iz + (j.sz > 0)));
                const unsigned keep = ~(LO << sh);
                rx = (rx & keep) | (unsigned)(j.ix - k->eye_x + B) << sh;
                ry = (ry & keep) | (unsigned)(j.iy + B) << sh;
                rz = (rz & keep) | (unsigned)(j.iz - k->eye_z + B) << sh;
                ci = (ci & keep) | (unsigned)SEC_XYZ(j.ix & 15, j.iy, j.iz & 15) << sh;
                if (L) jump[1] = 0, inw[1] = 0, cl[1] = -1, ewl[1] = 0;
                else jump[0] = 0, inw[0] = 0, cl[0] = -1, ewl[0] = 0;
            }
            if (!(act[0] | act[1])) break;
            /* the step of both lanes: the axis (ties x, then y, then z), its t, its next boundary and t */
            const unsigned am = (act[0] ? LO : 0u) | (act[1] ? HI : 0u);
            const unsigned lxy = __hle2_mask(tx, ty), lxz = __hle2_mask(tx, tz), lyz = __hle2_mask(ty, tz);
            const unsigned stx = lxy & lxz & am, sty = ~(lxy & lxz) & lyz & am, stz = ~(lxy & lxz) & ~lyz & am;
            const __half2 ta = hsel3(tx, ty, tz, stx, sty, stz);
            const __half2 nu = __hadd2_rn(hsel3(ux, uy, uz, stx, sty, stz), hsel3(fsx, fsy, fsz, stx, sty, stz));
            const __half2 nt = __hmul2_rn(__hsub2_rn(nu, hsel3(ox, oy, oz, stx, sty, stz)), hsel3(hidx, hidy, hidz, stx, sty, stz));
            tx = hsel(nt, tx, stx), ux = hsel(nu, ux, stx);
            ty = hsel(nt, ty, sty), uy = hsel(nu, uy, sty);
            tz = hsel(nt, tz, stz), uz = hsel(nu, uz, stz);
            const unsigned past = __hge2_mask(ta, lim) & am;
            /* the cell, the Morton index, the section's edge and the top, both lanes at once */
            rx = rx + (stx & DXP) - (stx & DXN);
            ry = ry + (sty & DYP) - (sty & DYN);
            rz = rz + (stz & DZP) - (stz & DZN);
            const unsigned Mq = (stx & 0x02490249u) | (sty & 0x09240924u) | (stz & 0x04920492u);
            const unsigned P = (stx & SXP) | (sty & SYP) | (stz & SZP);
            const unsigned pr = (ci | (~Mq & 0x0fff0fffu)) + ONE, mr = ((ci & Mq) | 0x10001000u) - ONE;
            const unsigned bnd = ((pr & P) | (~mr & ~P)) & 0x10001000u & am;
            ci = (((pr & P) | (mr & ~P)) & Mq) | (ci & ~Mq);
            const unsigned top = (((ry | H) - TOPB) | ~((ry | H) - (unsigned)B * ONE)) & sty & H;
            int idv0 = 0, idv1 = 0, hv = 0;
            if (!(bnd | top | past) && !(ewl[0] & act[0]) && !(ewl[1] & act[1]))
            {
                if (act[0]) idv0 = __ldg(&cur[0]->ids[ci & 0xfffu]);
                if (act[1]) idv1 = __ldg(&cur[1]->ids[ci >> 16]);
                if (!(idv0 | idv1))
                {
                    /* both in air (walk's id 0, not up): the lanes' in_water, cull, eye_was_liquid */
                    inw[0] = inw[1] = 0, cl[0] = cl[1] = -1;
                    continue;
                }
                hv = 1;
            }
            /* each lane's events as walk's, in its order: the top or the limit, a section's edge, the cell */
#pragma unroll
            for (int L = 0; L < 2; ++L)
            {
                if (!act[L]) continue;
                const int sh = 16 * L;
                const int a = (stx >> sh) & 1 ? 0 : (sty >> sh) & 1 ? 1 : 2;
                const int ix = (int)(rx >> sh & LO) - B + k->eye_x, iy = (int)(ry >> sh & LO) - B, iz = (int)(rz >> sh & LO) - B + k->eye_z;
                const int cl_i = (int)(ci >> sh & 0xfffu);
                int id = 0, meta = 0, found = 0;
                if (!((top >> (sh + 15) & 1) | (past >> sh & 1)))
                {
                    if (bnd >> (sh + 12) & 1)
                    {
                        /* into another section */
                        const int c = col_of(k, ix, iz);
                        cur[L] = c >= 0 ? secw(k, c * 16 + (iy >> 4)) : NULL;
                        if (cur[L] == NULL)
                        {
                            if (ewl[L] && uo[L] && a == 1) found = 1;
                            else
                            {
                                jump[L] = 1;
                                continue;
                            }
                        }
                    }
                    if (!found)
                    {
                        const int c_id = hv && !(bnd >> (sh + 12) & 1) ? (L ? idv1 : idv0) : __ldg(&cur[L]->ids[cl_i]);
                        const int up = ewl[L] && uo[L] && a == 1, wet = c_id == 8 || c_id == 9;
                        if (c_id == 0 && !up)
                        {
                            inw[L] = 0, cl[L] = -1, ewl[L] = 0;
                            continue;
                        }
                        if (c_id != 0 && eye_liquid && wet)
                        {
                            cl[L] = -1, ewl[L] = 1;
                            continue;
                        }
                        if (c_id != 0 && !up && inw[L] && wet)
                        {
                            ewl[L] = 0;
                            continue;
                        }
                        id = c_id, meta = c_id ? nibble_get(chunk_sec_metas(cur[L]), cl_i) : 0, found = 1;
                    }
                }
                /* the lane's air loop ends here: walk's state as its loop would leave it */
                int Lo = L;
                asm volatile("" : "+r"(Lo));
                rl[Lo].ix = ix, rl[Lo].iy = iy, rl[Lo].iz = iz, rl[Lo].axis = a, rl[Lo].id = id, rl[Lo].meta = meta;
                rl[Lo].inw = inw[L], rl[Lo].cl = cl[L], rl[Lo].ewl = ewl[L], rl[Lo].cur = cur[L];
                rl[Lo].tx = hl(tx, L), rl[Lo].ty = hl(ty, L), rl[Lo].tz = hl(tz, L), rl[Lo].t = hl(ta, L);
                rl[Lo].kind = found ? 0 : 2;
                act[L] = 0;
            }
        }
    }
    /* each lane on from its end, one after the other (one copy of walk) */
#pragma unroll 1
    for (int L = 0; L < 2; ++L)
    {
        const R d[3] = {hl(hdx, L), hl(hdy, L), hl(hdz, L)};
        const int p = L ? p1 : p0;
        walk<R, false>(k, E, d, p % w, p / w, 1, packed ? &rl[L] : NULL, fb + (size_t)p * ch);
    }
}
#endif

/* ------------------------------------------------------------- the kernels */

/* the staged records (rcmirror.c) into their envs' rings: one block a record */
__global__ void rc_scatter(const unsigned char *stage, size_t cap, const int2 *refs, struct rcd_col *cols,
                           struct rcd_sec *secs, int R)
{
    int2 r = refs[blockIdx.x];
    const unsigned char *p = stage + (size_t)r.x * cap + (size_t)r.y * RCD_REC_BYTES;
    const struct rcd_rec *h = (const struct rcd_rec *)p;
    const uint4 *src = (const uint4 *)(p + sizeof(struct rcd_rec));
    if (h->kind == RCD_REC_COL)
    {
        uint4 *dst = (uint4 *)(cols + (size_t)h->env * R * R + h->col);
        for (int i = threadIdx.x; i < (int)(sizeof(struct rcd_col) / 16); i += blockDim.x) dst[i] = src[i];
        return;
    }
    /* a section: its bytes read once in order (the stage is the worker's, over the bus) into shared memory, then
     * written in Morton order (SEC_XYZ), 16 bytes a store */
    __shared__ __align__(16) struct rcd_sec S;
    uint4 *sp = (uint4 *)&S;
    for (int i = threadIdx.x; i < (int)(sizeof S / 16); i += blockDim.x) sp[i] = src[i];
    __syncthreads();
    uint4 *dst = (uint4 *)(secs + ((size_t)h->env * R * R + h->col) * 16 + h->sec);
    for (int i = threadIdx.x; i < (int)(sizeof S / 16); i += blockDim.x)
    {
        uint32_t wd[4];
        const int o = i * 16;
        if (o < RCD_CELLS)
            for (int q = 0; q < 4; ++q)
            {
                const int m = o + q * 4;
                wd[q] = (uint32_t)S.ids[rc_canon(m)] | (uint32_t)S.ids[rc_canon(m + 1)] << 8 |
                        (uint32_t)S.ids[rc_canon(m + 2)] << 16 | (uint32_t)S.ids[rc_canon(m + 3)] << 24;
            }
        else
        {
            /* a nibble array (metas, sky, block): byte b holds cells 2b and 2b + 1 */
            const int a = (o - RCD_CELLS) / (RCD_CELLS / 2), b0 = (o - RCD_CELLS) % (RCD_CELLS / 2);
            const uint8_t *nb = a == 0 ? S.metas : a == 1 ? S.sky : S.block;
            for (int q = 0; q < 4; ++q)
            {
                uint32_t v = 0;
                for (int c = 0; c < 8; ++c)
                {
                    const int m = 2 * (b0 + q * 4) + c, ci = rc_canon(m);
                    v |= (uint32_t)(nb[ci >> 1] >> ((ci & 1) << 2) & 15) << (4 * c);
                }
                wd[q] = v;
            }
        }
        dst[i] = make_uint4(wd[0], wd[1], wd[2], wd[3]);
    }
}

/* a section between world.h's order and the device's (rcbench's input, rcdev_dump's output): to_dev 1 into Morton
 * order, 0 back */
extern "C" void rc_sec_order(struct rcd_sec *s, int to_dev)
{
    const struct rcd_sec t = *s;
    for (int m = 0; m < RCD_CELLS; ++m)
    {
        const int c = rc_canon(m), from = to_dev ? c : m, to = to_dev ? m : c;
        s->ids[to] = t.ids[from];
        uint8_t *dn[3] = {s->metas, s->sky, s->block};
        const uint8_t *sn[3] = {t.metas, t.sky, t.block};
        for (int a = 0; a < 3; ++a)
        {
            const int v = sn[a][from >> 1] >> ((from & 1) << 2) & 15;
            dn[a][to >> 1] = (uint8_t)((dn[a][to >> 1] & ~(15 << ((to & 1) << 2))) | v << ((to & 1) << 2));
        }
    }
}

__global__ void rc_seen_clear(const int *ids, int k, unsigned long long *key, int *n)
{
    int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= k * RCD_SEEN) return;
    int e = ids[t / RCD_SEEN], q = t % RCD_SEEN;
    key[e * RCD_SEEN + q] = ~0ull;
    n[e * RCD_SEEN + q] = 0;
}

/* 1 / n for a power of two n, exactly (a power of two's quotient is its reciprocal's product: the 3090's double
 * division is a long sequence; the trainer's sizes are powers of two) */
static __device__ __forceinline__ void rc_pow2inv(int n, double *r) { *r = __longlong_as_double((long long)(1023 - (31 - __clz(n))) << 52); }
static __device__ __forceinline__ void rc_pow2inv(int n, float *r) { *r = __int_as_float((127 - (31 - __clz(n))) << 23); }
/* the block's eye in each instance's scalar (made once a block from the head) */
__shared__ struct rce<double> ED;
__shared__ struct rce<float> EF;
static __device__ __forceinline__ struct rce<double> *rc_e(double) { return &ED; }
static __device__ __forceinline__ struct rce<float> *rc_e(float) { return &EF; }
static __device__ __forceinline__ double rc_sqrt(double x) { return sqrt(x); }
static __device__ __forceinline__ float rc_sqrt(float x) { return sqrtf(x); }
/* the direction tables of the instance (DA, DB in double; DAf, DBf in float) */
static __device__ __forceinline__ double (*rc_da(double))[3] { return DA; }
static __device__ __forceinline__ double (*rc_db(double))[3] { return DB; }
static __device__ __forceinline__ float (*rc_da(float))[3] { return DAf; }
static __device__ __forceinline__ float (*rc_db(float))[3] { return DBf; }
/* (FP16's: its eye, sqrt, tables, 1 / n) */
__shared__ struct rce<h16> EH;
static __device__ __forceinline__ struct rce<h16> *rc_e(h16) { return &EH; }
static __device__ __forceinline__ h16 rc_sqrt(h16 x) { return h16(sqrtf(hf(x))); }
static __device__ __forceinline__ h16 (*rc_da(h16))[3] { return DAh; }
static __device__ __forceinline__ h16 (*rc_db(h16))[3] { return DBh; }
static __device__ __forceinline__ void rc_pow2inv(int n, h16 *r) { float f; rc_pow2inv(n, &f); *r = h16(f); }

/* raycast.c cast()'s ray through pixel (i, j)'s centre, unit, in the instance's scalar (the tables where the block made
 * them: rows j0 to j0 + nj) */
template <typename Q, bool FX>
static __device__ __forceinline__ void rc_dir(const struct rch *k, Q (*da)[3], Q (*db)[3], int tab, int j0, int w, int h, int i, int j,
                                              Q *d)
{
    const Q tanh_ = (Q)RC_TAN_HALF, one = 1, two = 2, half = 0.5f;
    Q dx, dy, dz;
    if (tab)
        dx = da[i][0] + db[j - j0][0], dy = da[i][1] + db[j - j0][1], dz = da[i][2] + db[j - j0][2];
    else
    {
        const Q sxs = tanh_ * (Q)k->aspect;
        Q ih, iw;
        rc_pow2inv(h, &ih);
        rc_pow2inv(w, &iw);
        Q sy = (one - ((h & (h - 1)) ? two * ((Q)j + half) / (Q)h : two * ((Q)j + half) * ih)) * tanh_;
        Q sx = (((w & (w - 1)) ? two * ((Q)i + half) / (Q)w : two * ((Q)i + half) * iw) - one) * sxs;
        dx = (Q)k->fx + sx * (Q)k->rx + sy * (Q)k->ux, dy = (Q)k->fy + sx * (Q)k->ry + sy * (Q)k->uy,
        dz = (Q)k->fz + sx * (Q)k->rz + sy * (Q)k->uz;
    }
    Q inv = one / rc_sqrt(dx * dx + dy * dy + dz * dz);
    d[0] = dx * inv, d[1] = dy * inv, d[2] = dz * inv;
    if constexpr (sizeof(Q) == 2)
    {
        /* (FP16, lane/cxf16, raycast.c rays_row's: a component whose reciprocal overflows a half is 0) */
        for (int c = 0; c < 3; ++c) d[c] = fabsf(hf(Q(1.0f) / d[c])) <= 65504.0f ? d[c] : Q(0.0f);
    }
    if constexpr (FX)
    {
        /* the fixed instance: the direction on RC_FIXG fractional bits (raycast.c rc_fxqf: rint(d 2^G) 2^-G) */
        const float q = (float)(1 << RC_FIXG), qi = 1.0f / (float)(1 << RC_FIXG);
        for (int c = 0; c < 3; ++c) d[c] = rintf(d[c] * q) * qi;
    }
}

/* one thread a pixel, a block's pixels of one env (blockIdx.y: its index in ids); Q the instance's scalar (raycast.h
 * "The two instances": double the reference, float production); P2 (FP16's RC_H2): two pixels a thread, a block of
 * RC_BS / 2 threads the same RC_BS pixels */
template <typename Q, bool FX = false, bool P2 = false>
__global__ void __launch_bounds__(P2 ? RC_BS / 2 : RC_BS, P2 ? RC_MINB2 : sizeof(Q) == 4 ? RC_MINB32 : sizeof(Q) == 2 ? RC_MINB16 : RC_MINB) rc_cast(const struct rcd_head *heads, const struct rcd_col *cols,
                                               const struct rcd_sec *secs, const int *ids, int R, int w, int h,int ch,
                                               uint8_t *frames, unsigned long long *seen_key, int *seen_n, int *seen_xyz)
{
    const int env = ids[blockIdx.y];
    {
        /* the head but its lightmap and boxes (struct rch), a word a thread */
        const int *src = (const int *)(heads + env);
        int *dst = (int *)&H;
        const int na = (int)(RCH_A / 4), nb = (int)((RCH_E - RCH_B) / 4);
        for (int i = threadIdx.x; i < na + nb; i += blockDim.x) dst[i] = __ldg(&src[i < na ? i : i - na + (int)(RCH_B / 4)]);
    }
    if (threadIdx.x == 0)
    {
        X.lm = heads[env].lm;
        X.ent = heads[env].ent;
        X.cols = cols + (size_t)env * R * R;
        X.secs = secs + (size_t)env * R * R * 16;
        X.R = R, X.w = w;
        X.ch = ch;
        X.seen_key = seen_key + (size_t)env * RCD_SEEN;
        X.seen_n = seen_n + (size_t)env * RCD_SEEN;
        X.seen_xyz = seen_xyz + (size_t)env * w * h * 3;
    }
    __syncthreads();
    struct rce<Q> *const E = rc_e(Q(0));
    if (threadIdx.x == 0)
    {
        /* the eye in the instance's scalar: FP64 the head's doubles and origin 0; FP32 the head's floats (widened
         * exactly by raycast_head_f32: the eye's offset in its cell) and the eye's cell as the origin */
        constexpr bool F32 = sizeof(Q) < 8;   /* (FP16's too: the eye's offset in its cell) */
        E->ox = (Q)H.ox, E->oy = (Q)H.oy, E->oz = (Q)H.oz;
        E->fx = (Q)H.fx, E->fy = (Q)H.fy, E->fz = (Q)H.fz, E->rnext = (Q)H.rnext;
        E->rx = (Q)H.rx, E->ry = (Q)H.ry, E->rz = (Q)H.rz, E->ux = (Q)H.ux, E->uy = (Q)H.uy, E->uz = (Q)H.uz;
        E->aspect = (Q)H.aspect;
        E->bx = F32 ? H.eye_x : 0, E->by = F32 ? H.eye_y : 0, E->bz = F32 ? H.eye_z : 0;
    }
    if constexpr (sizeof(Q) < 8) __syncthreads();   /* (FP32's and FP16's direction tables read E: made once a block) */
    if (sizeof(Q) == 8 && threadIdx.x < 3)
    {
        const double o = threadIdx.x == 0 ? H.ox : threadIdx.x == 1 ? H.oy : H.oz;
        const int a = threadIdx.x;
        X.ofl[a] = (float)o;
        X.oab[a] = fabs(o) < 4e6 ? fabsf((float)o) : INFINITY;
        X.oi[a] = (int)floor(o);
        X.ofr[a] = (float)(o - (double)X.oi[a]);
    }
    for (int c = threadIdx.x; c < H.ncx * H.ncz; c += blockDim.x)
    {
        int cx = H.cx0 + c / H.ncz, cz = H.cz0 + c % H.ncz, ri = ring_index(R, cx, cz);
        /* the column's first 16 bytes in one load: cx, cz, exists, mask | present << 16 */
        const int4 C = __ldg((const int4 *)(X.cols + ri));
        int ok = C.z && C.x == cx && C.y == cz;
        WCOL[c] = (int16_t)(ok ? ri : -1);
        WPRES[c] = ok ? (uint16_t)((unsigned)C.w >> 16) : 0;
        WMASK[c] = ok ? (uint16_t)((unsigned)C.w & 0xffffu) : 0;
    }
#if RC_KSH
    for (int c = threadIdx.x; c < 4096 / 16; c += blockDim.x) ((uint4 *)KSH)[c] = __ldg((const uint4 *)T.kind + c);
#endif
    /* the pixel direction's two halves, f + sx r by column and sy u by the block's rows (raycast.c's (f + sx r) + sy u
     * in that order): a ray's direction one addition a component where it was a chain of eight double operations */
    /* RC_TILE: a block of 128 a 16 x 8 tile of the frame, its warps 8 x 4 (frames of 16 and 8 a multiple); else rows */
    const int npx = P2 ? 2 * (int)blockDim.x : (int)blockDim.x;   /* the block's pixels */
    const int tile = RC_TILE && npx == 128 && w % 16 == 0 && h % 8 == 0;
    const int j0 = tile ? (int)(blockIdx.x / (w / 16)) * 8 : (int)blockIdx.x * npx / w,
              nj = tile ? 8 : ((int)blockIdx.x * npx + npx - 1) / w - j0 + 1;
    Q (*const da)[3] = rc_da(Q(0)), (*const db)[3] = rc_db(Q(0));
    const Q tanh_ = (Q)RC_TAN_HALF, one = 1, two = 2, half = 0.5f;
    /* the camera in the instance's scalar: FP64 the head's own, FP32 E's floats (each the head's value exactly) */
    constexpr bool F32 = sizeof(Q) < 8;
    const Q cfx = F32 ? E->fx : (Q)H.fx, cfy = F32 ? E->fy : (Q)H.fy, cfz = F32 ? E->fz : (Q)H.fz;
    const Q crx = F32 ? E->rx : (Q)H.rx, cry = F32 ? E->ry : (Q)H.ry, crz = F32 ? E->rz : (Q)H.rz;
    const Q cux = F32 ? E->ux : (Q)H.ux, cuy = F32 ? E->uy : (Q)H.uy, cuz = F32 ? E->uz : (Q)H.uz;
    const Q casp = F32 ? E->aspect : (Q)H.aspect;
    if (w <= RC_DT && nj <= RC_DT)
        for (int c = threadIdx.x; c < w + nj; c += blockDim.x)
        {
            if (c < w)
            {
                Q iw;
                rc_pow2inv(w, &iw);
                const Q sxs = tanh_ * casp;
                const Q sx = (((w & (w - 1)) ? two * ((Q)c + half) / (Q)w : two * ((Q)c + half) * iw) - one) * sxs;
                da[c][0] = cfx + sx * crx, da[c][1] = cfy + sx * cry, da[c][2] = cfz + sx * crz;
            }
            else
            {
                Q ih;
                rc_pow2inv(h, &ih);
                const int jj = j0 + c - w;
                const Q sy = (one - ((h & (h - 1)) ? two * ((Q)jj + half) / (Q)h : two * ((Q)jj + half) * ih)) * tanh_;
                db[c - w][0] = sy * cux, db[c - w][1] = sy * cuy, db[c - w][2] = sy * cuz;
            }
        }
    __syncthreads();
    const int tab = w <= RC_DT && nj <= RC_DT;
    const struct rch *k = &H;
#ifndef RC_STATS
    if constexpr (P2)
    {
        /* two pixels a thread (RC_PAIR 0: (i, i + 1) of a row, the warp 16 x 4 pixels; 1: (i, i + 8)), the tile's rows
         * by the thread's high bits */
        const int t = (int)threadIdx.x;
        const int p0 = tile ? (j0 + (t >> 3)) * w + (int)(blockIdx.x % (w / 16)) * 16 + (RC_PAIR ? (t & 7) : (t & 7) * 2)
                            : (int)blockIdx.x * npx + 2 * t;
        const int p1 = p0 + (tile && RC_PAIR ? 8 : 1);
        if (p0 >= w * h) return;
        const int both = p1 < w * h;
        uint8_t *o0 = frames + ((size_t)env * w * h + p0) * ch, *o1 = frames + ((size_t)env * w * h + p1) * ch;
        if (!H.ok)
        {
            o0[0] = 0;
            if (ch == 3) o0[1] = o0[2] = 0;
            if (both)
            {
                o1[0] = 0;
                if (ch == 3) o1[1] = o1[2] = 0;
            }
            return;
        }
        Q d0[3], d1[3];
        rc_dir<Q, FX>(k, da, db, tab, j0, w, h, p0 % w, p0 / w, d0);
        if (both)
        {
            rc_dir<Q, FX>(k, da, db, tab, j0, w, h, p1 % w, p1 / w, d1);
            if constexpr (sizeof(Q) == 2) walk2(k, E, d0, d1, p0, p1, w, frames + (size_t)env * w * h * ch, ch);
        }
        else walk<Q, FX>(k, E, d0, p0 % w, p0 / w, 1, NULL, o0);
        return;
    }
#endif
    const int p = tile ? (j0 + (int)(threadIdx.x >> 6) * 4 + (int)(threadIdx.x >> 3 & 3)) * w + (int)(blockIdx.x % (w / 16)) * 16 +
                             (int)(threadIdx.x >> 5 & 1) * 8 + (int)(threadIdx.x & 7)
                       : (int)(blockIdx.x * blockDim.x + threadIdx.x);
    if (p >= w * h) return;
    const int i = p % w, j = p / w;
    uint8_t *o = frames + ((size_t)env * w * h + p) * ch;
    if (!H.ok)
    {
        o[0] = 0;
        if (ch == 3) o[1] = o[2] = 0;
        return;
    }
    /* raycast.c cast()'s ray through the pixel's centre */
    Q d[3];
    rc_dir<Q, FX>(k, da, db, tab, j0, w, h, i, j, d);
#ifdef RC_STATS
    unsigned st[5] = {0, 0, 0, 0, 0};
#endif
    walk<Q, FX>(k, E, d, i, j, 1, NULL, o RC_STATS_ARG);
#ifdef RC_STATS
    for (int c = 0; c < 5; ++c) rc_stats[((size_t)blockIdx.y * w * h + p) * 5 + c] = st[c];
#endif
}

/* the seen records (raycast.h struct raycast_seen) from the pixels' notes */
__global__ void rc_seen_fin(const int *ids, int k, const unsigned long long *key, const int *n, const int *xyz, int npix,
                            struct rcd_seen *out)
{
    int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= k * RCD_SEEN) return;
    int e = ids[t / RCD_SEEN], q = t % RCD_SEEN;
    struct rcd_seen *o = out + e;
    int c = n[e * RCD_SEEN + q];
    o->n[q] = c;
    if (c > 0)
    {
        unsigned long long kk = key[e * RCD_SEEN + q];
        int p = (int)(kk & 0xffffffffu);
        o->d[q] = __uint_as_float((unsigned)(kk >> 32));
        const int *v = xyz + ((size_t)e * npix + p) * 3;
        o->x[q] = v[0], o->y[q] = v[1], o->z[q] = v[2];
    }
    else
    {
        o->d[q] = -1.0f;
        o->x[q] = o->y[q] = o->z[q] = 0;
    }
}

/* ---------------------------------------------- the host's calls (host.cu) */

extern "C" int rc_tables_set(const struct rcd_tabdev *t)
{
    return cudaMemcpyToSymbol(T, t, sizeof *t) == cudaSuccess && rc_tables_set_fast(t) == 0 ? 0 : -1;
}
#ifdef RC_STATS
extern "C" int rc_stats_set(unsigned *p) { return cudaMemcpyToSymbol(rc_stats, &p, sizeof p) == cudaSuccess ? 0 : -1; }
#endif

extern "C" void rc_launch_scatter(const unsigned char *stage, size_t cap, const int2 *refs, int nref, struct rcd_col *cols,
                                  struct rcd_sec *secs, int R, cudaStream_t s)
{
    if (nref > 0) rc_scatter<<<nref, 256, 0, s>>>(stage, cap, refs, cols, secs, R);
}

extern "C" void rc_launch_cast(const struct rcd_head *heads, const struct rcd_col *cols, const struct rcd_sec *secs,
                               const int *ids, int k, int R, int w, int h, int ch, uint8_t *frames, unsigned long long *seen_key,
                               int *seen_n, int *seen_xyz, struct rcd_seen *seen, int fp, cudaStream_t s)
{
    int nq = k * RCD_SEEN;
    rc_seen_clear<<<(nq + 127) / 128, 128, 0, s>>>(ids, k, seen_key, seen_n);
    dim3 grid((unsigned)((w * h + RC_BS - 1) / RC_BS), (unsigned)k);
    /* the instance (rcd_head.fp, raycast.h): FP32 the production cast, else the FP64 reference */
    if (fp == 32) rc_cast<float><<<grid, RC_BS, 0, s>>>(heads, cols, secs, ids, R, w, h, ch, frames, seen_key, seen_n, seen_xyz);
    else if (fp == RCD_FP_FIXED)
        rc_cast<float, true><<<grid, RC_BS, 0, s>>>(heads, cols, secs, ids, R, w, h, ch, frames, seen_key, seen_n, seen_xyz);
    else if (fp == RCD_FP_FAST16)
        rc_launch_cast_fast(heads, cols, secs, ids, k, R, w, h, ch, frames, seen_key, seen_n, seen_xyz, seen, 1, s);
    else if (fp == RCD_FP_MIXED16)
        rc_launch_cast_fast(heads, cols, secs, ids, k, R, w, h, ch, frames, seen_key, seen_n, seen_xyz, seen, 2, s);
    else if (fp == RCD_FP_FAST32)
        rc_launch_cast_fast(heads, cols, secs, ids, k, R, w, h, ch, frames, seen_key, seen_n, seen_xyz, seen, 4, s);
#if RC_H2 && !defined(RC_STATS)
    else if (fp == 16)
        rc_cast<h16, false, true><<<grid, RC_BS / 2, 0, s>>>(heads, cols, secs, ids, R, w, h, ch, frames, seen_key, seen_n, seen_xyz);
#else
    else if (fp == 16) rc_cast<h16><<<grid, RC_BS, 0, s>>>(heads, cols, secs, ids, R, w, h, ch, frames, seen_key, seen_n, seen_xyz);
#endif
    else rc_cast<double><<<grid, RC_BS, 0, s>>>(heads, cols, secs, ids, R, w, h, ch, frames, seen_key, seen_n, seen_xyz);
    rc_seen_fin<<<(nq + 127) / 128, 128, 0, s>>>(ids, k, seen_key, seen_n, seen_xyz, w * h, seen);
}
