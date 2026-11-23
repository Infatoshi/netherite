/* Training-optimized low-precision / mixed-precision GPU raycast for Blackwell sm_120 (lane/neon-prec-raycast).
 *
 * Separate compilation unit from cast.cu: built with --fmad=true -prec-div=false -prec-sqrt=false -ftz=true
 * so that strict bit-exact host-parity modes (FP64, FP32, fixed, host-exact FP16 in cast.cu) remain untouched.
 *
 * Supports four precision modes via template parameter M (1=fast16, 2=mixed16, 3=mixed_bf16, 4=fast32):
 *   1 = fast16     : native FP16 (__half + HADD/HSETP) DDA + half2 RGB shading/fog
 *   2 = mixed16    : fast FP32 incremental DDA (load-hiding schedule) + half2 RGB shading/fog
 *   3 = mixed_bf16 : fast FP32 incremental DDA (load-hiding schedule) + nv_bfloat162 RGB shading/fog
 *   4 = fast32     : fast FP32 incremental DDA (load-hiding schedule) + fast FP32 FFMA shading/fog
 *
 * Key sm_120 architectural optimizations (verified by NCU + ncustall):
 *   - Single-barrier parallel block setup (parallel head load + packed WINFO/WMASK + optional tile entity cull)
 *   - Direct FFMA ray generation + rsqrtf (eliminates 3 KB DA/DB shared memory + extra __syncthreads)
 *   - Packed WF_INFO (ri | present<<16): 1 LDS load per section lookup instead of 2
 *   - Branchless incremental DDA (tx += dtx) with speculative load-hiding (__ldg(&cur->ids[n_ci]) issued before t update)
 *   - Scalar RGB accumulators (0 bytes stack frame, 0 spills) and register-cached seen_ids
 */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <cuda_bf16.h>
#include <cuda_fp16.h>

#include "../../runtime/rchead.h"
#include "kern.h"

#ifndef RCF_BS
#define RCF_BS 128
#endif
#ifndef RCF_MINB
#define RCF_MINB 6
#endif
#ifndef RCF_ECULL
#define RCF_ECULL 0
#endif
#ifndef RCF_FLAT
#define RCF_FLAT 1          /* 1: the air loop's turns all end at its foot (no continue inside a branch); 0 the old form */
#endif

#define RCF_TAN_HALF 0.700207531f
#define RCF_MX 0x249
#define RCF_MZ 0x492
#define RCF_MY 0x924

__constant__ struct rcd_tabdev TF;

extern "C" int rc_tables_set_fast(const struct rcd_tabdev *t)
{
    return cudaMemcpyToSymbol(TF, t, sizeof *t) == cudaSuccess ? 0 : -1;
}

static __device__ __forceinline__ int rcf_bi_of(int f) { return f == 0 ? 1 : f == 1 ? 4 : f == 2 ? 2 : f == 3 ? 5 : f == 4 ? 0 : 3; }

static __device__ __forceinline__ int rcf_spread4(int v)
{
    v = (v | (v << 4)) & 0x0C3;
    return (v | (v << 2)) & 0x249;
}
#define RCF_SEC_XYZ(x, y, z) (rcf_spread4((x) & 15) | (rcf_spread4((z) & 15) << 1) | (rcf_spread4((y) & 15) << 2))

static __device__ __forceinline__ int rcf_mstep(int ci, int M, int s)
{
    return ((((ci & M) | (s > 0 ? (~M & 0xfff) : 0)) + s) & M) | (ci & ~M);
}

static __device__ __forceinline__ int rcf_nibble_get(const uint8_t *a, int i)
{
    return (__ldg(&a[i >> 1]) >> ((i & 1) << 2)) & 15;
}

static __device__ __forceinline__ int rcf_ring_index(int R, int cx, int cz)
{
    int a = cx % R, b = cz % R;
    return (a < 0 ? a + R : a) * R + (b < 0 ? b + R : b);
}

static __device__ __forceinline__ int rcf_liquid_of(int id)
{
    return (id == 8 || id == 9) ? 8 : (id == 10 || id == 11) ? 10 : 0;
}

static __device__ __forceinline__ int rcf_entry_face(int axis, int s)
{
    return axis == 0 ? (s > 0 ? 4 : 5) : axis == 1 ? (s > 0 ? 0 : 1) : (s > 0 ? 2 : 3);
}

static __device__ __forceinline__ float rcf_shade_of(int f)
{
    return f == 0 ? 0.5f : f == 1 ? 1.0f : f < 4 ? 0.8f : 0.6f;
}

static __device__ __forceinline__ int rcf_sign(float d)
{
    int b = __float_as_int(d);
    return (b << 1) == 0 ? 0 : b < 0 ? -1 : 1;
}

/* Compact float-converted shared head: threads 0..13 convert the 14 doubles in parallel (1 D2F each!),
 * eliminating both the serial tid==0 bottleneck AND the 128-thread D2F bottleneck on Blackwell's 1/64 FP64 pipe! */
struct rcf_head_sh {
    float d14[14]; /* ox,oy,oz, fx,fy,fz, rx,ry,rz, ux,uy,uz, aspect, rnext */
    int32_t eye_x, eye_y, eye_z;
    int32_t liquid, range, no_sky, dim, fancy, top;
    int32_t ok;
    int32_t seen_on;
    int16_t seen_ids[RCD_SEEN];
    int32_t cx0, cz0, ncx, ncz;
    int32_t fp, fp_pad_;
    float sky[3], fog[3];
    int32_t exp_fog, uw;
    float uw_rgb[3];
    float fs, fe, fd, sky_end, fspan;
    int32_t nent;
};
#define RCF_DEND offsetof(struct rcd_head, eye_x)
#define RCF_A offsetof(struct rcd_head, lm)
#define RCF_B offsetof(struct rcd_head, sky)
#define RCF_E offsetof(struct rcd_head, ent)
static_assert(RCF_DEND == 14 * sizeof(double), "14 leading doubles in rcd_head");
static_assert(offsetof(struct rcf_head_sh, eye_x) == 14 * sizeof(float) &&
                  offsetof(struct rcf_head_sh, sky) == 14 * sizeof(float) + (RCF_A - RCF_DEND) &&
                  sizeof(struct rcf_head_sh) == 14 * sizeof(float) + (RCF_A - RCF_DEND) + (RCF_E - RCF_B),
              "rcf_head_sh layout");

struct rcf_ptrs {
    const struct rcd_col *cols;
    const struct rcd_sec *secs;
#if NETH_RC_HEAD_U8
    const uint32_t *lm;             /* packed RGB8 lightmap (rchead.h) */
#else
    const float (*lm)[3];
#endif
    const struct rcd_ent *ent;
    unsigned long long *seen_key;
    int *seen_n;
    int *seen_xyz;
    int w, ch;
};

#define RCF_MAX_TILE_ENT 16
struct rcf_sent {
    float box[6];
    int16_t i0, i1, j0, j1;
    float rgb[3];
    int lmi;
};

__shared__ struct rcf_head_sh HF;
__shared__ struct rcf_ptrs PF;
__shared__ uint32_t WF_INFO[RCD_RMAX * RCD_RMAX];
__shared__ uint16_t WF_MASK[RCD_RMAX * RCD_RMAX];
#if RCF_ECULL
__shared__ int SF_NENT;
__shared__ struct rcf_sent SF_ENT[RCF_MAX_TILE_ENT];
#endif

static __device__ __forceinline__ int rcf_col_of(int x, int z)
{
    unsigned dx = (unsigned)((x >> 4) - HF.cx0), dz = (unsigned)((z >> 4) - HF.cz0);
    return (dx < (unsigned)HF.ncx && dz < (unsigned)HF.ncz) ? (int)(dx * (unsigned)HF.ncz + dz) : -1;
}

static __device__ __forceinline__ const struct rcd_sec *rcf_secw(int c, int sb)
{
    if (c < 0 || (unsigned)sb > 15u) return NULL;
    uint32_t info = WF_INFO[c];
    int ri = (int)(int16_t)(info & 0xffffu);
    uint16_t pres = (uint16_t)(info >> 16);
    return (ri >= 0 && ((pres >> sb) & 1u)) ? (PF.secs + (size_t)ri * 16 + sb) : NULL;
}

static __device__ __forceinline__ int rcf_cell_fast(int x, int y, int z, int *meta)
{
    const struct rcd_sec *s = rcf_secw(rcf_col_of(x, z), y >> 4);
    if (s == NULL) { *meta = 0; return 0; }
    int i = RCF_SEC_XYZ(x & 15, y, z & 15);
    int id = __ldg(&s->ids[i]);
    *meta = id ? rcf_nibble_get(s->metas, i) : 0;
    return id;
}

static __device__ __forceinline__ void rcf_light_fast(int x, int y, int z, int *sky, int *block)
{
    if (y < 0) { *sky = 0; *block = 0; return; }
    if (y > 255) y = 255;
    const int c = rcf_col_of(x, z);
    if (c < 0) { *sky = HF.no_sky ? 0 : 15; *block = 0; return; }
    const uint32_t info = WF_INFO[c];
    const int ri = (int)(int16_t)(info & 0xffffu);
    if (ri < 0) { *sky = HF.no_sky ? 0 : 15; *block = 0; return; }
    const int sb = y >> 4;
    const int has = (WF_MASK[c] >> sb) & 1;
    if (has && ((info >> (16 + sb)) & 1u))
    {
        const struct rcd_sec *s = PF.secs + (size_t)ri * 16 + sb;
        const int i = RCF_SEC_XYZ(x & 15, y, z & 15);
        *sky = HF.no_sky ? 0 : rcf_nibble_get(s->sky, i);
        *block = rcf_nibble_get(s->block, i);
        return;
    }
    *block = 0;
    *sky = (!HF.no_sky && !has && y >= __ldg(&PF.cols[ri].height[((z & 15) << 4) | (x & 15)])) ? 15 : 0;
}

static __device__ __forceinline__ float3 rcf_rgb(const float (*tab)[3], int bio)
{
    return make_float3(__ldg(&tab[bio][0]), __ldg(&tab[bio][1]), __ldg(&tab[bio][2]));
}

static __device__ __forceinline__ float3 rcf_rgb01(int32_t c)
{
    constexpr float inv255 = 1.0f / 255.0f;
    return make_float3((float)((c >> 16) & 255) * inv255, (float)((c >> 8) & 255) * inv255, (float)(c & 255) * inv255);
}

static __device__ __forceinline__ float3 rcf_tint_of(int id, int meta, int side, int x, int y, int z)
{
    const float3 W = make_float3(1.0f, 1.0f, 1.0f);
    int t = __ldg(&TF.tint[id]);
    if (t == TN_NONE) return W;
    int c = rcf_col_of(x, z);
    int bio = 0;
    if (c >= 0)
    {
        int ri = (int)(int16_t)(WF_INFO[c] & 0xffffu);
        if (ri >= 0) bio = __ldg(&PF.cols[ri].biome[((z & 15) << 4) | (x & 15)]);
    }
    switch (t)
    {
    case TN_GRASS_TOP: return side == 1 ? rcf_rgb(TF.grass, bio) : W;
    case TN_GRASS: return rcf_rgb(TF.grass, bio);
    case TN_FOLIAGE: return rcf_rgb(TF.foliage, bio);
    case TN_OLDLEAF:
        if ((meta & 3) == 1) return rcf_rgb01(6396257);
        if ((meta & 3) == 2) return rcf_rgb01(8431445);
        return rcf_rgb(TF.foliage, bio);
    case TN_WATER: return rcf_rgb(TF.water, bio);
    case TN_TALLGRASS: return meta == 0 ? W : rcf_rgb(TF.grass, bio);
    case TN_STEM: return rcf_rgb01((meta * 32 << 16) | ((255 - meta * 8) << 8) | (meta * 4));
    case TN_DPLANT:
    {
        int v = meta & 7;
        if (meta & 8)
        {
            int m2 = 0;
            (void)rcf_cell_fast(x, y - 1, z, &m2);
            v = m2 & 7;
        }
        return (v == 2 || v == 3) ? rcf_rgb(TF.grass, bio) : W;
    }
    }
    return W;
}

template <int MODE>
static __device__ __forceinline__ float3 rcf_lit(float r, float g, float b, float3 tint, int sky, int block, int emit,
                                                 float shade, float depth)
{
    if (block < emit) block = emit;
#if NETH_RC_HEAD_U8
    /* the packed RGB8 lightmap (rchead.h): the same 0..1 values cast.cu's lit() unpacks; __fdiv_rn because this
     * unit is built with fast math, whose '/' is an approximate reciprocal (fast32's frames moved with it) */
    const uint32_t lu = __ldg(&PF.lm[sky * 16 + block]);
    const float l0 = __fdiv_rn((float)((lu >> 16) & 255), 255.0f), l1 = __fdiv_rn((float)((lu >> 8) & 255), 255.0f),
                l2 = __fdiv_rn((float)(lu & 255), 255.0f);
#else
    const float *l = PF.lm[sky * 16 + block];
    const float l0 = __ldg(&l[0]), l1 = __ldg(&l[1]), l2 = __ldg(&l[2]);
#endif
    float f =HF.exp_fog ? __expf(-HF.fd * depth) : __saturatef((HF.fe - depth) * HF.fspan);

    if constexpr (MODE == 1 || MODE == 2)
    {
        const __half2 rg = __floats2half2_rn(r, g);
        const __half2 trg = __floats2half2_rn(tint.x * l0, tint.y * l1);
        const __half2 sh2 = __float2half2_rn(shade * f);
        const __half2 fog_rg = __floats2half2_rn(HF.fog[0], HF.fog[1]);
        const __half2 omf2 = __float2half2_rn(1.0f - f);
        const __half2 res_rg = __hfma2(__hmul2(rg, trg), sh2, __hmul2(fog_rg, omf2));
        const __half res_b = __hfma(__hmul(__float2half_rn(b), __float2half_rn(tint.z * l2)),
                                    __float2half_rn(shade * f),
                                    __hmul(__float2half_rn(HF.fog[2]), __float2half_rn(1.0f - f)));
        const float2 fg = __half22float2(res_rg);
        return make_float3(fg.x, fg.y, __half2float(res_b));
    }
    else if constexpr (MODE == 3)
    {
        const __nv_bfloat162 rg = __floats2bfloat162_rn(r, g);
        const __nv_bfloat162 trg = __floats2bfloat162_rn(tint.x * l0, tint.y * l1);
        const __nv_bfloat162 sh2 = __float2bfloat162_rn(shade * f);
        const __nv_bfloat162 fog_rg = __floats2bfloat162_rn(HF.fog[0], HF.fog[1]);
        const __nv_bfloat162 omf2 = __float2bfloat162_rn(1.0f - f);
        const __nv_bfloat162 res_rg = __hfma2(__hmul2(rg, trg), sh2, __hmul2(fog_rg, omf2));
        const __nv_bfloat16 res_b = __hfma(__hmul(__float2bfloat16_rn(b), __float2bfloat16_rn(tint.z * l2)),
                                           __float2bfloat16_rn(shade * f),
                                           __hmul(__float2bfloat16_rn(HF.fog[2]), __float2bfloat16_rn(1.0f - f)));
        const float2 fg = __bfloat1622float2(res_rg);
        return make_float3(fg.x, fg.y, __bfloat162float(res_b));
    }
    else
    {
        const float sf = shade * f;
        const float omf = 1.0f - f;
        return make_float3(fmaf(r * tint.x * l0, sf, HF.fog[0] * omf),
                           fmaf(g * tint.y * l1, sf, HF.fog[1] * omf),
                           fmaf(b * tint.z * l2, sf, HF.fog[2] * omf));
    }
}

static __device__ __forceinline__ uint32_t rcf_texel(uint16_t spr, int face, float lx, float ly, float lz)
{
    float u, v;
    switch (face)
    {
    case 0: case 1: u = lx, v = lz; break;
    case 2: u = 1.0f - lx, v = 1.0f - ly; break;
    case 3: u = lx, v = 1.0f - ly; break;
    case 4: u = lz, v = 1.0f - ly; break;
    default: u = 1.0f - lz, v = 1.0f - ly; break;
    }
    int iu = max(0, min(15, __float2int_rz(u * 16.0f)));
    int iv = max(0, min(15, __float2int_rz(v * 16.0f)));
    return __ldg(&TF.tex[spr][iv * 16 + iu]);
}

static __device__ __forceinline__ void rcf_seen_note(int id, int x, int y, int z, float t, int pi, int pj)
{
    const uint32_t p0 = ((const uint32_t *)HF.seen_ids)[0];
    const uint32_t p1 = ((const uint32_t *)HF.seen_ids)[1];
    const int s0 = (int)(int16_t)(p0 & 0xffffu);
    const int s1 = (int)(int16_t)(p0 >> 16);
    const int s2 = (int)(int16_t)(p1 & 0xffffu);
    const int s3 = (int)(int16_t)(p1 >> 16);
    int q = (id == s0) ? 0 : (id == s1) ? 1 : (id == s2) ? 2 : (id == s3) ? 3 : -1;
    if (q >= 0)
    {
        int p = pj * PF.w + pi;
        atomicAdd(&PF.seen_n[q], 1);
        PF.seen_xyz[3 * p] = x;
        PF.seen_xyz[3 * p + 1] = y;
        PF.seen_xyz[3 * p + 2] = z;
        atomicMin(&PF.seen_key[q], ((unsigned long long)__float_as_uint(t) << 32) | (unsigned)p);
    }
}

static __device__ __forceinline__ void rcf_dda_jump(float ox, float oy, float oz, float dx, float dy, float dz,
                                                    float idx, float idy, float idz, int sx, int sy, int sz,
                                                    int bx, int by, int bz,
                                                    int *ixp, int *iyp, int *izp, float *txp, float *typ, float *tzp)
{
    int ix = *ixp, iy = *iyp, iz = *izp;
    int lx = sx > 0 ? (ix | 15) : (ix & ~15);
    int ly = sy > 0 ? (iy | 15) : (iy & ~15);
    int lz = sz > 0 ? (iz | 15) : (iz & ~15);
    float tE = INFINITY;
    int aE = -1;
    if (sx) { tE = ((float)(lx + (sx > 0) - bx) - ox) * idx; aE = 0; }
    if (sy) { float e = ((float)(ly + (sy > 0) - by) - oy) * idy; if (e < tE) { tE = e; aE = 1; } }
    if (sz) { float e = ((float)(lz + (sz > 0) - bz) - oz) * idz; if (e < tE) { tE = e; aE = 2; } }

    auto adv = [](float o, float d, int i, int s, int last, float tE, int B0) -> int {
        int f = __float2int_rd(fmaf(tE, d, o)) + B0;
        return s > 0 ? (f < i ? i : (f > last ? last : f)) : (f > i ? i : (f < last ? last : f));
    };

    if (aE == 0) { ix = lx; *txp = tE; }
    else if (sx) { ix = adv(ox, dx, ix, sx, lx, tE, bx); *txp = ((float)(ix + (sx > 0) - bx) - ox) * idx; }
    if (aE == 1) { iy = ly; *typ = tE; }
    else if (sy) { iy = adv(oy, dy, iy, sy, ly, tE, by); *typ = ((float)(iy + (sy > 0) - by) - oy) * idy; }
    if (aE == 2) { iz = lz; *tzp = tE; }
    else if (sz) { iz = adv(oz, dz, iz, sz, lz, tE, bz); *tzp = ((float)(iz + (sz > 0) - bz) - oz) * idz; }

    *ixp = ix; *iyp = iy; *izp = iz;
}

template <int MODE>
__global__ void __launch_bounds__(RCF_BS, RCF_MINB)
rc_cast_fast_kern(const struct rcd_head *heads, const struct rcd_col *cols, const struct rcd_sec *secs,
                  const int *ids, int R, int w, int h, int ch, uint8_t *frames,
                  unsigned long long *seen_key, int *seen_n, int *seen_xyz)
{
    const int env = ids[blockIdx.y];
    const int tid = (int)threadIdx.x;

    constexpr int TW = (RCF_BS == 128) ? 16 : 8;
    constexpr int TH = (RCF_BS == 32) ? 4 : 8;
    const int tile = (w % TW == 0) && (h % TH == 0);
    const int tx0 = tile ? (int)(blockIdx.x % (w / TW)) * TW : 0;
    const int ty0 = tile ? (int)(blockIdx.x / (w / TW)) * TH : 0;

    const struct rcd_head *hd = heads + env;
    const struct rcd_col *ecols = cols + (size_t)env * R * R;

    /* Parallel block setup: 14 parallel D2F conversions + parallel int/float copy + WINFO/WMASK + tile entity cull
     * ALL before ONE SINGLE __syncthreads()! */
    if (tid < 14)
    {
        HF.d14[tid] = (float)__ldg(((const double *)hd) + tid);
    }
    else
    {
        constexpr int na = (int)((RCF_A - RCF_DEND) / 4), nb = (int)((RCF_E - RCF_B) / 4);
        const int i = tid - 14;
        if (i < na + nb)
        {
            const int *src = (const int *)hd;
            int *dst = (int *)&HF.eye_x;
            dst[i] = __ldg(&src[i < na ? ((int)(RCF_DEND / 4) + i) : (i - na + (int)(RCF_B / 4))]);
        }
    }
    if (tid == 0)
    {
        PF.cols = ecols;
        PF.secs = secs + (size_t)env * R * R * 16;
        PF.lm = hd->lm;
        PF.ent = hd->ent;
        PF.seen_key = seen_key + (size_t)env * RCD_SEEN;
        PF.seen_n = seen_n + (size_t)env * RCD_SEEN;
        PF.seen_xyz = seen_xyz + (size_t)env * w * h * 3;
        PF.w = w; PF.ch = ch;
    }

    const int cx0 = __ldg(&hd->cx0), cz0 = __ldg(&hd->cz0);
    const int ncx = __ldg(&hd->ncx), ncz = __ldg(&hd->ncz);
    const int ncells = ncx * ncz;
    for (int c = tid; c < ncells; c += RCF_BS)
    {
        int cx = cx0 + c / ncz, cz = cz0 + c % ncz;
        int ri = rcf_ring_index(R, cx, cz);
        const int4 C = __ldg((const int4 *)(ecols + ri));
        int ok = C.z && C.x == cx && C.y == cz;
        uint32_t pres = ok ? ((unsigned)C.w & 0xffff0000u) : 0u;
        uint32_t rlo = ok ? ((uint32_t)(uint16_t)(int16_t)ri) : 0xffffu;
        WF_INFO[c] = pres | rlo;
        WF_MASK[c] = ok ? (uint16_t)((unsigned)C.w & 0xffffu) : 0;
    }

#if RCF_ECULL
    /* Cooperative tile entity culling: use ballot/prefix or atomic after zeroing SF_NENT without extra sync?
     * Check nent directly from __ldg(&hd->nent); only touch SF_NENT when nent > 0 using warp-0 init?
     * Even simpler: 0-cost single-sync via warp-cooperative cull if nent <= 32, else fallback! */
    const int h_nent = __ldg(&hd->nent);
    if (tid == 0) SF_NENT = 0;
    if (h_nent > 0 && tile)
    {
        /* Only need sync before atomicAdd if tid > 0 could race tid == 0 initializing SF_NENT.
         * Use warp 0 (32 threads) or wait: 1 warp covers up to 32 entities in 1 iteration with __syncwarp()! */
        if (tid < 32)
        {
            __syncwarp();
            for (int e = tid; e < h_nent; e += 32)
            {
                const struct rcd_ent *en = &hd->ent[e];
                const int2 ri = __ldg((const int2 *)&en->i0), rj = __ldg((const int2 *)&en->j0);
                if (ri.y < tx0 || ri.x >= tx0 + TW || rj.y < ty0 || rj.x >= ty0 + TH) continue;
                int slot = atomicAdd(&SF_NENT, 1);
                if (slot < RCF_MAX_TILE_ENT)
                {
                    struct rcf_sent *dst = &SF_ENT[slot];
                    for (int a = 0; a < 6; ++a) dst->box[a] = (float)__ldg(&en->box[a]);
                    dst->i0 = (int16_t)ri.x; dst->i1 = (int16_t)ri.y;
                    dst->j0 = (int16_t)rj.x; dst->j1 = (int16_t)rj.y;
                    dst->rgb[0] = __ldg(&en->rgb[0]); dst->rgb[1] = __ldg(&en->rgb[1]); dst->rgb[2] = __ldg(&en->rgb[2]);
                    dst->lmi = __ldg(&en->lmi);
                }
            }
        }
    }
#endif
    __syncthreads();

    int pi, pj, p;
    if (tile)
    {
        if constexpr (RCF_BS == 128)
        {
            pi = tx0 + ((tid >> 5) & 1) * 8 + (tid & 7);
            pj = ty0 + (tid >> 6) * 4 + ((tid >> 3) & 3);
        }
        else
        {
            pi = tx0 + (tid & 7);
            pj = ty0 + (tid >> 3);
        }
        p = pj * w + pi;
    }
    else
    {
        p = (int)blockIdx.x * RCF_BS + tid;
        if (p >= w * h) return;
        pi = p % w; pj = p / w;
    }

    uint8_t *out = frames + ((size_t)env * w * h + p) * ch;
    if (!HF.ok)
    {
        out[0] = 0;
        if (ch == 3) out[1] = out[2] = 0;
        return;
    }

    const float ox = HF.d14[0], oy = HF.d14[1], oz = HF.d14[2];
    const float fx = HF.d14[3], fy = HF.d14[4], fz = HF.d14[5];
    const float rx = HF.d14[6], ry = HF.d14[7], rz = HF.d14[8];
    const float ux = HF.d14[9], uy = HF.d14[10], uz = HF.d14[11];
    const float sxs = RCF_TAN_HALF * HF.d14[12];
    const float rnext = HF.d14[13];
    const int bx = HF.eye_x, by = HF.eye_y, bz = HF.eye_z;

    /* Direct FFMA ray direction (no DA/DB shared memory) */
    const float sx_ndc = fmaf(2.0f * ((float)pi + 0.5f) / (float)w, sxs, -sxs);
    const float sy_ndc = fmaf(-2.0f * ((float)pj + 0.5f) / (float)h, RCF_TAN_HALF, RCF_TAN_HALF);
    float dx = fmaf(sy_ndc, ux, fmaf(sx_ndc, rx, fx));
    float dy = fmaf(sy_ndc, uy, fmaf(sx_ndc, ry, fy));
    float dz = fmaf(sy_ndc, uz, fmaf(sx_ndc, rz, fz));
    const float inv = rsqrtf(fmaf(dx, dx, fmaf(dy, dy, dz * dz)));
    dx *= inv; dy *= inv; dz *= inv;

    if constexpr (MODE == 1)
    {
        __half hdx = __float2half_rn(dx), hdy = __float2half_rn(dy), hdz = __float2half_rn(dz);
        dx = (fabsf(__half2float(hdx)) >= (1.0f / 65504.0f)) ? __half2float(hdx) : 0.0f;
        dy = (fabsf(__half2float(hdy)) >= (1.0f / 65504.0f)) ? __half2float(hdy) : 0.0f;
        dz = (fabsf(__half2float(hdz)) >= (1.0f / 65504.0f)) ? __half2float(hdz) : 0.0f;
    }

    const float fwd = fmaf(dx, fx, fmaf(dy, fy, dz * fz));
    const int sx = rcf_sign(dx), sy = rcf_sign(dy), sz = rcf_sign(dz);
    const float idx = sx ? (1.0f / dx) : 0.0f;
    const float idy = sy ? (1.0f / dy) : 0.0f;
    const float idz = sz ? (1.0f / dz) : 0.0f;

    float te = INFINITY;
    int ent = -1, eface = 1;
#if RCF_ECULL
    int ent_is_shared = 0;
    if (tile && SF_NENT <= RCF_MAX_TILE_ENT)
    {
        ent_is_shared = 1;
        const int ne = SF_NENT;
        for (int e = 0; e < ne; ++e)
        {
            const struct rcf_sent *en = &SF_ENT[e];
            if (pi < en->i0 || pi > en->i1 || pj < en->j0 || pj > en->j1) continue;
            float tn = -INFINITY, tf = INFINITY;
            int ax = -1, miss = 0;
            if (!sx) { if (ox < en->box[0] || ox > en->box[3]) miss = 1; }
            else { float t0 = (en->box[0] - ox) * idx, t1 = (en->box[3] - ox) * idx; float lo = fminf(t0, t1), hi = fmaxf(t0, t1); if (lo > tn) { tn = lo; ax = 0; } tf = fminf(tf, hi); }
            if (!miss) { if (!sy) { if (oy < en->box[1] || oy > en->box[4]) miss = 1; }
            else { float t0 = (en->box[1] - oy) * idy, t1 = (en->box[4] - oy) * idy; float lo = fminf(t0, t1), hi = fmaxf(t0, t1); if (lo > tn) { tn = lo; ax = 1; } tf = fminf(tf, hi); } }
            if (!miss) { if (!sz) { if (oz < en->box[2] || oz > en->box[5]) miss = 1; }
            else { float t0 = (en->box[2] - oz) * idz, t1 = (en->box[5] - oz) * idz; float lo = fminf(t0, t1), hi = fmaxf(t0, t1); if (lo > tn) { tn = lo; ax = 2; } tf = fminf(tf, hi); } }
            if (!miss && ax >= 0 && tn <= tf && tn > 0.0f && tn < te)
            {
                te = tn; ent = e; eface = rcf_entry_face(ax, ax == 0 ? sx : ax == 1 ? sy : sz);
            }
        }
    }
    else
#endif
    {
        for (int e = 0; e < HF.nent; ++e)
        {
            const struct rcd_ent *en = &PF.ent[e];
            const int2 ri = __ldg((const int2 *)&en->i0), rj = __ldg((const int2 *)&en->j0);
            if (pi < ri.x || pi > ri.y || pj < rj.x || pj > rj.y) continue;
            float tn = -INFINITY, tf = INFINITY;
            int ax = -1, miss = 0;
            float b0, b1;
            b0 = (float)__ldg(&en->box[0]); b1 = (float)__ldg(&en->box[3]);
            if (!sx) { if (ox < b0 || ox > b1) miss = 1; }
            else { float t0 = (b0 - ox) * idx, t1 = (b1 - ox) * idx; float lo = fminf(t0, t1), hi = fmaxf(t0, t1); if (lo > tn) { tn = lo; ax = 0; } tf = fminf(tf, hi); }
            if (!miss) { b0 = (float)__ldg(&en->box[1]); b1 = (float)__ldg(&en->box[4]);
            if (!sy) { if (oy < b0 || oy > b1) miss = 1; }
            else { float t0 = (b0 - oy) * idy, t1 = (b1 - oy) * idy; float lo = fminf(t0, t1), hi = fmaxf(t0, t1); if (lo > tn) { tn = lo; ax = 1; } tf = fminf(tf, hi); } }
            if (!miss) { b0 = (float)__ldg(&en->box[2]); b1 = (float)__ldg(&en->box[5]);
            if (!sz) { if (oz < b0 || oz > b1) miss = 1; }
            else { float t0 = (b0 - oz) * idz, t1 = (b1 - oz) * idz; float lo = fminf(t0, t1), hi = fmaxf(t0, t1); if (lo > tn) { tn = lo; ax = 2; } tf = fminf(tf, hi); } }
            if (!miss && ax >= 0 && tn <= tf && tn > 0.0f && tn < te)
            {
                te = tn; ent = e; eface = rcf_entry_face(ax, ax == 0 ? sx : ax == 1 ? sy : sz);
            }
        }
    }

    float3 acc = make_float3(0.0f, 0.0f, 0.0f), col = make_float3(0.0f, 0.0f, 0.0f);
    float trans = 1.0f;
    int in_water = 0, cull = -1;
    const int eye_liquid = HF.liquid;
    int eye_was_liquid = eye_liquid != 0;
    const unsigned ytop = sy >= 0 ? (unsigned)HF.top : 256u;
    const float tlim = fminf(te, rnext);
    const int ex = sx > 0 ? 0 : 15, ey = sy > 0 ? 0 : 15, ez = sz > 0 ? 0 : 15;
    const int up_out = (eye_liquid == 8 && sy > 0);

    int ix = bx, iy = by, iz = bz;
    if (iy >= HF.top && sy >= 0) goto miss;

    {
        float tx = sx ? ((float)(sx > 0) - ox) * idx : INFINITY;
        float ty = sy ? ((float)(sy > 0) - oy) * idy : INFINITY;
        float tz = sz ? ((float)(sz > 0) - oz) * idz : INFINITY;
        const float dtx = fabsf(idx), dty = fabsf(idy), dtz = fabsf(idz);
        __half htx = __float2half_rn(tx), hty = __float2half_rn(ty), htz = __float2half_rn(tz);
        const __half hdtx = __float2half_rn(dtx), hdty = __float2half_rn(dty), hdtz = __float2half_rn(dtz);
        const __half htlim = __float2half_rn(tlim);

        const struct rcd_sec *cur = rcf_secw(rcf_col_of(ix, iz), iy >> 4);
        int ci = RCF_SEC_XYZ(ix & 15, iy, iz & 15);
        int jump = 0;

        for (;;)
        {
            int axis = 0, id = 0, meta = 0, found = 0;
            float t = 0.0f;

            if (eye_liquid != 10 && (unsigned)iy <= 255u)
            {
                if (cur == NULL)
                {
                    if (eye_was_liquid && up_out) goto general_step;
                    jump = 1;
                }
                for (;;)
                {
                    if (jump)
                    {
                        jump = 0;
                        in_water = 0; cull = -1; eye_was_liquid = 0;
                        if constexpr (MODE == 1) { tx = __half2float(htx); ty = __half2float(hty); tz = __half2float(htz); }
                        rcf_dda_jump(ox, oy, oz, dx, dy, dz, idx, idy, idz, sx, sy, sz, bx, by, bz,
                                     &ix, &iy, &iz, &tx, &ty, &tz);
                        if constexpr (MODE == 1) { htx = __float2half_rn(tx); hty = __float2half_rn(ty); htz = __float2half_rn(tz); }
                    }

                    int bnd, top, spec_id;
                    if constexpr (MODE == 1)
                    {
                        const bool stx = __hle(htx, hty) && __hle(htx, htz);
                        const bool sty = (!stx) && __hle(hty, htz);
                        const bool stz = (!stx) && (!sty);
                        const int a = stx ? 0 : (sty ? 1 : 2);
                        const __half hta = stx ? htx : (sty ? hty : htz);
                        const int s = stx ? sx : (sty ? sy : sz);
                        const int n_ix = ix + (stx ? sx : 0);
                        const int n_iy = iy + (sty ? sy : 0);
                        const int n_iz = iz + (stz ? sz : 0);
                        const int ia = stx ? n_ix : (sty ? n_iy : n_iz);
                        bnd = (ia & 15) == (stx ? ex : (sty ? ey : ez));
                        const int n_ci = rcf_mstep(ci, stx ? RCF_MX : (sty ? RCF_MY : RCF_MZ), s);
                        top = sty && ((unsigned)n_iy >= ytop);
                        spec_id = (!bnd && cur != NULL) ? __ldg(&cur->ids[n_ci]) : 0;
                        ix = n_ix; iy = n_iy; iz = n_iz; ci = n_ci;
                        if (stx) htx = __hadd(htx, hdtx);
                        if (sty) hty = __hadd(hty, hdty);
                        if (stz) htz = __hadd(htz, hdtz);
                        axis = a;
                        t = __half2float(hta);
                        if (top || __hge(hta, htlim)) break;
                    }
                    else
                    {
                        const bool stx = (tx <= ty) && (tx <= tz);
                        const bool sty = (!stx) && (ty <= tz);
                        const bool stz = (!stx) && (!sty);
                        const int a = stx ? 0 : (sty ? 1 : 2);
                        const float ta = stx ? tx : (sty ? ty : tz);
                        const int s = stx ? sx : (sty ? sy : sz);
                        const int n_ix = ix + (stx ? sx : 0);
                        const int n_iy = iy + (sty ? sy : 0);
                        const int n_iz = iz + (stz ? sz : 0);
                        const int ia = stx ? n_ix : (sty ? n_iy : n_iz);
                        bnd = (ia & 15) == (stx ? ex : (sty ? ey : ez));
                        const int n_ci = rcf_mstep(ci, stx ? RCF_MX : (sty ? RCF_MY : RCF_MZ), s);
                        top = sty && ((unsigned)n_iy >= ytop);
                        spec_id = (!bnd && cur != NULL) ? __ldg(&cur->ids[n_ci]) : 0;
                        ix = n_ix; iy = n_iy; iz = n_iz; ci = n_ci;
                        tx = stx ? (tx + dtx) : tx;
                        ty = sty ? (ty + dty) : ty;
                        tz = stz ? (tz + dtz) : tz;
                        t = ta; axis = a;
                        if (top || t >= tlim) break;
                    }

#if RCF_FLAT
                    /* one back edge, no continue inside a branch (lane/hill-raycast-sparse): each turn ends at the
                     * loop's foot, so a warp's lanes reconverge every step (with the continues ptxas kept them apart:
                     * 5 lanes of 32 an instruction in this loop, against 13 to 14 in cast.cu's structured FP32 one).
                     * The same steps, loads and decisions in the same order as the form below. */
                    const int sec = bnd || cur == NULL;
                    if (sec)
                    {
                        ci = RCF_SEC_XYZ(ix & 15, iy, iz & 15);
                        cur = rcf_secw(rcf_col_of(ix, iz), iy >> 4);
                    }
                    const int up = eye_was_liquid && up_out && axis == 1;
                    if (cur == NULL)
                    {
                        if (up) { id = 0; meta = 0; found = 1; break; }
                        jump = 1;
                    }
                    else
                    {
                        id = sec ? __ldg(&cur->ids[ci]) : spec_id;
                        const int wet = (id == 8 || id == 9);
                        if (id == 0 && !up) { in_water = 0; cull = -1; eye_was_liquid = 0; }
                        else if (id != 0 && eye_liquid && wet) { cull = -1; eye_was_liquid = 1; }
                        else if (id != 0 && !up && in_water && wet) { eye_was_liquid = 0; }
                        else
                        {
                            meta = id ? rcf_nibble_get(cur->metas, ci) : 0;
                            found = 1;
                            break;
                        }
                    }
#else
                    if (bnd || cur == NULL)
                    {
                        ci = RCF_SEC_XYZ(ix & 15, iy, iz & 15);
                        cur = rcf_secw(rcf_col_of(ix, iz), iy >> 4);
                        if (cur == NULL)
                        {
                            if (eye_was_liquid && up_out && axis == 1) { id = 0; meta = 0; found = 1; break; }
                            jump = 1;
                            continue;
                        }
                        id = __ldg(&cur->ids[ci]);
                    }
                    else id = spec_id;

                    const int up = eye_was_liquid && up_out && axis == 1;
                    if (id == 0 && !up) { in_water = 0; cull = -1; eye_was_liquid = 0; continue; }
                    const int wet = (id == 8 || id == 9);
                    if (id != 0 && eye_liquid && wet) { cull = -1; eye_was_liquid = 1; continue; }
                    if (id != 0 && !up && in_water && wet) { eye_was_liquid = 0; continue; }
                    meta = id ? rcf_nibble_get(cur->metas, ci) : 0;
                    found = 1;
                    break;
#endif
                }
                if constexpr (MODE == 1) { tx = __half2float(htx); ty = __half2float(hty); tz = __half2float(htz); }
                if (!found) { if (t >= te) goto entity; goto miss; }
                goto block_hit;
            }

        general_step:
            {
                const bool stx = (tx <= ty) && (tx <= tz);
                const bool sty = (!stx) && (ty <= tz);
                if (stx) { t = tx; ix += sx; tx += dtx; axis = 0; }
                else if (sty) { t = ty; iy += sy; ty += dty; axis = 1; }
                else { t = tz; iz += sz; tz += dtz; axis = 2; }
                if (t >= te) goto entity;
                if (!(t <= (float)HF.range) || iy > 255 || iy < 0) goto miss;
                if (iy >= HF.top && sy >= 0) goto miss;
                cur = rcf_secw(rcf_col_of(ix, iz), iy >> 4);
                ci = RCF_SEC_XYZ(ix & 15, iy, iz & 15);
                id = cur ? __ldg(&cur->ids[ci]) : 0;
                meta = id ? rcf_nibble_get(cur->metas, ci) : 0;
            }

        block_hit:;
            const int px = ix - (axis == 0 ? sx : 0);
            const int py = iy - (axis == 1 ? sy : 0);
            const int pz = iz - (axis == 2 ? sz : 0);
            const int face = rcf_entry_face(axis, axis == 0 ? sx : (axis == 1 ? sy : sz));

            if (eye_was_liquid && eye_liquid == 8 && rcf_liquid_of(id) != 8 && axis == 1 && sy > 0 && TF.water_spr != 0xffff)
            {
                int sky, block;
                rcf_light_fast(px, py, pz, &sky, &block);
                uint32_t tw = rcf_texel(TF.water_spr, 1, fmaf(dx, t, ox) - (float)(ix - bx), 0.0f, fmaf(dz, t, oz) - (float)(iz - bz));
                const float3 tint = rcf_tint_of(9, 0, 1, px, py, pz);
                float3 wc = rcf_lit<MODE>((float)(tw & 255), (float)((tw >> 8) & 255), (float)((tw >> 16) & 255), tint, sky, block, 0, 1.0f, t * fwd);
                float a = (float)(tw >> 24) * (1.0f / 255.0f);
                float ta = trans * a;
                acc.x = fmaf(ta, wc.x, acc.x); acc.y = fmaf(ta, wc.y, acc.y); acc.z = fmaf(ta, wc.z, acc.z);
                trans *= (1.0f - a);
            }
            eye_was_liquid = eye_liquid && (rcf_liquid_of(id) == eye_liquid);
            const int kind = __ldg(&TF.kind[id]);
            if (kind == RK_AIR || (eye_liquid && rcf_liquid_of(id) == eye_liquid))
            {
                if (kind == RK_AIR) in_water = 0;
                cull = -1;
                if (cur == NULL)
                {
                    rcf_dda_jump(ox, oy, oz, dx, dy, dz, idx, idy, idz, sx, sy, sz, bx, by, bz, &ix, &iy, &iz, &tx, &ty, &tz);
                    if constexpr (MODE == 1) { htx = __float2half_rn(tx); hty = __float2half_rn(ty); htz = __float2half_rn(tz); }
                }
                continue;
            }
            const float rcx = (float)(ix - bx), rcy = (float)(iy - by), rcz = (float)(iz - bz);
            if (kind == RK_CUBE)
            {
                in_water = 0;
                const int key = id | (meta << 12);
                const int csame = __ldg(&TF.cull_same[id]);
                if (csame && key == cull) continue;
                cull = -1;
                const uint16_t *faces = TF.face[HF.fancy] + ((size_t)id * 16 + (size_t)meta) * 6;
                const uint16_t spr = __ldg(&faces[face]);
                const uint32_t tt = spr != 0xffff ? rcf_texel(spr, face, fmaf(dx, t, ox) - rcx, fmaf(dy, t, oy) - rcy, fmaf(dz, t, oz) - rcz) : 0;
                if ((tt >> 24) > 25u)
                {
                    int sky, block;
                    rcf_light_fast(px, py, pz, &sky, &block);
                    const float3 tint = rcf_tint_of(id, meta, face, ix, iy, iz);
                    col = rcf_lit<MODE>((float)(tt & 255), (float)((tt >> 8) & 255), (float)((tt >> 16) & 255), tint, sky, block,
                                        __ldg(&TF.emit[id]), rcf_shade_of(face), t * fwd);
                    if (HF.seen_on) rcf_seen_note(id, ix, iy, iz, t, pi, pj);
                    goto done;
                }
                if (csame) cull = key;
                continue;
            }
            const float texit = fminf(tx, fminf(ty, tz));
            if (kind == RK_WATER || kind == RK_LAVA)
            {
                if (kind == RK_WATER && in_water) continue;
                float hx = fmaf(dx, t, ox) - rcx, hy = fmaf(dy, t, oy) - rcy, hz = fmaf(dz, t, oz) - rcz;
                int m2 = 0;
                int above = iy < 255 ? rcf_cell_fast(ix, iy + 1, iz, &m2) : 0;
                float lh = rcf_liquid_of(above) == rcf_liquid_of(id) ? 1.0f : (1.0f - (float)((meta >= 8 ? 0 : meta) + 1) * (1.0f / 9.0f));
                float th = t; int hf = face;
                if (hy > lh)
                {
                    if (sy >= 0) { cull = -1; continue; }
                    th = (rcy + lh - oy) * idy;
                    if (th > texit) { cull = -1; continue; }
                    hf = 1; hx = fmaf(dx, th, ox) - rcx; hy = lh; hz = fmaf(dz, th, oz) - rcz;
                }
                if (th >= te) goto entity;
                int sky, block;
                rcf_light_fast(ix, iy, iz, &sky, &block);
                uint16_t spr = kind == RK_WATER ? TF.water_spr : TF.lava_spr;
                uint32_t tx_ = spr != 0xffff ? rcf_texel(spr, hf, hx, hy, hz) : 0xff808080u;
                const float3 tint = rcf_tint_of(id, meta, hf, ix, iy, iz);
                col = rcf_lit<MODE>((float)(tx_ & 255), (float)((tx_ >> 8) & 255), (float)((tx_ >> 16) & 255), tint, sky, block,
                                    __ldg(&TF.emit[id]), rcf_shade_of(hf), th * fwd);
                if (kind == RK_LAVA) goto done;
                float a = (float)(tx_ >> 24) * (1.0f / 255.0f);
                float ta = trans * a;
                acc.x = fmaf(ta, col.x, acc.x); acc.y = fmaf(ta, col.y, acc.y); acc.z = fmaf(ta, col.z, acc.z);
                trans *= (1.0f - a);
                in_water = 1; cull = -1;
                if constexpr (MODE == 1) { htx = __float2half_rn(tx); hty = __float2half_rn(ty); htz = __float2half_rn(tz); }
                continue;
            }
            in_water = 0;
            const int key = id | (meta << 12);
            const int csame = __ldg(&TF.cull_same[id]);
            if (csame && key == cull) continue;
            cull = -1;
            const uint16_t *faces = TF.face[HF.fancy] + ((size_t)id * 16 + (size_t)meta) * 6;
            if (kind == RK_BOX)
            {
                const double *bp = TF.bounds + ((size_t)id * 16 + (size_t)meta) * 6;
                float tn = -INFINITY, tf = INFINITY;
                int ax = -1, miss = 0;
                float lx0 = ox - rcx, ly0 = oy - rcy, lz0 = oz - rcz;
                float b0 = (float)__ldg(&bp[0]), b1 = (float)__ldg(&bp[3]);
                if (!sx) { if (lx0 < b0 || lx0 > b1) miss = 1; }
                else { float t0 = (b0 - lx0) * idx, t1 = (b1 - lx0) * idx; float lo = fminf(t0, t1), hi = fmaxf(t0, t1); if (lo > tn) { tn = lo; ax = 0; } tf = fminf(tf, hi); }
                if (!miss) { b0 = (float)__ldg(&bp[1]); b1 = (float)__ldg(&bp[4]);
                if (!sy) { if (ly0 < b0 || ly0 > b1) miss = 1; }
                else { float t0 = (b0 - ly0) * idy, t1 = (b1 - ly0) * idy; float lo = fminf(t0, t1), hi = fmaxf(t0, t1); if (lo > tn) { tn = lo; ax = 1; } tf = fminf(tf, hi); } }
                if (!miss) { b0 = (float)__ldg(&bp[2]); b1 = (float)__ldg(&bp[5]);
                if (!sz) { if (lz0 < b0 || lz0 > b1) miss = 1; }
                else { float t0 = (b0 - lz0) * idz, t1 = (b1 - lz0) * idz; float lo = fminf(t0, t1), hi = fmaxf(t0, t1); if (lo > tn) { tn = lo; ax = 2; } tf = fminf(tf, hi); } }
                if (!miss && ax >= 0 && tn <= tf && tn > 0.0f && tn <= texit)
                {
                    if (tn >= te) goto entity;
                    int bf = rcf_entry_face(ax, ax == 0 ? sx : ax == 1 ? sy : sz);
                    uint16_t spr = __ldg(&faces[bf]);
                    uint32_t tt = spr != 0xffff ? rcf_texel(spr, bf, fmaf(dx, tn, ox) - rcx, fmaf(dy, tn, oy) - rcy, fmaf(dz, tn, oz) - rcz) : 0;
                    if ((tt >> 24) > 25u)
                    {
                        float bd = (float)__ldg(&bp[rcf_bi_of(bf)]);
                        int edge = (bf & 1) ? (bd == 1.0f) : (bd == 0.0f);
                        int sky, block;
                        rcf_light_fast(edge ? px : ix, edge ? py : iy, edge ? pz : iz, &sky, &block);
                        const float3 tint = rcf_tint_of(id, meta, bf, ix, iy, iz);
                        col = rcf_lit<MODE>((float)(tt & 255), (float)((tt >> 8) & 255), (float)((tt >> 16) & 255), tint, sky, block,
                                            __ldg(&TF.emit[id]), rcf_shade_of(bf), tn * fwd);
                        goto done;
                    }
                }
            }
            else
            {
                float lx0 = ox - rcx, lz0 = oz - rcz, ly0 = oy - rcy;
                float tp0 = INFINITY, tp1 = INFINITY;
                float a1 = dx - dz, a2 = dx + dz;
                if (a1 != 0.0f) tp0 = -(lx0 - lz0) / a1;
                if (a2 != 0.0f) tp1 = (1.0f - lx0 - lz0) / a2;
                if (tp1 < tp0) { float tmp = tp0; tp0 = tp1; tp1 = tmp; }
                uint16_t spr = __ldg(&faces[0]);
                if (id == 175)
                {
                    int v = meta & 7, m2 = 0;
                    if (meta & 8) { int below = rcf_cell_fast(ix, iy - 1, iz, &m2); v = below == 175 ? (m2 & 7) : 6; }
                    spr = v < 6 ? ((meta & 8) ? TF.dp_high[v] : TF.dp_low[v]) : 0xffff;
                }
                for (int q = 0; q < 2 && spr != 0xffff; ++q)
                {
                    float tq = q == 0 ? tp0 : tp1;
                    if (!(tq >= t && tq <= texit)) continue;
                    float qx = fmaf(dx, tq, lx0), qy = fmaf(dy, tq, ly0);
                    if (qx < 0.05f || qx > 0.95f || qy < 0.0f || qy > 1.0f) continue;
                    if (tq >= te) goto entity;
                    int iu = max(0, min(15, __float2int_rz((qx - 0.05f) * (16.0f / 0.9f))));
                    int iv = max(0, min(15, __float2int_rz((1.0f - qy) * 16.0f)));
                    uint32_t tt = __ldg(&TF.tex[spr][iv * 16 + iu]);
                    if ((tt >> 24) <= 25u) continue;
                    int sky, block;
                    rcf_light_fast(ix, iy, iz, &sky, &block);
                    const float3 tint = rcf_tint_of(id, meta, 2, ix, iy, iz);
                    col = rcf_lit<MODE>((float)(tt & 255), (float)((tt >> 8) & 255), (float)((tt >> 16) & 255), tint, sky, block,
                                        __ldg(&TF.emit[id]), 1.0f, tq * fwd);
                    goto done;
                }
            }
            if constexpr (MODE == 1) { htx = __float2half_rn(tx); hty = __float2half_rn(ty); htz = __float2half_rn(tz); }
        }
    }

entity:
    {
        float er, eg, eb; int lmi;
#if RCF_ECULL
        if (ent_is_shared) { const struct rcf_sent *en = &SF_ENT[ent]; er = en->rgb[0]; eg = en->rgb[1]; eb = en->rgb[2]; lmi = en->lmi; }
        else
#endif
        { const struct rcd_ent *en = &PF.ent[ent]; er = __ldg(&en->rgb[0]); eg = __ldg(&en->rgb[1]); eb = __ldg(&en->rgb[2]); lmi = __ldg(&en->lmi); }
        col = rcf_lit<MODE>(er, eg, eb, make_float3(1.0f, 1.0f, 1.0f), lmi >> 4, lmi & 15, 0, rcf_shade_of(eface), te * fwd);
        goto done;
    }

miss:
    if (ent >= 0) goto entity;
    if (HF.exp_fog || HF.dim == -1) col = make_float3(HF.fog[0], HF.fog[1], HF.fog[2]);
    else if (HF.dim == 1) col = make_float3(TF.end_sky[0], TF.end_sky[1], TF.end_sky[2]);
    else if (dy <= 0.0f) col = make_float3(HF.fog[0], HF.fog[1], HF.fog[2]);
    else
    {
        float z = (16.0f / dy) * fwd;
        float f = __saturatef((HF.sky_end - z) / HF.sky_end);
        float omf = 1.0f - f;
        col = make_float3(fmaf(HF.sky[0], f, HF.fog[0] * omf), fmaf(HF.sky[1], f, HF.fog[1] * omf), fmaf(HF.sky[2], f, HF.fog[2] * omf));
    }

done:
    {
        float v0 = fmaf(trans, col.x, acc.x);
        float v1 = fmaf(trans, col.y, acc.y);
        float v2 = fmaf(trans, col.z, acc.z);
        if (HF.uw)
        {
            v0 = fmaf(v0, 0.5f, HF.uw_rgb[0] * 0.5f);
            v1 = fmaf(v1, 0.5f, HF.uw_rgb[1] * 0.5f);
            v2 = fmaf(v2, 0.5f, HF.uw_rgb[2] * 0.5f);
        }
        uint8_t b0 = (uint8_t)(v0 >= 255.0f ? 255 : v0 <= 0.0f ? 0 : __float2int_rn(v0));
        uint8_t b1 = (uint8_t)(v1 >= 255.0f ? 255 : v1 <= 0.0f ? 0 : __float2int_rn(v1));
        uint8_t b2 = (uint8_t)(v2 >= 255.0f ? 255 : v2 <= 0.0f ? 0 : __float2int_rn(v2));
        if (PF.ch == 1) out[0] = (uint8_t)((77u * b0 + 150u * b1 + 29u * b2 + 128u) >> 8);
        else { out[0] = b0; out[1] = b1; out[2] = b2; }
    }
}

extern "C" void rc_launch_cast_fast(const struct rcd_head *heads, const struct rcd_col *cols, const struct rcd_sec *secs,
                                    const int *ids, int k, int R, int w, int h, int ch, uint8_t *frames,
                                    unsigned long long *seen_key, int *seen_n, int *seen_xyz, struct rcd_seen *seen,
                                    int mode, cudaStream_t s)
{
    dim3 grid((unsigned)((w * h + RCF_BS - 1) / RCF_BS), (unsigned)k);
    if (mode == 1) rc_cast_fast_kern<1><<<grid, RCF_BS, 0, s>>>(heads, cols, secs, ids, R, w, h, ch, frames, seen_key, seen_n, seen_xyz);
    else if (mode == 2) rc_cast_fast_kern<2><<<grid, RCF_BS, 0, s>>>(heads, cols, secs, ids, R, w, h, ch, frames, seen_key, seen_n, seen_xyz);
    else if (mode == 3) rc_cast_fast_kern<3><<<grid, RCF_BS, 0, s>>>(heads, cols, secs, ids, R, w, h, ch, frames, seen_key, seen_n, seen_xyz);
    else rc_cast_fast_kern<4><<<grid, RCF_BS, 0, s>>>(heads, cols, secs, ids, R, w, h, ch, frames, seen_key, seen_n, seen_xyz);
}
