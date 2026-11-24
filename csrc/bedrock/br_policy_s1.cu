/* Netherite Bedrock Lane C: Square-NVFP4 Fwd + Selectable (FP16 | FP8 | NVFP4-SR) Bwd
 * PPO Policy with exact csrc/runtime/train/net.h factored s1 action heads + BRT1 Checkpoint I/O.
 *
 * Compile standalone test binary with -DBR_POLICY_S1_TEST:
 *   nvcc -O3 -gencode=arch=compute_120a,code=sm_120a -DBR_POLICY_S1_TEST -I. \
 *        br_policy_s1.cu -lcublasLt -lcublas -lcudart -lm -o br_policy_s1_test
 */
#include "br_policy_s1.h"

#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cublasLt.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Host/Device Helpers: Timing, RNG (SplitMix64 + Philox-4x32-10), NVFP4 & FP8
 * ============================================================================ */
static double s1_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static __host__ __device__ __forceinline__ uint64_t s1_sm64(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/* Exact Philox-4x32-10 matching csrc/runtime/train/actor.cu */
static __device__ __forceinline__ uint4 s1_philox4x32(uint4 c, uint2 k) {
    #pragma unroll
    for (int i = 0; i < 10; i++) {
        uint32_t hi0 = __umulhi(0xD2511F53u, c.x);
        uint32_t lo0 = 0xD2511F53u * c.x;
        uint32_t hi1 = __umulhi(0xCD9E8D57u, c.z);
        uint32_t lo1 = 0xCD9E8D57u * c.z;
        c = make_uint4(hi1 ^ c.y ^ k.x, lo1, hi0 ^ c.w ^ k.y, lo0);
        k.x += 0x9E3779B9u;
        k.y += 0xBB67AE85u;
    }
    return c;
}

/* Uniform in (0, 1) strictly < 1.0f so Gumbel -log(-log(u)) is always finite */
static __device__ __forceinline__ float s1_philox_u(uint64_t seed, uint64_t ctr, int b, int e) {
    uint4 r = s1_philox4x32(
        make_uint4((uint32_t)e >> 2, (uint32_t)b, (uint32_t)ctr, (uint32_t)(ctr >> 32)),
        make_uint2((uint32_t)seed, (uint32_t)(seed >> 32)));
    uint32_t v = (e & 3) == 0 ? r.x : ((e & 3) == 1 ? r.y : ((e & 3) == 2 ? r.z : r.w));
    float u = ((float)(v >> 8) + 0.5f) * (1.0f / 16777216.0f);
    return fminf(fmaxf(u, 1e-7f), 0x1.fffffep-1f);
}

/* Exact torch.nn.functional.logsigmoid matching actor.cu:717 & learn.cu:2058 */
static __host__ __device__ __forceinline__ float s1_logsig(float x) {
    return fminf(x, 0.0f) - log1pf(expf(-fabsf(x)));
}

static __device__ __forceinline__ float s1_wsum(float v) {
    #pragma unroll
    for (int o = 16; o > 0; o >>= 1) v += __shfl_xor_sync(0xffffffffu, v, o);
    return v;
}

static __device__ __forceinline__ float s1_wmax(float v) {
    #pragma unroll
    for (int o = 16; o > 0; o >>= 1) v = fmaxf(v, __shfl_xor_sync(0xffffffffu, v, o));
    return v;
}

/* UE4M3 block scale encoding + Blackwell 128x4 swizzled scale indexing */
static __host__ __device__ __forceinline__ uint8_t s1_f32_to_ue4m3(float x) {
    if (x <= 0.0f) return 0x01;
    if (x >= 448.0f) return 0x7E;
    uint32_t u;
    memcpy(&u, &x, 4);
    int exp = (int)((u >> 23) & 0xFF) - 127 + 7;
    int man = (int)((u >> 20) & 0x7);
    if (exp <= 0) {
        int m = (int)lrintf(x * 512.0f);
        return (uint8_t)(m < 1 ? 1 : (m > 7 ? 7 : m));
    }
    if (exp > 15) return 0x7E;
    uint8_t r = (uint8_t)((exp << 3) | man);
    return (r == 0x7F) ? 0x7E : r;
}

static __host__ __device__ __forceinline__ float s1_ue4m3_to_f32(uint8_t s) {
    int exp = (s >> 3) & 0xF, man = s & 7;
    return (exp == 0) ? ((float)man / 512.0f) : ldexpf(1.0f + (float)man / 8.0f, exp - 7);
}

static __host__ __device__ __forceinline__ size_t s1_swz_idx(int r, int kb, int num_k_tiles) {
    int rt = r >> 7, ri = r & 127, r32 = ri & 31, r4 = ri >> 5;
    int kt = kb >> 2, k4 = kb & 3;
    return (size_t)((rt * num_k_tiles + kt) * 512 + r32 * 16 + r4 * 4 + k4);
}

static __host__ __device__ __forceinline__ size_t s1_swz_bytes(int rows, int K) {
    return (size_t)((rows + 127) / 128) * 128 * (size_t)(((K / 16) + 3) / 4 * 4);
}

static __device__ __forceinline__ uint8_t s1_cvt_e2m1x2(float v0, float v1) {
    uint32_t r;
    asm volatile("{ .reg .b8 tmp; cvt.rn.satfinite.e2m1x2.f32 tmp, %2, %1; mov.b32 %0, {tmp, 0, 0, 0}; }"
                 : "=r"(r) : "f"(v0), "f"(v1));
    return (uint8_t)(r & 0xFFu);
}

static __device__ __forceinline__ uint16_t s1_cvt_e4m3x2(float v0, float v1) {
    uint16_t r;
    asm volatile("cvt.rn.satfinite.e4m3x2.f32 %0, %2, %1;" : "=h"(r) : "f"(v0), "f"(v1));
    return r;
}

/* Unbiased stochastic rounding jitter in E2M1 domain (NVFP4 pretraining recipe for backward) */
static __device__ __forceinline__ float s1_e2m1_sr(float x, uint32_t *rng) {
    uint32_t z = (*rng = (*rng * 1664525u + 1013904223u));
    float u = ((float)(z >> 8) * (1.0f / 16777216.0f)) - 0.5f;
    float ax = fabsf(x);
    float step = (ax < 2.0f) ? 0.5f : ((ax < 4.0f) ? 1.0f : 2.0f);
    return x + u * step;
}

/* ============================================================================
 * Quantization, Transpose, Bias, ReLU-Bwd, AdamW + NVFP4 Requant Kernels
 * ============================================================================ */

/* Warp-coalesced 16-thread-per-block FP16 -> NVFP4 + 128x4 swizzled UE4M3
 * (100% coalesced 128B global loads; K must be a multiple of 32) */
__global__ void k_s1_quant_f16_to_nvfp4_swz(
    const __half *__restrict__ src, uint8_t *__restrict__ dst_q,
    uint8_t *__restrict__ dst_sf, int rows, int K, int do_sr, uint32_t seed_off
) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    int total = rows * K;
    if (tid >= total) return;
    float v = __half2float(src[tid]);
    float am = fabsf(v);
    am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 8));
    am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 4));
    am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 2));
    am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 1));
    uint8_t sf = s1_f32_to_ue4m3(am * (1.0f / 6.0f));
    float inv = 1.0f / s1_ue4m3_to_f32(sf);
    float vs = v * inv;
    if (do_sr) {
        uint32_t rng = (uint32_t)tid * 747796405u + seed_off + 2891336453u;
        vs = s1_e2m1_sr(vs, &rng);
    }
    float partner = __shfl_xor_sync(0xffffffffu, vs, 1);
    int r = tid / K, c = tid - r * K;
    int kb = c >> 4, lane16 = c & 15;
    if (lane16 == 0) {
        int nkt = ((K >> 4) + 3) >> 2;
        dst_sf[s1_swz_idx(r, kb, nkt)] = sf;
    }
    if ((lane16 & 1) == 0) {
        dst_q[(size_t)r * (K >> 1) + (c >> 1)] = s1_cvt_e2m1x2(vs, partner);
    }
}

/* Sync FP32 master weights -> FP16 + (optional) swizzled NVFP4 on GPU (bit-identical to AdamW path) */
__global__ void k_s1_sync_weights_nvfp4(
    const float *__restrict__ w32, __half *__restrict__ w16,
    uint8_t *__restrict__ w_q, uint8_t *__restrict__ w_sf,
    int rows, int K, int active_rows, int do_nvfp4
) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    int total = rows * K;
    if (tid >= total) return;
    int r = tid / K, c = tid - r * K;
    float w = (r < active_rows) ? w32[tid] : 0.0f;
    w16[tid] = __float2half(w);
    if (do_nvfp4 && w_q && w_sf) {
        float am = fabsf(w);
        am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 8));
        am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 4));
        am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 2));
        am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 1));
        uint8_t sf = s1_f32_to_ue4m3(am * (1.0f / 6.0f));
        float inv = 1.0f / s1_ue4m3_to_f32(sf);
        float ws = w * inv;
        float partner = __shfl_xor_sync(0xffffffffu, ws, 1);
        int kb = c >> 4, lane16 = c & 15;
        if (lane16 == 0) {
            int nkt = ((K >> 4) + 3) >> 2;
            w_sf[s1_swz_idx(r, kb, nkt)] = sf;
        }
        if ((lane16 & 1) == 0) {
            w_q[(size_t)r * (K >> 1) + (c >> 1)] = s1_cvt_e2m1x2(ws, partner);
        }
    }
}

/* Coalesced 64x32 SMEM Tiled Transpose + NVFP4 Block-16 Quantize (128x4 swizzled UE4M3):
 * src [R, C] -> dst_T [C, R] packed E2M1 + swizzled UE4M3. Block(32, 4). */
__global__ void k_s1_quant_trans_nvfp4_swz(
    const __half *__restrict__ src, uint8_t *__restrict__ dst_q,
    uint8_t *__restrict__ dst_sf, int R, int C, int do_sr, uint32_t seed_off
) {
    __shared__ __half tile[64][33];
    int c0 = blockIdx.x * 32, r0 = blockIdx.y * 64;
    int c_in = c0 + threadIdx.x;
    if (c_in < C) {
        #pragma unroll
        for (int dy = threadIdx.y; dy < 64; dy += 4) {
            int r_in = r0 + dy;
            tile[dy][threadIdx.x] = (r_in < R) ? src[(size_t)r_in * C + c_in] : __float2half(0.0f);
        }
    }
    __syncthreads();
    int c = c0 + threadIdx.x;
    int kb = (r0 >> 4) + threadIdx.y;
    int kb_n = R >> 4;
    if (c >= C || kb >= kb_n) return;
    int nkt = (kb_n + 3) >> 2;
    int base_y = threadIdx.y << 4;
    float v[16], am = 0.0f;
    #pragma unroll
    for (int k = 0; k < 16; k++) {
        v[k] = __half2float(tile[base_y + k][threadIdx.x]);
        am = fmaxf(am, fabsf(v[k]));
    }
    uint8_t sf = s1_f32_to_ue4m3(am * (1.0f / 6.0f));
    float inv = 1.0f / s1_ue4m3_to_f32(sf);
    dst_sf[s1_swz_idx(c, kb, nkt)] = sf;
    uint32_t rng = (uint32_t)(c * kb_n + kb) * 747796405u + seed_off + 1013904223u;
    uint64_t packed = 0;
    #pragma unroll
    for (int k = 0; k < 16; k += 2) {
        float a0 = v[k] * inv, a1 = v[k + 1] * inv;
        if (do_sr) { a0 = s1_e2m1_sr(a0, &rng); a1 = s1_e2m1_sr(a1, &rng); }
        packed |= ((uint64_t)s1_cvt_e2m1x2(a0, a1)) << ((k >> 1) * 8);
    }
    *(uint64_t *)(dst_q + (size_t)c * (R >> 1) + (kb << 3)) = packed;
}

/* FP8 E4M3 1-step Delayed Scaling helpers */
__global__ void k_s1_absmax_f16(const __half *__restrict__ src, float *__restrict__ out_scale, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    float m = 0.0f;
    for (int j = i; j < n; j += gridDim.x * blockDim.x) m = fmaxf(m, fabsf(__half2float(src[j])));
    #pragma unroll
    for (int off = 16; off > 0; off >>= 1) m = fmaxf(m, __shfl_xor_sync(0xffffffffu, m, off));
    if ((threadIdx.x & 31) == 0) {
        float sc = fmaxf(m * (1.0f / 448.0f), 1e-5f);
        atomicMax((int *)out_scale, __float_as_int(sc));
    }
}

__global__ void k_s1_fp8_swap_scales(float *sc, float *nxt) {
    int i = threadIdx.x;
    if (i < 3) {
        float s = nxt[i];
        if (s > 1e-6f) sc[i] = s;
        nxt[i] = 0.0f;
    }
}

__global__ void k_s1_quant_f16_to_fp8(
    const __half *__restrict__ src, uint8_t *__restrict__ dst,
    const float *__restrict__ d_scale, float *__restrict__ d_nxt_sc, int n
) {
    int i = (blockIdx.x * blockDim.x + threadIdx.x) * 2;
    float m = 0.0f;
    if (i < n) {
        float inv = 1.0f / fmaxf(*d_scale, 1e-5f);
        float v0 = __half2float(src[i]), v1 = __half2float(src[i + 1]);
        m = fmaxf(fabsf(v0), fabsf(v1));
        *(uint16_t *)(dst + i) = s1_cvt_e4m3x2(v0 * inv, v1 * inv);
    }
    if (d_nxt_sc) {
        #pragma unroll
        for (int off = 16; off > 0; off >>= 1) m = fmaxf(m, __shfl_xor_sync(0xffffffffu, m, off));
        if ((threadIdx.x & 31) == 0 && m > 0.0f) {
            float sc = fmaxf(m * (1.0f / 448.0f), 1e-5f);
            atomicMax((int *)d_nxt_sc, __float_as_int(sc));
        }
    }
}

__global__ void k_s1_quant_trans_fp8(
    const __half *__restrict__ src, uint8_t *__restrict__ dst_T,
    const float *__restrict__ d_scale, float *__restrict__ d_nxt_sc, int R, int C
) {
    __shared__ uint8_t tile[32][33];
    float inv = 1.0f / fmaxf(*d_scale, 1e-5f);
    int x = blockIdx.x * 32 + threadIdx.x;
    int y0 = blockIdx.y * 32 + threadIdx.y;
    float m = 0.0f;
    if (x < C) {
        #pragma unroll
        for (int j = 0; j < 32; j += 8) {
            int y = y0 + j;
            if (y < R) {
                float v = __half2float(src[(size_t)y * C + x]);
                m = fmaxf(m, fabsf(v));
                tile[threadIdx.y + j][threadIdx.x] = (uint8_t)(s1_cvt_e4m3x2(v * inv, 0.0f) & 0xFFu);
            }
        }
    }
    __syncthreads();
    int ox = blockIdx.y * 32 + threadIdx.x;
    int oy0 = blockIdx.x * 32 + threadIdx.y;
    if (ox < R) {
        #pragma unroll
        for (int j = 0; j < 32; j += 8) {
            int oy = oy0 + j;
            if (oy < C) dst_T[(size_t)oy * R + ox] = tile[threadIdx.x][threadIdx.y + j];
        }
    }
    if (d_nxt_sc) {
        #pragma unroll
        for (int off = 16; off > 0; off >>= 1) m = fmaxf(m, __shfl_xor_sync(0xffffffffu, m, off));
        if ((threadIdx.x & 31) == 0 && m > 0.0f) {
            float sc = fmaxf(m * (1.0f / 448.0f), 1e-5f);
            atomicMax((int *)d_nxt_sc, __float_as_int(sc));
        }
    }
}

/* Fallback bias-add kernel if cuBLASLt fused FP32 bias epilogue is unavailable */
__global__ void k_s1_add_bias(float *__restrict__ y, const float *__restrict__ bias, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * BR_S1_HEAD_PAD;
    if (idx >= total) return;
    int c = idx % BR_S1_HEAD_PAD;
    y[idx] = (c < BR_S1_HEAD_DIM) ? (y[idx] + bias[c]) : 0.0f;
}

/* Vectorized 8-half ReLU backward */
__global__ void k_s1_relu_bwd(__half *__restrict__ dact, const __half *__restrict__ act, int n) {
    int i = (blockIdx.x * blockDim.x + threadIdx.x) * 8;
    if (i + 7 < n) {
        uint4 da = *(const uint4 *)(dact + i);
        uint4 ac = *(const uint4 *)(act  + i);
        __half2 *dh = (__half2 *)&da;
        const __half2 *ah = (const __half2 *)&ac;
        #pragma unroll
        for (int k = 0; k < 4; k++) {
            float2 a = __half22float2(ah[k]);
            float2 d = __half22float2(dh[k]);
            if (a.x <= 0.0f) d.x = 0.0f;
            if (a.y <= 0.0f) d.y = 0.0f;
            dh[k] = __float22half2_rn(d);
        }
        *(uint4 *)(dact + i) = da;
    } else {
        for (int j = i; j < n; j++) {
            if (__half2float(act[j]) <= 0.0f) dact[j] = __float2half(0.0f);
        }
    }
}

/* Warp-Coalesced AdamW + Swizzled NVFP4 Requant (16 threads per 16-elem NVFP4 block) */
__global__ void k_s1_adamw_requant_swz(
    float *__restrict__ w32, float *__restrict__ m1, float *__restrict__ m2,
    const __half *__restrict__ grad, __half *__restrict__ w16,
    uint8_t *__restrict__ w_q, uint8_t *__restrict__ w_sf,
    int rows, int K, int active_rows,
    float lr, float wd, float grad_clip, float inv_gscale,
    float bc1, float bc2, int do_nvfp4
) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    int total = rows * K;
    if (tid >= total) return;
    int r = tid / K, c = tid - r * K;
    float w = 0.0f;
    if (r < active_rows) {
        float g = __half2float(grad[tid]) * inv_gscale;
        if (grad_clip > 0.0f) g = fminf(fmaxf(g, -grad_clip), grad_clip);
        float mm = 0.9f * m1[tid] + 0.1f * g;
        float vv = 0.999f * m2[tid] + 0.001f * g * g;
        w = w32[tid];
        if (wd > 0.0f) w *= (1.0f - lr * wd);
        w -= lr * (mm * bc1) / (sqrtf(vv * bc2) + 1e-8f);
        w32[tid] = w;
        m1[tid] = mm;
        m2[tid] = vv;
    } else {
        w32[tid] = 0.0f;
    }
    w16[tid] = __float2half(w);
    if (do_nvfp4 && w_q && w_sf) {
        float am = fabsf(w);
        am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 8));
        am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 4));
        am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 2));
        am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 1));
        uint8_t sf = s1_f32_to_ue4m3(am * (1.0f / 6.0f));
        float inv = 1.0f / s1_ue4m3_to_f32(sf);
        float ws = w * inv;
        float partner = __shfl_xor_sync(0xffffffffu, ws, 1);
        int kb = c >> 4, lane16 = c & 15;
        if (lane16 == 0) {
            int nkt = ((K >> 4) + 3) >> 2;
            w_sf[s1_swz_idx(r, kb, nkt)] = sf;
        }
        if ((lane16 & 1) == 0) {
            w_q[(size_t)r * (K >> 1) + (c >> 1)] = s1_cvt_e2m1x2(ws, partner);
        }
    }
}

/* FP32 Adam update for packed head biases b_head[0..106] (keeps 107..111 zero) */
__global__ void k_s1_adamw_bias(
    float *__restrict__ b32, float *__restrict__ m1, float *__restrict__ m2,
    const float *__restrict__ gb, float lr, float grad_clip, float bc1, float bc2
) {
    int i = threadIdx.x;
    if (i >= BR_S1_HEAD_PAD) return;
    if (i < BR_S1_HEAD_DIM) {
        float g = gb[i];
        if (grad_clip > 0.0f) g = fminf(fmaxf(g, -grad_clip), grad_clip);
        float mm = 0.9f * m1[i] + 0.1f * g;
        float vv = 0.999f * m2[i] + 0.001f * g * g;
        b32[i] -= lr * (mm * bc1) / (sqrtf(vv * bc2) + 1e-8f);
        m1[i] = mm;
        m2[i] = vv;
    } else {
        b32[i] = 0.0f;
    }
}

/* Advantage sum & sum-of-squares reduction */
__global__ void k_s1_adv_stats(const float *__restrict__ adv, float *__restrict__ out_sum_sq, int M) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    float a = (i < M) ? adv[i] : 0.0f;
    float a2 = a * a;
    #pragma unroll
    for (int off = 16; off > 0; off >>= 1) {
        a  += __shfl_down_sync(0xffffffffu, a, off);
        a2 += __shfl_down_sync(0xffffffffu, a2, off);
    }
    if ((threadIdx.x & 31) == 0) {
        atomicAdd(&out_sum_sq[0], a);
        atomicAdd(&out_sum_sq[1], a2);
    }
}

/* ============================================================================
 * Exact csrc/runtime/train/net.h + actor.cu Factored s1 Sampler & PPO Loss/Grad
 * ============================================================================ */
#define S1_LW 8  /* 8 warps (256 threads) per block, 1 warp per sample */

/* Exact s1 sampler:
 *   pitch_bin (0..40) : Gumbel-max over h[0..40]
 *   yaw_bin   (0..40) : Gumbel-max over h[41..81]
 *   keys[0..13]       : independent Bernoulli u < sigmoid(h[82..95])
 *   hotbar    (0..9)  : Gumbel-max over h[96..105]
 *   value             : h[106]
 *   logp = ((log_softmax(pitch)[a0] + log_softmax(yaw)[a1]) + sum_k logsig((2*ak-1)*lk)) + log_softmax(hot)[a16]
 */
__global__ void k_s1_sample(
    const float *__restrict__ head_out,
    brc_s1_action_t *__restrict__ act_out,
    float *__restrict__ logp_out,
    float *__restrict__ val_out,
    int B, uint64_t rng_seed, uint64_t step_ctr, int greedy
) {
    int lane = threadIdx.x & 31;
    int wib  = threadIdx.x >> 5;
    int n    = blockIdx.x * S1_LW + wib;
    if (n >= B) return;

    const float *h = head_out + (size_t)n * BR_S1_HEAD_PAD;
    int has1 = (lane + 32 < BRC_CAM_BINS);
    float lp_cam[2];
    int   a_cam[2];

    #pragma unroll
    for (int c = 0; c < 2; c++) {
        const float *lh = h + c * BRC_CAM_BINS;
        float x0 = lh[lane];
        float x1 = has1 ? lh[lane + 32] : -INFINITY;
        float g0 = x0;
        float g1 = x1;
        if (!greedy) {
            float u0 = s1_philox_u(rng_seed, step_ctr, n, c * BRC_CAM_BINS + lane);
            g0 = x0 - logf(-logf(u0));
            if (has1) {
                float u1 = s1_philox_u(rng_seed, step_ctr, n, c * BRC_CAM_BINS + lane + 32);
                g1 = x1 - logf(-logf(u1));
            }
        }
        float gb = g0;
        int   gi = lane;
        if (has1 && g1 > gb) { gb = g1; gi = lane + 32; }
        float mx = fmaxf(x0, x1);
        #pragma unroll
        for (int o = 16; o > 0; o >>= 1) {
            float og = __shfl_xor_sync(0xffffffffu, gb, o);
            int   oi = __shfl_xor_sync(0xffffffffu, gi, o);
            if (og > gb || (og == gb && oi < gi)) { gb = og; gi = oi; }
            mx = fmaxf(mx, __shfl_xor_sync(0xffffffffu, mx, o));
        }
        float se = s1_wsum(expf(x0 - mx) + (has1 ? expf(x1 - mx) : 0.0f));
        a_cam[c]  = gi;
        lp_cam[c] = (lh[gi] - mx) - logf(se);
    }

    /* 14 independent Bernoulli keys */
    int kl = (lane < BRC_KEYS);
    float lk = 0.0f;
    if (kl) {
        float x = h[BR_S1_HEAD_KEY_OFF + lane];
        int key;
        if (greedy) {
            key = (x > 0.0f) ? 1 : 0;
        } else {
            float u = s1_philox_u(rng_seed, step_ctr, n, BR_S1_HEAD_KEY_OFF + lane);
            key = (u < 1.0f / (1.0f + expf(-x))) ? 1 : 0;
        }
        lk = key ? s1_logsig(x) : s1_logsig(-x);
        act_out[n].a[2 + lane] = key;
    }
    float lp_keys = s1_wsum(lk);

    /* 10-way hotbar categorical */
    int hl = (lane < BRC_HOTBAR_CATS);
    float xh = hl ? h[BR_S1_HEAD_HOT_OFF + lane] : -INFINITY;
    float gh = xh;
    if (hl && !greedy) {
        float u = s1_philox_u(rng_seed, step_ctr, n, BR_S1_HEAD_HOT_OFF + lane);
        gh = xh - logf(-logf(u));
    }
    int   hi = lane;
    float mh = xh;
    #pragma unroll
    for (int o = 16; o > 0; o >>= 1) {
        float og = __shfl_xor_sync(0xffffffffu, gh, o);
        int   oi = __shfl_xor_sync(0xffffffffu, hi, o);
        if (og > gh || (og == gh && oi < hi)) { gh = og; hi = oi; }
        mh = fmaxf(mh, __shfl_xor_sync(0xffffffffu, mh, o));
    }
    float sh = s1_wsum(hl ? expf(xh - mh) : 0.0f);
    float lp_hot = (h[BR_S1_HEAD_HOT_OFF + hi] - mh) - logf(sh);

    if (lane == 0) {
        act_out[n].a[0]  = a_cam[0];
        act_out[n].a[1]  = a_cam[1];
        act_out[n].a[16] = hi;
        logp_out[n] = ((lp_cam[0] + lp_cam[1]) + lp_keys) + lp_hot;
        val_out[n]  = h[BR_S1_HEAD_VAL_OFF];
    }
}

/* Exact factored PPO clipped loss + entropy gradient kernel (matching net.h & learn.cu):
 *   - Writes gscale-scaled FP16 gradients to dhead_16 [B, 112] (for head backward GEMM)
 *   - Optionally writes exact unscaled FP32 gradients to dhead_32 [B, 112] (when non-NULL)
 *   - Optionally accumulates mean FP32 bias gradients to gb_head [112] (when non-NULL)
 *   - Optionally accumulates [pg, vl, ent, kl, clip_cnt] into loss_acc [5] (when non-NULL)
 */
__global__ void k_s1_ppo_loss_grad(
    const float *__restrict__ head_out,
    const brc_s1_action_t *__restrict__ acts,
    const float *__restrict__ old_logp,
    const float *__restrict__ adv,
    const float *__restrict__ ret,
    const float *__restrict__ adv_stats,
    int total_samples,
    __half *__restrict__ dhead_16,
    float  *__restrict__ dhead_32,
    float  *__restrict__ gb_head,
    float  *__restrict__ loss_acc,
    int B, float clip_eps, float vf_coef, float ent_coef, float gscale
) {
    __shared__ float sb[S1_LW][BR_S1_HEAD_PAD];
    __shared__ float sa[S1_LW][5];

    int lane = threadIdx.x & 31;
    int wib  = threadIdx.x >> 5;
    int n    = blockIdx.x * S1_LW + wib;

    if (n < B) {
        const float *h = head_out + (size_t)n * BR_S1_HEAD_PAD;
        float a_norm = adv[n];
        if (adv_stats != NULL && total_samples > 0) {
            float mean = adv_stats[0] / (float)total_samples;
            float var  = fmaxf(adv_stats[1] / (float)total_samples - mean * mean, 1e-6f);
            a_norm = (a_norm - mean) * rsqrtf(var + 1e-6f);
        }

        int has1 = (lane + 32 < BRC_CAM_BINS);
        float pa[2], pb[2], la[2], lb[2], Hc[2];
        int   kc[2];
        float lp = 0.0f, E = 0.0f;

        #pragma unroll
        for (int c = 0; c < 2; c++) {
            const float *lh = h + c * BRC_CAM_BINS;
            float va = lh[lane];
            float vb = has1 ? lh[lane + 32] : -INFINITY;
            float mx = s1_wmax(fmaxf(va, vb));
            float se = s1_wsum(expf(va - mx) + (has1 ? expf(vb - mx) : 0.0f));
            float lse = logf(se);
            la[c] = (va - mx) - lse;
            lb[c] = has1 ? ((vb - mx) - lse) : 0.0f;
            pa[c] = expf(la[c]);
            pb[c] = has1 ? expf(lb[c]) : 0.0f;
            Hc[c] = -s1_wsum(pa[c] * la[c] + (has1 ? pb[c] * lb[c] : 0.0f));
            int k = acts[n].a[c];
            if (k < 0) k = 0;
            if (k >= BRC_CAM_BINS) k = BRC_CAM_BINS - 1;
            kc[c] = k;
            lp += __shfl_sync(0xffffffffu, (k < 32) ? la[c] : lb[c], k & 31);
            E  += Hc[c];
        }

        /* 14 Bernoulli keys */
        int kl = (lane < BRC_KEYS);
        float xk = kl ? h[BR_S1_HEAD_KEY_OFF + lane] : 0.0f;
        float ak = kl ? (float)(acts[n].a[2 + lane] != 0) : 0.0f;
        float lsp = s1_logsig(xk);
        float lsn = s1_logsig(-xk);
        float pk  = 1.0f / (1.0f + expf(-xk));
        lp += s1_wsum(kl ? (ak * lsp + (1.0f - ak) * lsn) : 0.0f);
        E  += -s1_wsum(kl ? (pk * lsp + (1.0f - pk) * lsn) : 0.0f);

        /* 10-way hotbar categorical */
        int hl = (lane < BRC_HOTBAR_CATS);
        float xh = hl ? h[BR_S1_HEAD_HOT_OFF + lane] : -INFINITY;
        float mh = s1_wmax(xh);
        float lseh = logf(s1_wsum(hl ? expf(xh - mh) : 0.0f));
        float lh = hl ? ((xh - mh) - lseh) : 0.0f;
        float ph = hl ? expf(lh) : 0.0f;
        float Hh = -s1_wsum(hl ? (ph * lh) : 0.0f);
        int kh = acts[n].a[16];
        if (kh < 0) kh = 0;
        if (kh >= BRC_HOTBAR_CATS) kh = BRC_HOTBAR_CATS - 1;
        lp += __shfl_sync(0xffffffffu, lh, kh);
        E  += Hh;

        /* PPO clipped surrogate (exact PyTorch/learn.cu min & clamp derivative) */
        float lp0   = old_logp[n];
        float diff  = fminf(fmaxf(lp - lp0, -10.0f), 10.0f);
        float ratio = expf(diff);
        float lo = 1.0f - clip_eps, hi = 1.0f + clip_eps;
        float rc = fminf(fmaxf(ratio, lo), hi);
        float s1 = ratio * a_norm, s2 = rc * a_norm;
        float pl = -fminf(s1, s2);
        float w1 = (s1 < s2) ? 1.0f : ((s1 == s2) ? 0.5f : 0.0f);
        float w2 = 1.0f - w1;
        float inr = (ratio >= lo && ratio <= hi) ? 1.0f : 0.0f;
        float glp = -(w1 + w2 * inr) * ratio * a_norm;
        float gE  = -ent_coef;

        /* Exact per-sample unscaled gradients into shared memory row sb[wib][0..111] */
        #pragma unroll
        for (int c = 0; c < 2; c++) {
            float ga = glp * ((lane == kc[c] ? 1.0f : 0.0f) - pa[c]) + gE * (-pa[c] * (la[c] + Hc[c]));
            sb[wib][c * BRC_CAM_BINS + lane] = ga;
            if (has1) {
                float gb = glp * ((lane + 32 == kc[c] ? 1.0f : 0.0f) - pb[c]) + gE * (-pb[c] * (lb[c] + Hc[c]));
                sb[wib][c * BRC_CAM_BINS + lane + 32] = gb;
            }
        }
        if (kl) {
            float gk = glp * (ak - pk) + gE * (-xk * pk * (1.0f - pk));
            sb[wib][BR_S1_HEAD_KEY_OFF + lane] = gk;
        }
        if (hl) {
            float gh = glp * ((lane == kh ? 1.0f : 0.0f) - ph) + gE * (-ph * (lh + Hh));
            sb[wib][BR_S1_HEAD_HOT_OFF + lane] = gh;
        }
        if (lane == 0) {
            float v  = h[BR_S1_HEAD_VAL_OFF];
            float dv = v - ret[n];
            sb[wib][BR_S1_HEAD_VAL_OFF] = vf_coef * dv;
            sa[wib][0] = pl;
            sa[wib][1] = 0.5f * dv * dv;
            sa[wib][2] = E;
            sa[wib][3] = lp0 - lp;
            sa[wib][4] = (fabsf(ratio - 1.0f) > clip_eps) ? 1.0f : 0.0f;
        }
        if (lane < (BR_S1_HEAD_PAD - BR_S1_HEAD_DIM)) {
            sb[wib][BR_S1_HEAD_DIM + lane] = 0.0f;
        }
        __syncwarp();

        /* Coalesced global stores of dhead_16 (scaled) and optional dhead_32 (unscaled) */
        __half *dh16 = dhead_16 + (size_t)n * BR_S1_HEAD_PAD;
        float  *dh32 = dhead_32 ? (dhead_32 + (size_t)n * BR_S1_HEAD_PAD) : NULL;
        for (int j = lane; j < BR_S1_HEAD_PAD; j += 32) {
            float g = sb[wib][j];
            dh16[j] = __float2half(g * gscale);
            if (dh32) dh32[j] = g;
        }
    } else {
        for (int j = lane; j < BR_S1_HEAD_PAD; j += 32) sb[wib][j] = 0.0f;
        if (lane < 5) sa[wib][lane] = 0.0f;
    }

    __syncthreads();

    if (gb_head != NULL && threadIdx.x < BR_S1_HEAD_DIM) {
        float sum = 0.0f;
        #pragma unroll
        for (int w = 0; w < S1_LW; w++) sum += sb[w][threadIdx.x];
        atomicAdd(&gb_head[threadIdx.x], sum / (float)B);
    }
    if (loss_acc != NULL && threadIdx.x < 5) {
        float sum = 0.0f;
        #pragma unroll
        for (int w = 0; w < S1_LW; w++) sum += sa[w][threadIdx.x];
        atomicAdd(&loss_acc[threadIdx.x], sum);
    }
}

/* ============================================================================
 * cublasLt Plan Builders (NVFP4, FP8 E4M3, FP16, FP16->FP32 + Bias)
 * ============================================================================ */
typedef struct {
    cublasLtMatmulDesc_t desc;
    cublasLtMatrixLayout_t lA, lB, lD;
    cublasLtMatmulAlgo_t algo;
    int valid;
} s1_lt_plan_t;

static void s1_destroy_plan(s1_lt_plan_t *p) {
    if (p->lA) cublasLtMatrixLayoutDestroy(p->lA);
    if (p->lB) cublasLtMatrixLayoutDestroy(p->lB);
    if (p->lD) cublasLtMatrixLayoutDestroy(p->lD);
    if (p->desc) cublasLtMatmulDescDestroy(p->desc);
    memset(p, 0, sizeof(*p));
}

static s1_lt_plan_t s1_make_nvfp4_plan(cublasLtHandle_t lt, int M, int N, int K,
                                       const uint8_t *dSA, const uint8_t *dSB,
                                       int relu_epilogue, size_t ws_bytes) {
    s1_lt_plan_t p; memset(&p, 0, sizeof(p));
    cublasLtMatmulDescCreate(&p.desc, CUBLAS_COMPUTE_32F, CUDA_R_32F);
    cublasOperation_t opT = CUBLAS_OP_T, opN = CUBLAS_OP_N;
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_TRANSA, &opT, sizeof(opT));
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_TRANSB, &opN, sizeof(opN));
    cublasLtMatmulMatrixScale_t sm = CUBLASLT_MATMUL_MATRIX_SCALE_VEC16_UE4M3;
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_A_SCALE_MODE, &sm, sizeof(sm));
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_B_SCALE_MODE, &sm, sizeof(sm));
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_A_SCALE_POINTER, &dSA, sizeof(dSA));
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_B_SCALE_POINTER, &dSB, sizeof(dSB));
    if (relu_epilogue) {
        cublasLtEpilogue_t ep = CUBLASLT_EPILOGUE_RELU;
        cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_EPILOGUE, &ep, sizeof(ep));
    }
    cublasLtMatrixLayoutCreate(&p.lA, CUDA_R_4F_E2M1, K, M, K);
    cublasLtMatrixLayoutCreate(&p.lB, CUDA_R_4F_E2M1, K, N, K);
    cublasLtMatrixLayoutCreate(&p.lD, CUDA_R_16F,     M, N, M);
    cublasLtMatmulPreference_t pref; cublasLtMatmulPreferenceCreate(&pref);
    cublasLtMatmulPreferenceSetAttribute(pref, CUBLASLT_MATMUL_PREF_MAX_WORKSPACE_BYTES, &ws_bytes, sizeof(ws_bytes));
    cublasLtMatmulHeuristicResult_t hr; int found = 0;
    if (cublasLtMatmulAlgoGetHeuristic(lt, p.desc, p.lA, p.lB, p.lD, p.lD, pref, 1, &hr, &found) == CUBLAS_STATUS_SUCCESS && found > 0) {
        p.algo = hr.algo; p.valid = 1;
    }
    cublasLtMatmulPreferenceDestroy(pref);
    return p;
}

static s1_lt_plan_t s1_make_fp8_plan(cublasLtHandle_t lt, int M, int N, int K,
                                     const float *dSA, const float *dSB, size_t ws_bytes) {
    s1_lt_plan_t p; memset(&p, 0, sizeof(p));
    cublasLtMatmulDescCreate(&p.desc, CUBLAS_COMPUTE_32F, CUDA_R_32F);
    cublasOperation_t opT = CUBLAS_OP_T, opN = CUBLAS_OP_N;
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_TRANSA, &opT, sizeof(opT));
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_TRANSB, &opN, sizeof(opN));
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_A_SCALE_POINTER, &dSA, sizeof(dSA));
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_B_SCALE_POINTER, &dSB, sizeof(dSB));
    cublasLtMatrixLayoutCreate(&p.lA, CUDA_R_8F_E4M3, K, M, K);
    cublasLtMatrixLayoutCreate(&p.lB, CUDA_R_8F_E4M3, K, N, K);
    cublasLtMatrixLayoutCreate(&p.lD, CUDA_R_16F,     M, N, M);
    cublasLtMatmulPreference_t pref; cublasLtMatmulPreferenceCreate(&pref);
    cublasLtMatmulPreferenceSetAttribute(pref, CUBLASLT_MATMUL_PREF_MAX_WORKSPACE_BYTES, &ws_bytes, sizeof(ws_bytes));
    cublasLtMatmulHeuristicResult_t hr; int found = 0;
    if (cublasLtMatmulAlgoGetHeuristic(lt, p.desc, p.lA, p.lB, p.lD, p.lD, pref, 1, &hr, &found) == CUBLAS_STATUS_SUCCESS && found > 0) {
        p.algo = hr.algo; p.valid = 1;
    }
    cublasLtMatmulPreferenceDestroy(pref);
    return p;
}

static s1_lt_plan_t s1_make_fp16_plan(cublasLtHandle_t lt, cublasOperation_t opA, cublasOperation_t opB,
                                      int M, int N, int rowsA, int colsA, int rowsB, int colsB,
                                      cudaDataType_t dtypeD, int relu_epilogue,
                                      const float *d_bias_f32, size_t ws_bytes) {
    s1_lt_plan_t p; memset(&p, 0, sizeof(p));
    cublasLtMatmulDescCreate(&p.desc, CUBLAS_COMPUTE_32F, CUDA_R_32F);
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_TRANSA, &opA, sizeof(opA));
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_TRANSB, &opB, sizeof(opB));
    if (d_bias_f32 != NULL) {
        cublasLtEpilogue_t ep = relu_epilogue ? CUBLASLT_EPILOGUE_RELU_BIAS : CUBLASLT_EPILOGUE_BIAS;
        cudaDataType_t bt = CUDA_R_32F;
        cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_EPILOGUE, &ep, sizeof(ep));
        cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_BIAS_POINTER, &d_bias_f32, sizeof(d_bias_f32));
        cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_BIAS_DATA_TYPE, &bt, sizeof(bt));
    } else if (relu_epilogue) {
        cublasLtEpilogue_t ep = CUBLASLT_EPILOGUE_RELU;
        cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_EPILOGUE, &ep, sizeof(ep));
    }
    cublasLtMatrixLayoutCreate(&p.lA, CUDA_R_16F, rowsA, colsA, rowsA);
    cublasLtMatrixLayoutCreate(&p.lB, CUDA_R_16F, rowsB, colsB, rowsB);
    cublasLtMatrixLayoutCreate(&p.lD, dtypeD,     M,     N,     M);
    cublasLtMatmulPreference_t pref; cublasLtMatmulPreferenceCreate(&pref);
    cublasLtMatmulPreferenceSetAttribute(pref, CUBLASLT_MATMUL_PREF_MAX_WORKSPACE_BYTES, &ws_bytes, sizeof(ws_bytes));
    cublasLtMatmulHeuristicResult_t hr; int found = 0;
    if (cublasLtMatmulAlgoGetHeuristic(lt, p.desc, p.lA, p.lB, p.lD, p.lD, pref, 1, &hr, &found) == CUBLAS_STATUS_SUCCESS && found > 0) {
        p.algo = hr.algo; p.valid = 1;
    }
    cublasLtMatmulPreferenceDestroy(pref);
    return p;
}

/* ============================================================================
 * Policy Struct & Internal Lifecycle
 * ============================================================================ */
struct br_policy_s1 {
    br_policy_s1_cfg_t cfg;
    cudaStream_t own_stream;
    cudaStream_t stream;
    cublasLtHandle_t lt;
    void *d_ws;
    size_t ws_bytes;

    /* Forward working buffers */
    uint8_t *d_obs_q, *d_obs_sf;
    uint8_t *d_h1_q, *d_h1_sf;
    __half  *d_h1, *d_h2;
    float   *d_head_out;   /* [Q, 112] FP32 (includes bias!) */

    /* Backward working buffers */
    __half  *d_dhead_16;   /* [Q, 112] FP16 scaled by grad_loss_scale */
    __half  *d_dh2, *d_dh1;

    /* Backward FP8 E4M3 buffers & 1-step delayed scales [0=W2, 1=dh2, 2=h1] */
    uint8_t *d_W2T_f8, *d_dh2_f8, *d_dh2T_f8, *d_h1T_f8;
    float   *d_fp8_sc, *d_fp8_nxt;
    int      fp8_init;

    /* Backward NVFP4 buffers */
    uint8_t *d_W2T_q,  *d_W2T_sf;
    uint8_t *d_dh2_q,  *d_dh2_sf;
    uint8_t *d_dh2T_q, *d_dh2T_sf;
    uint8_t *d_h1T_q,  *d_h1T_sf;

    /* Trunk weights: W1 [H, 128], W2 [H, H] (no trunk bias per br_contract.h) */
    float *d_W1_32, *d_W1_m1, *d_W1_m2; __half *d_W1_16, *d_gW1; uint8_t *d_W1_q, *d_W1_sf;
    float *d_W2_32, *d_W2_m1, *d_W2_m2; __half *d_W2_16, *d_gW2; uint8_t *d_W2_q, *d_W2_sf;

    /* Packed head weights [112, H] + bias [112] (rows 0..106 active, 107..111 zero pad) */
    float *d_Wh_32, *d_Wh_m1, *d_Wh_m2; __half *d_Wh_16, *d_gWh;
    float *d_bh_32, *d_bh_m1, *d_bh_m2, *d_gbh;

    /* Internal scratch for ppo_update */
    float *d_adv_stats; /* [2] */
    float *d_loss_acc;  /* [5] */

    /* cuBLASLt plans */
    s1_lt_plan_t p_f1_nv4, p_f2_nv4;
    s1_lt_plan_t p_f1_f16, p_f2_f16;
    s1_lt_plan_t p_fh_f16;
    int          head_bias_fused;

    s1_lt_plan_t p_bh_d, p_bh_w;
    s1_lt_plan_t p_b2_d_f16, p_b2_w_f16, p_b1_w_f16;
    s1_lt_plan_t p_b2_d_f8,  p_b2_w_f8;
    s1_lt_plan_t p_b2_d_nv4, p_b2_w_nv4;

    int adam_step;
};

static void s1_alloc_weight(float **d32, float **dm1, float **dm2, __half **d16, __half **dg,
                            uint8_t **dq, uint8_t **dsf, int rows, int K, int active_rows,
                            float scale, uint64_t *rng) {
    size_t n = (size_t)rows * K;
    float *h32 = (float *)calloc(n, sizeof(float));
    for (int r = 0; r < active_rows; r++) {
        for (int c = 0; c < K; c++) {
            float s = 0.0f;
            for (int j = 0; j < 6; j++) s += ((float)(s1_sm64(rng) & 0xFFFFu) / 65536.0f) - 0.5f;
            h32[(size_t)r * K + c] = s * 1.41421356f * scale;
        }
    }
    cudaMalloc(d32, n * sizeof(float));
    cudaMalloc(dm1, n * sizeof(float));
    cudaMalloc(dm2, n * sizeof(float));
    cudaMalloc(d16, n * sizeof(__half));
    cudaMalloc(dg,  n * sizeof(__half));
    cudaMemcpy(*d32, h32, n * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemset(*dm1, 0, n * sizeof(float));
    cudaMemset(*dm2, 0, n * sizeof(float));
    if (dq && dsf) {
        cudaMalloc(dq,  (size_t)rows * (K / 2));
        cudaMalloc(dsf, s1_swz_bytes(rows, K));
    }
    free(h32);
}

void br_policy_s1_sync_weights(br_policy_s1_t *pol) {
    int H = pol->cfg.hidden;
    k_s1_sync_weights_nvfp4<<<(H * BRC_OBS_DIM + 255) / 256, 256, 0, pol->stream>>>(
        pol->d_W1_32, pol->d_W1_16, pol->d_W1_q, pol->d_W1_sf, H, BRC_OBS_DIM, H, 1);
    k_s1_sync_weights_nvfp4<<<(H * H + 255) / 256, 256, 0, pol->stream>>>(
        pol->d_W2_32, pol->d_W2_16, pol->d_W2_q, pol->d_W2_sf, H, H, H, 1);
    k_s1_sync_weights_nvfp4<<<(BR_S1_HEAD_PAD * H + 255) / 256, 256, 0, pol->stream>>>(
        pol->d_Wh_32, pol->d_Wh_16, NULL, NULL, BR_S1_HEAD_PAD, H, BR_S1_HEAD_DIM, 0);
}

br_policy_s1_t *br_policy_s1_create(const br_policy_s1_cfg_t *cfg) {
    br_policy_s1_t *pol = (br_policy_s1_t *)calloc(1, sizeof(br_policy_s1_t));
    if (cfg) pol->cfg = *cfg;
    if (pol->cfg.batch_q <= 0)         pol->cfg.batch_q = 8192;
    if (pol->cfg.hidden <= 0)          pol->cfg.hidden = 1024;
    if (pol->cfg.lr <= 0.0f)           pol->cfg.lr = 3e-4f;
    if (pol->cfg.clip_eps <= 0.0f)     pol->cfg.clip_eps = 0.2f;
    if (pol->cfg.vf_coef <= 0.0f)      pol->cfg.vf_coef = 0.5f;
    if (pol->cfg.ent_coef < 0.0f)      pol->cfg.ent_coef = 0.01f;
    if (pol->cfg.grad_loss_scale <= 0.0f) pol->cfg.grad_loss_scale = 16.0f;
    if (pol->cfg.seed == 0)            pol->cfg.seed = 0x20261124ULL;
    if (pol->cfg.grad_clip == 0.0f)    pol->cfg.grad_clip = 1.0f;

    int Q = pol->cfg.batch_q;
    int H = pol->cfg.hidden;

    cudaStreamCreate(&pol->own_stream);
    pol->stream = pol->own_stream;
    cublasLtCreate(&pol->lt);
    pol->ws_bytes = 32 * 1024 * 1024;
    cudaMalloc(&pol->d_ws, pol->ws_bytes);

    /* Forward buffers */
    cudaMalloc(&pol->d_obs_q,    (size_t)Q * (BRC_OBS_DIM / 2));
    cudaMalloc(&pol->d_obs_sf,   s1_swz_bytes(Q, BRC_OBS_DIM));
    cudaMalloc(&pol->d_h1_q,     (size_t)Q * (H / 2));
    cudaMalloc(&pol->d_h1_sf,    s1_swz_bytes(Q, H));
    cudaMalloc(&pol->d_h1,       (size_t)Q * H * sizeof(__half));
    cudaMalloc(&pol->d_h2,       (size_t)Q * H * sizeof(__half));
    cudaMalloc(&pol->d_head_out, (size_t)Q * BR_S1_HEAD_PAD * sizeof(float));

    /* Backward buffers */
    cudaMalloc(&pol->d_dhead_16, (size_t)Q * BR_S1_HEAD_PAD * sizeof(__half));
    cudaMalloc(&pol->d_dh2,      (size_t)Q * H * sizeof(__half));
    cudaMalloc(&pol->d_dh1,      (size_t)Q * H * sizeof(__half));

    /* Backward FP8 buffers */
    cudaMalloc(&pol->d_W2T_f8,   (size_t)H * H);
    cudaMalloc(&pol->d_dh2_f8,   (size_t)Q * H);
    cudaMalloc(&pol->d_dh2T_f8,  (size_t)H * Q);
    cudaMalloc(&pol->d_h1T_f8,   (size_t)H * Q);
    cudaMalloc(&pol->d_fp8_sc,   3 * sizeof(float));
    cudaMalloc(&pol->d_fp8_nxt,  3 * sizeof(float));
    cudaMemset(pol->d_fp8_sc,    0, 3 * sizeof(float));
    cudaMemset(pol->d_fp8_nxt,   0, 3 * sizeof(float));

    /* Backward NVFP4 buffers */
    cudaMalloc(&pol->d_W2T_q,    (size_t)H * (H / 2));
    cudaMalloc(&pol->d_W2T_sf,   s1_swz_bytes(H, H));
    cudaMalloc(&pol->d_dh2_q,    (size_t)Q * (H / 2));
    cudaMalloc(&pol->d_dh2_sf,   s1_swz_bytes(Q, H));
    cudaMalloc(&pol->d_dh2T_q,   (size_t)H * (Q / 2));
    cudaMalloc(&pol->d_dh2T_sf,  s1_swz_bytes(H, Q));
    cudaMalloc(&pol->d_h1T_q,    (size_t)H * (Q / 2));
    cudaMalloc(&pol->d_h1T_sf,   s1_swz_bytes(H, Q));

    /* Initialize weights: Kaiming for ReLU trunk, small orthogonal-scale for policy/value heads */
    uint64_t wrng = pol->cfg.seed ^ 0xA5A55A5A12345678ULL;
    s1_alloc_weight(&pol->d_W1_32, &pol->d_W1_m1, &pol->d_W1_m2, &pol->d_W1_16, &pol->d_gW1,
                    &pol->d_W1_q, &pol->d_W1_sf, H, BRC_OBS_DIM, H, sqrtf(2.0f / (float)BRC_OBS_DIM), &wrng);
    s1_alloc_weight(&pol->d_W2_32, &pol->d_W2_m1, &pol->d_W2_m2, &pol->d_W2_16, &pol->d_gW2,
                    &pol->d_W2_q, &pol->d_W2_sf, H, H, H, sqrtf(2.0f / (float)H), &wrng);
    s1_alloc_weight(&pol->d_Wh_32, &pol->d_Wh_m1, &pol->d_Wh_m2, &pol->d_Wh_16, &pol->d_gWh,
                    NULL, NULL, BR_S1_HEAD_PAD, H, BR_S1_HEAD_DIM, 0.05f / sqrtf((float)H), &wrng);

    /* Head biases [112] FP32 */
    float h_bh[BR_S1_HEAD_PAD];
    memset(h_bh, 0, sizeof(h_bh));
    h_bh[BR_S1_HEAD_HOT_OFF] = pol->cfg.hot_keep_bias;
    cudaMalloc(&pol->d_bh_32, BR_S1_HEAD_PAD * sizeof(float));
    cudaMalloc(&pol->d_bh_m1, BR_S1_HEAD_PAD * sizeof(float));
    cudaMalloc(&pol->d_bh_m2, BR_S1_HEAD_PAD * sizeof(float));
    cudaMalloc(&pol->d_gbh,   BR_S1_HEAD_PAD * sizeof(float));
    cudaMemcpy(pol->d_bh_32, h_bh, sizeof(h_bh), cudaMemcpyHostToDevice);
    cudaMemset(pol->d_bh_m1, 0, BR_S1_HEAD_PAD * sizeof(float));
    cudaMemset(pol->d_bh_m2, 0, BR_S1_HEAD_PAD * sizeof(float));
    cudaMemset(pol->d_gbh,   0, BR_S1_HEAD_PAD * sizeof(float));

    cudaMalloc(&pol->d_adv_stats, 2 * sizeof(float));
    cudaMalloc(&pol->d_loss_acc,  5 * sizeof(float));

    br_policy_s1_sync_weights(pol);

    /* Build cuBLASLt plans */
    pol->p_f1_nv4 = s1_make_nvfp4_plan(pol->lt, H, Q, BRC_OBS_DIM, pol->d_W1_sf, pol->d_obs_sf, 1, pol->ws_bytes);
    pol->p_f2_nv4 = s1_make_nvfp4_plan(pol->lt, H, Q, H,           pol->d_W2_sf, pol->d_h1_sf,  1, pol->ws_bytes);
    pol->p_f1_f16 = s1_make_fp16_plan(pol->lt, CUBLAS_OP_T, CUBLAS_OP_N, H, Q, BRC_OBS_DIM, H, BRC_OBS_DIM, Q, CUDA_R_16F, 1, NULL, pol->ws_bytes);
    pol->p_f2_f16 = s1_make_fp16_plan(pol->lt, CUBLAS_OP_T, CUBLAS_OP_N, H, Q, H,           H, H,           Q, CUDA_R_16F, 1, NULL, pol->ws_bytes);

    /* Single packed FP16->FP32 head GEMM [112, H] * [H, Q] -> [112, Q] with fused FP32 bias */
    pol->p_fh_f16 = s1_make_fp16_plan(pol->lt, CUBLAS_OP_T, CUBLAS_OP_N,
                                      BR_S1_HEAD_PAD, Q, H, BR_S1_HEAD_PAD, H, Q,
                                      CUDA_R_32F, 0, pol->d_bh_32, pol->ws_bytes);
    if (pol->p_fh_f16.valid) {
        pol->head_bias_fused = 1;
    } else {
        s1_destroy_plan(&pol->p_fh_f16);
        pol->p_fh_f16 = s1_make_fp16_plan(pol->lt, CUBLAS_OP_T, CUBLAS_OP_N,
                                          BR_S1_HEAD_PAD, Q, H, BR_S1_HEAD_PAD, H, Q,
                                          CUDA_R_32F, 0, NULL, pol->ws_bytes);
        pol->head_bias_fused = 0;
    }

    /* Packed head backward plans (always FP16/FP32) */
    pol->p_bh_d = s1_make_fp16_plan(pol->lt, CUBLAS_OP_N, CUBLAS_OP_N,
                                    H, Q, H, BR_S1_HEAD_PAD, BR_S1_HEAD_PAD, Q,
                                    CUDA_R_16F, 0, NULL, pol->ws_bytes);
    pol->p_bh_w = s1_make_fp16_plan(pol->lt, CUBLAS_OP_N, CUBLAS_OP_T,
                                    H, BR_S1_HEAD_PAD, H, Q, BR_S1_HEAD_PAD, Q,
                                    CUDA_R_16F, 0, NULL, pol->ws_bytes);

    /* Trunk backward plans: W2 (FP16 | FP8 | NVFP4-SR), skinny W1 always FP16 */
    pol->p_b2_d_f16 = s1_make_fp16_plan(pol->lt, CUBLAS_OP_N, CUBLAS_OP_N, H, Q, H, H, H, Q, CUDA_R_16F, 0, NULL, pol->ws_bytes);
    pol->p_b2_w_f16 = s1_make_fp16_plan(pol->lt, CUBLAS_OP_N, CUBLAS_OP_T, H, H, H, Q, H, Q, CUDA_R_16F, 0, NULL, pol->ws_bytes);
    pol->p_b1_w_f16 = s1_make_fp16_plan(pol->lt, CUBLAS_OP_N, CUBLAS_OP_T, BRC_OBS_DIM, H, BRC_OBS_DIM, Q, H, Q, CUDA_R_16F, 0, NULL, pol->ws_bytes);

    pol->p_b2_d_f8  = s1_make_fp8_plan(pol->lt, H, Q, H, pol->d_fp8_sc + 0, pol->d_fp8_sc + 1, pol->ws_bytes);
    pol->p_b2_w_f8  = s1_make_fp8_plan(pol->lt, H, H, Q, pol->d_fp8_sc + 2, pol->d_fp8_sc + 1, pol->ws_bytes);

    pol->p_b2_d_nv4 = s1_make_nvfp4_plan(pol->lt, H, Q, H, pol->d_W2T_sf, pol->d_dh2_sf,  0, pol->ws_bytes);
    pol->p_b2_w_nv4 = s1_make_nvfp4_plan(pol->lt, H, H, Q, pol->d_h1T_sf, pol->d_dh2T_sf, 0, pol->ws_bytes);

    cudaStreamSynchronize(pol->stream);
    return pol;
}

void br_policy_s1_destroy(br_policy_s1_t *pol) {
    if (!pol) return;
    cudaStreamSynchronize(pol->stream);
    s1_destroy_plan(&pol->p_f1_nv4); s1_destroy_plan(&pol->p_f2_nv4);
    s1_destroy_plan(&pol->p_f1_f16); s1_destroy_plan(&pol->p_f2_f16);
    s1_destroy_plan(&pol->p_fh_f16);
    s1_destroy_plan(&pol->p_bh_d);   s1_destroy_plan(&pol->p_bh_w);
    s1_destroy_plan(&pol->p_b2_d_f16); s1_destroy_plan(&pol->p_b2_w_f16); s1_destroy_plan(&pol->p_b1_w_f16);
    s1_destroy_plan(&pol->p_b2_d_f8);  s1_destroy_plan(&pol->p_b2_w_f8);
    s1_destroy_plan(&pol->p_b2_d_nv4); s1_destroy_plan(&pol->p_b2_w_nv4);

    cudaFree(pol->d_ws);
    cudaFree(pol->d_obs_q); cudaFree(pol->d_obs_sf);
    cudaFree(pol->d_h1_q);  cudaFree(pol->d_h1_sf);
    cudaFree(pol->d_h1);    cudaFree(pol->d_h2);    cudaFree(pol->d_head_out);
    cudaFree(pol->d_dhead_16); cudaFree(pol->d_dh2); cudaFree(pol->d_dh1);

    cudaFree(pol->d_W2T_f8); cudaFree(pol->d_dh2_f8); cudaFree(pol->d_dh2T_f8); cudaFree(pol->d_h1T_f8);
    cudaFree(pol->d_fp8_sc); cudaFree(pol->d_fp8_nxt);

    cudaFree(pol->d_W2T_q);  cudaFree(pol->d_W2T_sf);
    cudaFree(pol->d_dh2_q);  cudaFree(pol->d_dh2_sf);
    cudaFree(pol->d_dh2T_q); cudaFree(pol->d_dh2T_sf);
    cudaFree(pol->d_h1T_q);  cudaFree(pol->d_h1T_sf);

    cudaFree(pol->d_W1_32); cudaFree(pol->d_W1_m1); cudaFree(pol->d_W1_m2);
    cudaFree(pol->d_W1_16); cudaFree(pol->d_gW1);   cudaFree(pol->d_W1_q); cudaFree(pol->d_W1_sf);

    cudaFree(pol->d_W2_32); cudaFree(pol->d_W2_m1); cudaFree(pol->d_W2_m2);
    cudaFree(pol->d_W2_16); cudaFree(pol->d_gW2);   cudaFree(pol->d_W2_q); cudaFree(pol->d_W2_sf);

    cudaFree(pol->d_Wh_32); cudaFree(pol->d_Wh_m1); cudaFree(pol->d_Wh_m2);
    cudaFree(pol->d_Wh_16); cudaFree(pol->d_gWh);
    cudaFree(pol->d_bh_32); cudaFree(pol->d_bh_m1); cudaFree(pol->d_bh_m2); cudaFree(pol->d_gbh);

    cudaFree(pol->d_adv_stats);
    cudaFree(pol->d_loss_acc);

    cublasLtDestroy(pol->lt);
    cudaStreamDestroy(pol->own_stream);
    free(pol);
}

void *br_policy_s1_stream(const br_policy_s1_t *pol) {
    return (void *)pol->stream;
}

void br_policy_s1_set_stream(br_policy_s1_t *pol, void *cuda_stream) {
    pol->stream = cuda_stream ? (cudaStream_t)cuda_stream : pol->own_stream;
}

const float *br_policy_s1_head_out_dev(const br_policy_s1_t *pol) {
    return pol->d_head_out;
}

void br_policy_s1_sync(br_policy_s1_t *pol) {
    cudaStreamSynchronize(pol->stream);
}

/* ============================================================================
 * Forward, Sampling, Backward Trunk & PPO Update API
 * ============================================================================ */
void br_policy_s1_forward(br_policy_s1_t *pol, const void *d_obs_f16) {
    const __half *obs = (const __half *)d_obs_f16;
    int Q = pol->cfg.batch_q, H = pol->cfg.hidden;
    float alpha = 1.0f, beta = 0.0f;

    if (pol->cfg.use_nvfp4_fwd && pol->p_f1_nv4.valid && pol->p_f2_nv4.valid) {
        k_s1_quant_f16_to_nvfp4_swz<<<(Q * BRC_OBS_DIM + 255) / 256, 256, 0, pol->stream>>>(
            obs, pol->d_obs_q, pol->d_obs_sf, Q, BRC_OBS_DIM, 0, 0);
        cublasLtMatmul(pol->lt, pol->p_f1_nv4.desc, &alpha,
                       pol->d_W1_q, pol->p_f1_nv4.lA, pol->d_obs_q, pol->p_f1_nv4.lB, &beta,
                       pol->d_h1, pol->p_f1_nv4.lD, pol->d_h1, pol->p_f1_nv4.lD,
                       &pol->p_f1_nv4.algo, pol->d_ws, pol->ws_bytes, pol->stream);

        k_s1_quant_f16_to_nvfp4_swz<<<(Q * H + 255) / 256, 256, 0, pol->stream>>>(
            pol->d_h1, pol->d_h1_q, pol->d_h1_sf, Q, H, 0, 0);
        cublasLtMatmul(pol->lt, pol->p_f2_nv4.desc, &alpha,
                       pol->d_W2_q, pol->p_f2_nv4.lA, pol->d_h1_q, pol->p_f2_nv4.lB, &beta,
                       pol->d_h2, pol->p_f2_nv4.lD, pol->d_h2, pol->p_f2_nv4.lD,
                       &pol->p_f2_nv4.algo, pol->d_ws, pol->ws_bytes, pol->stream);
    } else {
        cublasLtMatmul(pol->lt, pol->p_f1_f16.desc, &alpha,
                       pol->d_W1_16, pol->p_f1_f16.lA, obs, pol->p_f1_f16.lB, &beta,
                       pol->d_h1, pol->p_f1_f16.lD, pol->d_h1, pol->p_f1_f16.lD,
                       &pol->p_f1_f16.algo, pol->d_ws, pol->ws_bytes, pol->stream);
        cublasLtMatmul(pol->lt, pol->p_f2_f16.desc, &alpha,
                       pol->d_W2_16, pol->p_f2_f16.lA, pol->d_h1, pol->p_f2_f16.lB, &beta,
                       pol->d_h2, pol->p_f2_f16.lD, pol->d_h2, pol->p_f2_f16.lD,
                       &pol->p_f2_f16.algo, pol->d_ws, pol->ws_bytes, pol->stream);
    }

    /* Single packed FP16->FP32 head GEMM [112, H] * [H, Q] (+ bias) */
    cublasLtMatmul(pol->lt, pol->p_fh_f16.desc, &alpha,
                   pol->d_Wh_16, pol->p_fh_f16.lA, pol->d_h2, pol->p_fh_f16.lB, &beta,
                   pol->d_head_out, pol->p_fh_f16.lD, pol->d_head_out, pol->p_fh_f16.lD,
                   &pol->p_fh_f16.algo, pol->d_ws, pol->ws_bytes, pol->stream);
    if (!pol->head_bias_fused) {
        int tot = Q * BR_S1_HEAD_PAD;
        k_s1_add_bias<<<(tot + 255) / 256, 256, 0, pol->stream>>>(pol->d_head_out, pol->d_bh_32, Q);
    }
}

void br_policy_s1_act(br_policy_s1_t *pol,
                      const void *d_obs_f16,
                      brc_s1_action_t *d_act_out,
                      float *d_logp_out,
                      float *d_val_out,
                      uint64_t step_ctr,
                      int greedy) {
    br_policy_s1_forward(pol, d_obs_f16);
    int Q = pol->cfg.batch_q;
    k_s1_sample<<<(Q + S1_LW - 1) / S1_LW, S1_LW * 32, 0, pol->stream>>>(
        pol->d_head_out, d_act_out, d_logp_out, d_val_out, Q, pol->cfg.seed, step_ctr, greedy);
}

void br_policy_s1_adv_stats(br_policy_s1_t *pol,
                            const float *d_adv,
                            float *d_adv_stats_2f,
                            int total_samples) {
    cudaMemsetAsync(d_adv_stats_2f, 0, 2 * sizeof(float), pol->stream);
    k_s1_adv_stats<<<(total_samples + 255) / 256, 256, 0, pol->stream>>>(d_adv, d_adv_stats_2f, total_samples);
}

static void s1_run_backward_trunk(br_policy_s1_t *pol, const __half *obs, uint32_t sr_seed) {
    int Q = pol->cfg.batch_q, H = pol->cfg.hidden;
    float alpha_d = 1.0f;
    /* Keep gscale factor inside d_gW (4 extra bits above FP16 subnormals); AdamW unscales in FP32! */
    float alpha_w = 1.0f / (float)Q;
    float beta    = 0.0f;
    int relu_grd  = ((Q * H + 7) / 8 + 255) / 256;

    if (pol->cfg.bwd_prec == BR_S1_BWD_FP8 && pol->p_b2_d_f8.valid && pol->p_b2_w_f8.valid) {
        if (!pol->fp8_init) {
            cudaMemsetAsync(pol->d_fp8_sc,  0, 3 * sizeof(float), pol->stream);
            cudaMemsetAsync(pol->d_fp8_nxt, 0, 3 * sizeof(float), pol->stream);
            k_s1_absmax_f16<<<128, 256, 0, pol->stream>>>(pol->d_W2_16, pol->d_fp8_sc + 0, H * H);
            k_s1_absmax_f16<<<256, 256, 0, pol->stream>>>(pol->d_dh2,   pol->d_fp8_sc + 1, Q * H);
            k_s1_absmax_f16<<<256, 256, 0, pol->stream>>>(pol->d_h1,    pol->d_fp8_sc + 2, Q * H);
            pol->fp8_init = 1;
        }
        dim3 blkT(32, 8);
        k_s1_quant_trans_fp8<<<dim3((H + 31) / 32, (H + 31) / 32), blkT, 0, pol->stream>>>(
            pol->d_W2_16, pol->d_W2T_f8, pol->d_fp8_sc + 0, pol->d_fp8_nxt + 0, H, H);
        k_s1_quant_f16_to_fp8<<<(Q * H / 2 + 255) / 256, 256, 0, pol->stream>>>(
            pol->d_dh2, pol->d_dh2_f8, pol->d_fp8_sc + 1, pol->d_fp8_nxt + 1, Q * H);
        cublasLtMatmul(pol->lt, pol->p_b2_d_f8.desc, &alpha_d,
                       pol->d_W2T_f8, pol->p_b2_d_f8.lA, pol->d_dh2_f8, pol->p_b2_d_f8.lB, &beta,
                       pol->d_dh1, pol->p_b2_d_f8.lD, pol->d_dh1, pol->p_b2_d_f8.lD,
                       &pol->p_b2_d_f8.algo, pol->d_ws, pol->ws_bytes, pol->stream);

        k_s1_quant_trans_fp8<<<dim3((H + 31) / 32, (Q + 31) / 32), blkT, 0, pol->stream>>>(
            pol->d_h1, pol->d_h1T_f8, pol->d_fp8_sc + 2, pol->d_fp8_nxt + 2, Q, H);
        k_s1_quant_trans_fp8<<<dim3((H + 31) / 32, (Q + 31) / 32), blkT, 0, pol->stream>>>(
            pol->d_dh2, pol->d_dh2T_f8, pol->d_fp8_sc + 1, NULL, Q, H);
        cublasLtMatmul(pol->lt, pol->p_b2_w_f8.desc, &alpha_w,
                       pol->d_h1T_f8, pol->p_b2_w_f8.lA, pol->d_dh2T_f8, pol->p_b2_w_f8.lB, &beta,
                       pol->d_gW2, pol->p_b2_w_f8.lD, pol->d_gW2, pol->p_b2_w_f8.lD,
                       &pol->p_b2_w_f8.algo, pol->d_ws, pol->ws_bytes, pol->stream);
        k_s1_fp8_swap_scales<<<1, 32, 0, pol->stream>>>(pol->d_fp8_sc, pol->d_fp8_nxt);
    } else if (pol->cfg.bwd_prec == BR_S1_BWD_NVFP4 && pol->p_b2_d_nv4.valid && pol->p_b2_w_nv4.valid) {
        dim3 blk4(32, 4);
        k_s1_quant_trans_nvfp4_swz<<<dim3((H + 31) / 32, (H + 63) / 64), blk4, 0, pol->stream>>>(
            pol->d_W2_16, pol->d_W2T_q, pol->d_W2T_sf, H, H, 0, sr_seed);
        k_s1_quant_f16_to_nvfp4_swz<<<(Q * H + 255) / 256, 256, 0, pol->stream>>>(
            pol->d_dh2, pol->d_dh2_q, pol->d_dh2_sf, Q, H, 1, sr_seed + 1u);
        cublasLtMatmul(pol->lt, pol->p_b2_d_nv4.desc, &alpha_d,
                       pol->d_W2T_q, pol->p_b2_d_nv4.lA, pol->d_dh2_q, pol->p_b2_d_nv4.lB, &beta,
                       pol->d_dh1, pol->p_b2_d_nv4.lD, pol->d_dh1, pol->p_b2_d_nv4.lD,
                       &pol->p_b2_d_nv4.algo, pol->d_ws, pol->ws_bytes, pol->stream);

        k_s1_quant_trans_nvfp4_swz<<<dim3((H + 31) / 32, (Q + 63) / 64), blk4, 0, pol->stream>>>(
            pol->d_h1, pol->d_h1T_q, pol->d_h1T_sf, Q, H, 0, sr_seed + 2u);
        k_s1_quant_trans_nvfp4_swz<<<dim3((H + 31) / 32, (Q + 63) / 64), blk4, 0, pol->stream>>>(
            pol->d_dh2, pol->d_dh2T_q, pol->d_dh2T_sf, Q, H, 1, sr_seed + 3u);
        cublasLtMatmul(pol->lt, pol->p_b2_w_nv4.desc, &alpha_w,
                       pol->d_h1T_q, pol->p_b2_w_nv4.lA, pol->d_dh2T_q, pol->p_b2_w_nv4.lB, &beta,
                       pol->d_gW2, pol->p_b2_w_nv4.lD, pol->d_gW2, pol->p_b2_w_nv4.lD,
                       &pol->p_b2_w_nv4.algo, pol->d_ws, pol->ws_bytes, pol->stream);
    } else {
        cublasLtMatmul(pol->lt, pol->p_b2_d_f16.desc, &alpha_d,
                       pol->d_W2_16, pol->p_b2_d_f16.lA, pol->d_dh2, pol->p_b2_d_f16.lB, &beta,
                       pol->d_dh1, pol->p_b2_d_f16.lD, pol->d_dh1, pol->p_b2_d_f16.lD,
                       &pol->p_b2_d_f16.algo, pol->d_ws, pol->ws_bytes, pol->stream);
        cublasLtMatmul(pol->lt, pol->p_b2_w_f16.desc, &alpha_w,
                       pol->d_h1, pol->p_b2_w_f16.lA, pol->d_dh2, pol->p_b2_w_f16.lB, &beta,
                       pol->d_gW2, pol->p_b2_w_f16.lD, pol->d_gW2, pol->p_b2_w_f16.lD,
                       &pol->p_b2_w_f16.algo, pol->d_ws, pol->ws_bytes, pol->stream);
    }

    /* ReLU backward on dH1 + skinny input layer W1 wgrad always in FP16 */
    k_s1_relu_bwd<<<relu_grd, 256, 0, pol->stream>>>(pol->d_dh1, pol->d_h1, Q * H);
    cublasLtMatmul(pol->lt, pol->p_b1_w_f16.desc, &alpha_w,
                   obs, pol->p_b1_w_f16.lA, pol->d_dh1, pol->p_b1_w_f16.lB, &beta,
                   pol->d_gW1, pol->p_b1_w_f16.lD, pol->d_gW1, pol->p_b1_w_f16.lD,
                   &pol->p_b1_w_f16.algo, pol->d_ws, pol->ws_bytes, pol->stream);
}

void br_policy_s1_ppo_minibatch(br_policy_s1_t *pol,
                                const void *d_obs_f16,
                                const brc_s1_action_t *d_acts,
                                const float *d_old_logp,
                                const float *d_adv,
                                const float *d_ret,
                                const float *d_adv_stats_2f,
                                int total_samples,
                                uint32_t sr_seed,
                                float *d_loss_acc_5f) {
    const __half *obs = (const __half *)d_obs_f16;
    int Q = pol->cfg.batch_q, H = pol->cfg.hidden;

    br_policy_s1_forward(pol, obs);

    cudaMemsetAsync(pol->d_gbh, 0, BR_S1_HEAD_PAD * sizeof(float), pol->stream);
    k_s1_ppo_loss_grad<<<(Q + S1_LW - 1) / S1_LW, S1_LW * 32, 0, pol->stream>>>(
        pol->d_head_out, d_acts, d_old_logp, d_adv, d_ret, d_adv_stats_2f, total_samples,
        pol->d_dhead_16, NULL, pol->d_gbh, d_loss_acc_5f,
        Q, pol->cfg.clip_eps, pol->cfg.vf_coef, pol->cfg.ent_coef, pol->cfg.grad_loss_scale);

    /* Packed output heads backward (all 107 rows: cam + keys + hotbar + value!) in FP16 */
    float alpha = 1.0f, alpha_w = 1.0f / (float)Q, beta = 0.0f;
    cublasLtMatmul(pol->lt, pol->p_bh_d.desc, &alpha,
                   pol->d_Wh_16, pol->p_bh_d.lA, pol->d_dhead_16, pol->p_bh_d.lB, &beta,
                   pol->d_dh2, pol->p_bh_d.lD, pol->d_dh2, pol->p_bh_d.lD,
                   &pol->p_bh_d.algo, pol->d_ws, pol->ws_bytes, pol->stream);
    cublasLtMatmul(pol->lt, pol->p_bh_w.desc, &alpha_w,
                   pol->d_h2, pol->p_bh_w.lA, pol->d_dhead_16, pol->p_bh_w.lB, &beta,
                   pol->d_gWh, pol->p_bh_w.lD, pol->d_gWh, pol->p_bh_w.lD,
                   &pol->p_bh_w.algo, pol->d_ws, pol->ws_bytes, pol->stream);

    int relu_grd = ((Q * H + 7) / 8 + 255) / 256;
    k_s1_relu_bwd<<<relu_grd, 256, 0, pol->stream>>>(pol->d_dh2, pol->d_h2, Q * H);

    s1_run_backward_trunk(pol, obs, sr_seed);

    /* Warp-coalesced AdamW + NVFP4 requant */
    pol->adam_step++;
    float bc1 = 1.0f / (1.0f - powf(0.9f, (float)pol->adam_step));
    float bc2 = 1.0f / (1.0f - powf(0.999f, (float)pol->adam_step));
    float inv_gs = 1.0f / pol->cfg.grad_loss_scale;
    int req = pol->cfg.use_nvfp4_fwd;

    k_s1_adamw_requant_swz<<<(H * BRC_OBS_DIM + 255) / 256, 256, 0, pol->stream>>>(
        pol->d_W1_32, pol->d_W1_m1, pol->d_W1_m2, pol->d_gW1, pol->d_W1_16,
        pol->d_W1_q, pol->d_W1_sf, H, BRC_OBS_DIM, H,
        pol->cfg.lr, pol->cfg.weight_decay, pol->cfg.grad_clip, inv_gs, bc1, bc2, req);
    k_s1_adamw_requant_swz<<<(H * H + 255) / 256, 256, 0, pol->stream>>>(
        pol->d_W2_32, pol->d_W2_m1, pol->d_W2_m2, pol->d_gW2, pol->d_W2_16,
        pol->d_W2_q, pol->d_W2_sf, H, H, H,
        pol->cfg.lr, pol->cfg.weight_decay, pol->cfg.grad_clip, inv_gs, bc1, bc2, req);
    k_s1_adamw_requant_swz<<<(BR_S1_HEAD_PAD * H + 255) / 256, 256, 0, pol->stream>>>(
        pol->d_Wh_32, pol->d_Wh_m1, pol->d_Wh_m2, pol->d_gWh, pol->d_Wh_16,
        NULL, NULL, BR_S1_HEAD_PAD, H, BR_S1_HEAD_DIM,
        pol->cfg.lr, pol->cfg.weight_decay, pol->cfg.grad_clip, inv_gs, bc1, bc2, 0);
    k_s1_adamw_bias<<<1, 128, 0, pol->stream>>>(
        pol->d_bh_32, pol->d_bh_m1, pol->d_bh_m2, pol->d_gbh,
        pol->cfg.lr, pol->cfg.grad_clip, bc1, bc2);
}

void br_policy_s1_ppo_update(br_policy_s1_t *pol,
                             const void *d_obs_f16,
                             const brc_s1_action_t *d_acts,
                             const float *d_old_logp,
                             const float *d_adv,
                             const float *d_ret,
                             int total_samples,
                             int ppo_epochs,
                             uint32_t iter_seed,
                             br_policy_s1_stats_t *out_stats) {
    if (out_stats) memset(out_stats, 0, sizeof(*out_stats));
    int Q = pol->cfg.batch_q;
    int n_mb = total_samples / Q;
    if (n_mb <= 0 || ppo_epochs <= 0) return;

    cudaMemsetAsync(pol->d_loss_acc, 0, 5 * sizeof(float), pol->stream);
    br_policy_s1_adv_stats(pol, d_adv, pol->d_adv_stats, total_samples);
    cudaStreamSynchronize(pol->stream);

    double t0 = s1_now_ns();
    const __half *obs_base = (const __half *)d_obs_f16;
    for (int ep = 0; ep < ppo_epochs; ep++) {
        for (int mb = 0; mb < n_mb; mb++) {
            size_t off = (size_t)mb * Q;
            uint32_t sr_seed = iter_seed * 997u + (uint32_t)ep * 31u + (uint32_t)mb * 7u;
            br_policy_s1_ppo_minibatch(pol,
                                       obs_base + off * BRC_OBS_DIM,
                                       d_acts + off,
                                       d_old_logp + off,
                                       d_adv + off,
                                       d_ret + off,
                                       pol->d_adv_stats,
                                       total_samples,
                                       sr_seed,
                                       pol->d_loss_acc);
        }
    }
    cudaStreamSynchronize(pol->stream);
    double ms = (s1_now_ns() - t0) / 1e6;

    if (out_stats) {
        float acc[5] = {0};
        cudaMemcpy(acc, pol->d_loss_acc, sizeof(acc), cudaMemcpyDeviceToHost);
        double denom = (double)ppo_epochs * (double)(n_mb * Q);
        out_stats->policy_loss = (double)acc[0] / denom;
        out_stats->value_loss  = (double)acc[1] / denom;
        out_stats->entropy     = (double)acc[2] / denom;
        out_stats->approx_kl   = (double)acc[3] / denom;
        out_stats->clip_frac   = (double)acc[4] / denom;
        out_stats->update_ms   = ms;
    }
}

/* ============================================================================
 * BRT1 Teacher Checkpoint Save & Load (exact csrc/bedrock/br_contract.h)
 * ============================================================================ */
static int s1_fwrite_all(FILE *fp, const void *buf, size_t bytes) {
    return (fwrite(buf, 1, bytes, fp) == bytes) ? 0 : -1;
}

static int s1_fread_all(FILE *fp, void *buf, size_t bytes) {
    return (fread(buf, 1, bytes, fp) == bytes) ? 0 : -1;
}

int br_policy_s1_save(const br_policy_s1_t *pol, const char *path) {
    if (!pol || !path) return -1;
    cudaStreamSynchronize(pol->stream);

    int H = pol->cfg.hidden;
    brc_teacher_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic    = BRC_CKPT_MAGIC;
    hdr.version  = 1u;
    hdr.obs_dim  = BRC_OBS_DIM;
    hdr.hidden   = (uint32_t)H;
    hdr.cam_bins = BRC_CAM_BINS;
    hdr.n_keys   = BRC_KEYS;
    hdr.n_hotbar = BRC_HOTBAR_CATS;
    hdr.reserved = 0u;

    size_t nW1 = (size_t)H * BRC_OBS_DIM;
    size_t nW2 = (size_t)H * H;
    size_t nWh = (size_t)BR_S1_HEAD_DIM * H;
    float *hW1 = (float *)malloc(nW1 * sizeof(float));
    float *hW2 = (float *)malloc(nW2 * sizeof(float));
    float *hWh = (float *)malloc(nWh * sizeof(float));
    float  hbh[BR_S1_HEAD_DIM];
    if (!hW1 || !hW2 || !hWh) { free(hW1); free(hW2); free(hWh); return -2; }

    cudaMemcpy(hW1, pol->d_W1_32, nW1 * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(hW2, pol->d_W2_32, nW2 * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(hWh, pol->d_Wh_32, nWh * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(hbh, pol->d_bh_32, sizeof(hbh),         cudaMemcpyDeviceToHost);

    FILE *fp = fopen(path, "wb");
    if (!fp) { free(hW1); free(hW2); free(hWh); return -3; }

    int err = 0;
    err |= s1_fwrite_all(fp, &hdr, sizeof(hdr));
    err |= s1_fwrite_all(fp, hW1, nW1 * sizeof(float));
    err |= s1_fwrite_all(fp, hW2, nW2 * sizeof(float));
    /* W_cam [82 * H], b_cam [82] */
    err |= s1_fwrite_all(fp, hWh + (size_t)BR_S1_HEAD_CAM_OFF * H, (size_t)BR_S1_HEAD_CAM_DIM * H * sizeof(float));
    err |= s1_fwrite_all(fp, hbh + BR_S1_HEAD_CAM_OFF, BR_S1_HEAD_CAM_DIM * sizeof(float));
    /* W_keys [14 * H], b_keys [14] */
    err |= s1_fwrite_all(fp, hWh + (size_t)BR_S1_HEAD_KEY_OFF * H, (size_t)BR_S1_HEAD_KEY_DIM * H * sizeof(float));
    err |= s1_fwrite_all(fp, hbh + BR_S1_HEAD_KEY_OFF, BR_S1_HEAD_KEY_DIM * sizeof(float));
    /* W_hot [10 * H], b_hot [10] */
    err |= s1_fwrite_all(fp, hWh + (size_t)BR_S1_HEAD_HOT_OFF * H, (size_t)BR_S1_HEAD_HOT_DIM * H * sizeof(float));
    err |= s1_fwrite_all(fp, hbh + BR_S1_HEAD_HOT_OFF, BR_S1_HEAD_HOT_DIM * sizeof(float));
    /* W_val [1 * H], b_val [1] */
    err |= s1_fwrite_all(fp, hWh + (size_t)BR_S1_HEAD_VAL_OFF * H, (size_t)H * sizeof(float));
    err |= s1_fwrite_all(fp, hbh + BR_S1_HEAD_VAL_OFF, sizeof(float));

    if (fclose(fp) != 0) err = -4;
    free(hW1); free(hW2); free(hWh);
    return err;
}

int br_policy_s1_load(br_policy_s1_t *pol, const char *path) {
    if (!pol || !path) return -1;
    FILE *fp = fopen(path, "rb");
    if (!fp) return -2;

    brc_teacher_hdr_t hdr;
    if (s1_fread_all(fp, &hdr, sizeof(hdr)) != 0) { fclose(fp); return -3; }
    if (hdr.magic    != BRC_CKPT_MAGIC ||
        hdr.version  != 1u ||
        hdr.obs_dim  != BRC_OBS_DIM ||
        hdr.hidden   != (uint32_t)pol->cfg.hidden ||
        hdr.cam_bins != BRC_CAM_BINS ||
        hdr.n_keys   != BRC_KEYS ||
        hdr.n_hotbar != BRC_HOTBAR_CATS) {
        fclose(fp);
        return -4;
    }

    int H = pol->cfg.hidden;
    size_t nW1 = (size_t)H * BRC_OBS_DIM;
    size_t nW2 = (size_t)H * H;
    size_t nWh_pad = (size_t)BR_S1_HEAD_PAD * H;
    float *hW1 = (float *)malloc(nW1 * sizeof(float));
    float *hW2 = (float *)malloc(nW2 * sizeof(float));
    float *hWh = (float *)calloc(nWh_pad, sizeof(float));
    float  hbh[BR_S1_HEAD_PAD];
    memset(hbh, 0, sizeof(hbh));
    if (!hW1 || !hW2 || !hWh) { free(hW1); free(hW2); free(hWh); fclose(fp); return -5; }

    int err = 0;
    err |= s1_fread_all(fp, hW1, nW1 * sizeof(float));
    err |= s1_fread_all(fp, hW2, nW2 * sizeof(float));
    err |= s1_fread_all(fp, hWh + (size_t)BR_S1_HEAD_CAM_OFF * H, (size_t)BR_S1_HEAD_CAM_DIM * H * sizeof(float));
    err |= s1_fread_all(fp, hbh + BR_S1_HEAD_CAM_OFF, BR_S1_HEAD_CAM_DIM * sizeof(float));
    err |= s1_fread_all(fp, hWh + (size_t)BR_S1_HEAD_KEY_OFF * H, (size_t)BR_S1_HEAD_KEY_DIM * H * sizeof(float));
    err |= s1_fread_all(fp, hbh + BR_S1_HEAD_KEY_OFF, BR_S1_HEAD_KEY_DIM * sizeof(float));
    err |= s1_fread_all(fp, hWh + (size_t)BR_S1_HEAD_HOT_OFF * H, (size_t)BR_S1_HEAD_HOT_DIM * H * sizeof(float));
    err |= s1_fread_all(fp, hbh + BR_S1_HEAD_HOT_OFF, BR_S1_HEAD_HOT_DIM * sizeof(float));
    err |= s1_fread_all(fp, hWh + (size_t)BR_S1_HEAD_VAL_OFF * H, (size_t)H * sizeof(float));
    err |= s1_fread_all(fp, hbh + BR_S1_HEAD_VAL_OFF, sizeof(float));
    fclose(fp);

    if (err != 0) { free(hW1); free(hW2); free(hWh); return -6; }

    cudaStreamSynchronize(pol->stream);
    cudaMemcpy(pol->d_W1_32, hW1, nW1 * sizeof(float),     cudaMemcpyHostToDevice);
    cudaMemcpy(pol->d_W2_32, hW2, nW2 * sizeof(float),     cudaMemcpyHostToDevice);
    cudaMemcpy(pol->d_Wh_32, hWh, nWh_pad * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(pol->d_bh_32, hbh, sizeof(hbh),             cudaMemcpyHostToDevice);

    /* Reset optimizer & FP8 delayed scaling state */
    cudaMemsetAsync(pol->d_W1_m1, 0, nW1 * sizeof(float),     pol->stream);
    cudaMemsetAsync(pol->d_W1_m2, 0, nW1 * sizeof(float),     pol->stream);
    cudaMemsetAsync(pol->d_W2_m1, 0, nW2 * sizeof(float),     pol->stream);
    cudaMemsetAsync(pol->d_W2_m2, 0, nW2 * sizeof(float),     pol->stream);
    cudaMemsetAsync(pol->d_Wh_m1, 0, nWh_pad * sizeof(float), pol->stream);
    cudaMemsetAsync(pol->d_Wh_m2, 0, nWh_pad * sizeof(float), pol->stream);
    cudaMemsetAsync(pol->d_bh_m1, 0, sizeof(hbh),             pol->stream);
    cudaMemsetAsync(pol->d_bh_m2, 0, sizeof(hbh),             pol->stream);
    cudaMemsetAsync(pol->d_fp8_sc,  0, 3 * sizeof(float),     pol->stream);
    cudaMemsetAsync(pol->d_fp8_nxt, 0, 3 * sizeof(float),     pol->stream);
    pol->adam_step = 0;
    pol->fp8_init  = 0;

    /* Automatically sync FP16 + swizzled NVFP4 device weights */
    br_policy_s1_sync_weights(pol);
    cudaStreamSynchronize(pol->stream);

    free(hW1); free(hW2); free(hWh);
    return 0;
}

/* ============================================================================
 * Standalone Verification Harness (-DBR_POLICY_S1_TEST)
 *   Test 1: Exact net.h / actor.cu logp, entropy & PPO gradient parity (FP64 ref + finite diff)
 *   Test 2: BRT1 checkpoint save/load round-trip (bit-exact FP32 + <=1e-3 logits/value)
 *   Test 3: End-to-end 17-dim factored PPO convergence + throughput (Q=8192, H=1024)
 * ============================================================================ */
#ifdef BR_POLICY_S1_TEST

static double cpu_logsig_f64(double x) {
    return fmin(x, 0.0) - log1p(exp(-fabs(x)));
}

/* Exact CPU FP64 evaluation of net.h / learn.cu factored s1 loss for a single sample */
static double cpu_s1_sample_loss_f64(
    const double *h, const brc_s1_action_t *act, double old_lp, double adv, double ret,
    double clip_eps, double vf_coef, double ent_coef,
    double *out_lp, double *out_ent, double *out_pl, double *out_vl, double *out_grad
) {
    double lp = 0.0, ent = 0.0;
    double pa[2][BRC_CAM_BINS], la[2][BRC_CAM_BINS], Hc[2];
    for (int c = 0; c < 2; c++) {
        const double *lh = h + c * BRC_CAM_BINS;
        double mx = -1e300;
        for (int j = 0; j < BRC_CAM_BINS; j++) if (lh[j] > mx) mx = lh[j];
        double se = 0.0;
        for (int j = 0; j < BRC_CAM_BINS; j++) se += exp(lh[j] - mx);
        double lse = log(se);
        Hc[c] = 0.0;
        for (int j = 0; j < BRC_CAM_BINS; j++) {
            la[c][j] = (lh[j] - mx) - lse;
            pa[c][j] = exp(la[c][j]);
            Hc[c] -= pa[c][j] * la[c][j];
        }
        lp  += la[c][act->a[c]];
        ent += Hc[c];
    }

    double pk[BRC_KEYS];
    for (int k = 0; k < BRC_KEYS; k++) {
        double x = h[BR_S1_HEAD_KEY_OFF + k];
        double ak = (act->a[2 + k] != 0) ? 1.0 : 0.0;
        double lsp = cpu_logsig_f64(x);
        double lsn = cpu_logsig_f64(-x);
        pk[k] = 1.0 / (1.0 + exp(-x));
        lp  += ak * lsp + (1.0 - ak) * lsn;
        ent -= pk[k] * lsp + (1.0 - pk[k]) * lsn;
    }

    double ph[BRC_HOTBAR_CATS], lh_hot[BRC_HOTBAR_CATS], Hh = 0.0;
    {
        const double *xh = h + BR_S1_HEAD_HOT_OFF;
        double mh = -1e300;
        for (int j = 0; j < BRC_HOTBAR_CATS; j++) if (xh[j] > mh) mh = xh[j];
        double sh = 0.0;
        for (int j = 0; j < BRC_HOTBAR_CATS; j++) sh += exp(xh[j] - mh);
        double lseh = log(sh);
        for (int j = 0; j < BRC_HOTBAR_CATS; j++) {
            lh_hot[j] = (xh[j] - mh) - lseh;
            ph[j] = exp(lh_hot[j]);
            Hh -= ph[j] * lh_hot[j];
        }
        lp  += lh_hot[act->a[16]];
        ent += Hh;
    }

    double diff  = fmin(fmax(lp - old_lp, -10.0), 10.0);
    double ratio = exp(diff);
    double lo = 1.0 - clip_eps, hi = 1.0 + clip_eps;
    double rc = fmin(fmax(ratio, lo), hi);
    double s1 = ratio * adv, s2 = rc * adv;
    double pl = -fmin(s1, s2);
    double dv = h[BR_S1_HEAD_VAL_OFF] - ret;
    double vl = 0.5 * dv * dv;

    if (out_lp)  *out_lp  = lp;
    if (out_ent) *out_ent = ent;
    if (out_pl)  *out_pl  = pl;
    if (out_vl)  *out_vl  = vl;

    if (out_grad) {
        double w1 = (s1 < s2) ? 1.0 : ((s1 == s2) ? 0.5 : 0.0);
        double w2 = 1.0 - w1;
        double inr = (ratio >= lo && ratio <= hi) ? 1.0 : 0.0;
        double glp = -(w1 + w2 * inr) * ratio * adv;
        double gE  = -ent_coef;
        for (int c = 0; c < 2; c++) {
            for (int j = 0; j < BRC_CAM_BINS; j++) {
                double ind = (j == act->a[c]) ? 1.0 : 0.0;
                out_grad[c * BRC_CAM_BINS + j] = glp * (ind - pa[c][j]) + gE * (-pa[c][j] * (la[c][j] + Hc[c]));
            }
        }
        for (int k = 0; k < BRC_KEYS; k++) {
            double x = h[BR_S1_HEAD_KEY_OFF + k];
            double ak = (act->a[2 + k] != 0) ? 1.0 : 0.0;
            out_grad[BR_S1_HEAD_KEY_OFF + k] = glp * (ak - pk[k]) + gE * (-x * pk[k] * (1.0 - pk[k]));
        }
        for (int j = 0; j < BRC_HOTBAR_CATS; j++) {
            double ind = (j == act->a[16]) ? 1.0 : 0.0;
            out_grad[BR_S1_HEAD_HOT_OFF + j] = glp * (ind - ph[j]) + gE * (-ph[j] * (lh_hot[j] + Hh));
        }
        out_grad[BR_S1_HEAD_VAL_OFF] = vf_coef * dv;
    }

    return pl + vf_coef * vl - ent_coef * ent;
}

static int run_test1_parity(void) {
    printf("=== TEST 1: Exact net.h / actor.cu Factored s1 Logp, Entropy & Grad Parity ===\n");
    const int B = 1024;
    const float clip_eps = 0.2f, vf_coef = 0.5f, ent_coef = 0.01f, gscale = 16.0f;

    float *h_head  = (float *)calloc((size_t)B * BR_S1_HEAD_PAD, sizeof(float));
    float *h_oldlp = (float *)malloc((size_t)B * sizeof(float));
    float *h_adv   = (float *)malloc((size_t)B * sizeof(float));
    float *h_ret   = (float *)malloc((size_t)B * sizeof(float));
    uint64_t rng = 0xDEADBEEF12345678ULL;

    for (int i = 0; i < B; i++) {
        float scale = (i % 4 == 0) ? 8.0f : ((i % 4 == 1) ? 0.2f : 2.5f);
        for (int j = 0; j < BR_S1_HEAD_DIM; j++) {
            float u = ((float)(s1_sm64(&rng) & 0xFFFFu) / 32768.0f) - 1.0f;
            h_head[(size_t)i * BR_S1_HEAD_PAD + j] = u * scale;
        }
        h_adv[i] = (((float)(s1_sm64(&rng) & 0xFFFFu) / 32768.0f) - 1.0f) * 2.5f;
        h_ret[i] = (((float)(s1_sm64(&rng) & 0xFFFFu) / 32768.0f) - 1.0f) * 4.0f;
    }

    float *d_head, *d_lp, *d_val, *d_oldlp, *d_adv, *d_ret, *d_dh32, *d_gbh, *d_lacc;
    __half *d_dh16;
    brc_s1_action_t *d_acts;
    cudaMalloc(&d_head,  (size_t)B * BR_S1_HEAD_PAD * sizeof(float));
    cudaMalloc(&d_acts,  (size_t)B * sizeof(brc_s1_action_t));
    cudaMalloc(&d_lp,    (size_t)B * sizeof(float));
    cudaMalloc(&d_val,   (size_t)B * sizeof(float));
    cudaMalloc(&d_oldlp, (size_t)B * sizeof(float));
    cudaMalloc(&d_adv,   (size_t)B * sizeof(float));
    cudaMalloc(&d_ret,   (size_t)B * sizeof(float));
    cudaMalloc(&d_dh16,  (size_t)B * BR_S1_HEAD_PAD * sizeof(__half));
    cudaMalloc(&d_dh32,  (size_t)B * BR_S1_HEAD_PAD * sizeof(float));
    cudaMalloc(&d_gbh,   BR_S1_HEAD_PAD * sizeof(float));
    cudaMalloc(&d_lacc,  5 * sizeof(float));

    cudaMemcpy(d_head, h_head, (size_t)B * BR_S1_HEAD_PAD * sizeof(float), cudaMemcpyHostToDevice);
    k_s1_sample<<<(B + S1_LW - 1) / S1_LW, S1_LW * 32>>>(d_head, d_acts, d_lp, d_val, B, 0xCAFEBABEULL, 7ULL, 0);
    cudaDeviceSynchronize();

    brc_s1_action_t *h_acts = (brc_s1_action_t *)malloc((size_t)B * sizeof(brc_s1_action_t));
    float *h_lp  = (float *)malloc((size_t)B * sizeof(float));
    float *h_val = (float *)malloc((size_t)B * sizeof(float));
    cudaMemcpy(h_acts, d_acts, (size_t)B * sizeof(brc_s1_action_t), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_lp,   d_lp,   (size_t)B * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_val,  d_val,  (size_t)B * sizeof(float), cudaMemcpyDeviceToHost);

    /* Perturb old_logp around true logp so both clipped and unclipped PPO branches fire */
    int range_bad = 0;
    for (int i = 0; i < B; i++) {
        if (h_acts[i].a[0] < 0 || h_acts[i].a[0] >= BRC_CAM_BINS) range_bad++;
        if (h_acts[i].a[1] < 0 || h_acts[i].a[1] >= BRC_CAM_BINS) range_bad++;
        for (int k = 0; k < BRC_KEYS; k++) if (h_acts[i].a[2 + k] != 0 && h_acts[i].a[2 + k] != 1) range_bad++;
        if (h_acts[i].a[16] < 0 || h_acts[i].a[16] >= BRC_HOTBAR_CATS) range_bad++;
        float delta = (((float)(s1_sm64(&rng) & 0xFFFFu) / 32768.0f) - 1.0f) * 0.45f;
        /* Avoid exact clip boundary where FP32 vs FP64 could straddle the step discontinuity */
        if (fabsf(fabsf(expf(delta) - 1.0f) - clip_eps) < 0.01f) delta += 0.05f;
        h_oldlp[i] = h_lp[i] - delta;
    }

    cudaMemcpy(d_oldlp, h_oldlp, (size_t)B * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(d_adv,   h_adv,   (size_t)B * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(d_ret,   h_ret,   (size_t)B * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemset(d_gbh,   0, BR_S1_HEAD_PAD * sizeof(float));
    cudaMemset(d_lacc,  0, 5 * sizeof(float));

    k_s1_ppo_loss_grad<<<(B + S1_LW - 1) / S1_LW, S1_LW * 32>>>(
        d_head, d_acts, d_oldlp, d_adv, d_ret, NULL, B,
        d_dh16, d_dh32, d_gbh, d_lacc, B, clip_eps, vf_coef, ent_coef, gscale);
    cudaDeviceSynchronize();

    float  *h_dh32 = (float *)malloc((size_t)B * BR_S1_HEAD_PAD * sizeof(float));
    __half *h_dh16 = (__half *)malloc((size_t)B * BR_S1_HEAD_PAD * sizeof(__half));
    float   h_gbh[BR_S1_HEAD_PAD], h_lacc[5];
    cudaMemcpy(h_dh32, d_dh32, (size_t)B * BR_S1_HEAD_PAD * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_dh16, d_dh16, (size_t)B * BR_S1_HEAD_PAD * sizeof(__half), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_gbh,  d_gbh,  sizeof(h_gbh), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_lacc, d_lacc, sizeof(h_lacc), cudaMemcpyDeviceToHost);

    double max_lp_err = 0.0, max_val_err = 0.0, max_grad_err = 0.0, max_pad_err = 0.0;
    double pl_ref_sum = 0.0, vl_ref_sum = 0.0, ent_ref_sum = 0.0;
    double gb_ref[BR_S1_HEAD_DIM];
    memset(gb_ref, 0, sizeof(gb_ref));

    for (int i = 0; i < B; i++) {
        double hd[BR_S1_HEAD_DIM], gd[BR_S1_HEAD_DIM];
        for (int j = 0; j < BR_S1_HEAD_DIM; j++) hd[j] = (double)h_head[(size_t)i * BR_S1_HEAD_PAD + j];
        double lp_r, ent_r, pl_r, vl_r;
        cpu_s1_sample_loss_f64(hd, &h_acts[i], (double)h_oldlp[i], (double)h_adv[i], (double)h_ret[i],
                               (double)clip_eps, (double)vf_coef, (double)ent_coef,
                               &lp_r, &ent_r, &pl_r, &vl_r, gd);
        max_lp_err  = fmax(max_lp_err,  fabs((double)h_lp[i]  - lp_r));
        max_val_err = fmax(max_val_err, fabs((double)h_val[i] - hd[BR_S1_HEAD_VAL_OFF]));
        pl_ref_sum  += pl_r;
        vl_ref_sum  += vl_r;
        ent_ref_sum += ent_r;
        for (int j = 0; j < BR_S1_HEAD_DIM; j++) {
            double e = fabs((double)h_dh32[(size_t)i * BR_S1_HEAD_PAD + j] - gd[j]);
            if (e > max_grad_err) max_grad_err = e;
            gb_ref[j] += gd[j] / (double)B;
        }
        for (int j = BR_S1_HEAD_DIM; j < BR_S1_HEAD_PAD; j++) {
            max_pad_err = fmax(max_pad_err, fabs((double)__half2float(h_dh16[(size_t)i * BR_S1_HEAD_PAD + j])));
        }
    }

    double max_gb_err = 0.0;
    for (int j = 0; j < BR_S1_HEAD_DIM; j++) max_gb_err = fmax(max_gb_err, fabs((double)h_gbh[j] - gb_ref[j]));
    double pl_err  = fabs((double)h_lacc[0] / B - pl_ref_sum / B);
    double vl_err  = fabs((double)h_lacc[1] / B - vl_ref_sum / B);
    double ent_err = fabs((double)h_lacc[2] / B - ent_ref_sum / B);

    /* Finite-difference verification on sample 0 across all 107 logits */
    double max_fd_err = 0.0;
    {
        double hd[BR_S1_HEAD_DIM];
        for (int j = 0; j < BR_S1_HEAD_DIM; j++) hd[j] = (double)h_head[j];
        const double eps = 1e-6;
        for (int j = 0; j < BR_S1_HEAD_DIM; j++) {
            double orig = hd[j];
            hd[j] = orig + eps;
            double lp_p = cpu_s1_sample_loss_f64(hd, &h_acts[0], h_oldlp[0], h_adv[0], h_ret[0],
                                                 clip_eps, vf_coef, ent_coef, NULL, NULL, NULL, NULL, NULL);
            hd[j] = orig - eps;
            double lp_m = cpu_s1_sample_loss_f64(hd, &h_acts[0], h_oldlp[0], h_adv[0], h_ret[0],
                                                 clip_eps, vf_coef, ent_coef, NULL, NULL, NULL, NULL, NULL);
            hd[j] = orig;
            double fd = (lp_p - lp_m) / (2.0 * eps);
            double e = fabs((double)h_dh32[j] - fd);
            if (e > max_fd_err) max_fd_err = e;
        }
    }

    int pass = (range_bad == 0 && max_lp_err <= 1e-4 && max_val_err <= 1e-6 &&
                max_grad_err <= 1e-4 && max_gb_err <= 1e-4 && max_fd_err <= 1e-4 &&
                pl_err <= 1e-4 && vl_err <= 1e-4 && ent_err <= 1e-4 && max_pad_err == 0.0);

    printf("  range_bad=%d  max_lp_err=%.3e  max_val_err=%.3e  max_grad_err=%.3e\n",
           range_bad, max_lp_err, max_val_err, max_grad_err);
    printf("  max_gb_err=%.3e  max_fd_err=%.3e  pl_err=%.3e  vl_err=%.3e  ent_err=%.3e  pad_err=%.1e\n",
           max_gb_err, max_fd_err, pl_err, vl_err, ent_err, max_pad_err);
    printf("  TEST 1: %s\n\n", pass ? "PASS" : "FAIL");

    cudaFree(d_head); cudaFree(d_acts); cudaFree(d_lp); cudaFree(d_val);
    cudaFree(d_oldlp); cudaFree(d_adv); cudaFree(d_ret);
    cudaFree(d_dh16); cudaFree(d_dh32); cudaFree(d_gbh); cudaFree(d_lacc);
    free(h_head); free(h_oldlp); free(h_adv); free(h_ret);
    free(h_acts); free(h_lp); free(h_val); free(h_dh32); free(h_dh16);
    return pass ? 0 : 1;
}

static int run_test2_checkpoint(void) {
    printf("=== TEST 2: BRT1 Teacher Checkpoint Save / Load Round-Trip ===\n");
    const int Q = 256, H = 1024;
    const char *path1 = "/tmp/br_policy_s1_test_ckpt1.bin";
    const char *path2 = "/tmp/br_policy_s1_test_ckpt2.bin";

    br_policy_s1_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.batch_q = Q;
    cfg.hidden  = H;
    cfg.use_nvfp4_fwd   = 1;
    cfg.bwd_prec        = BR_S1_BWD_NVFP4;
    cfg.lr              = 1e-3f;
    cfg.grad_loss_scale = 16.0f;
    cfg.hot_keep_bias   = 4.0f;
    cfg.seed            = 0x1111222233334444ULL;

    br_policy_s1_t *polA = br_policy_s1_create(&cfg);
    cfg.seed = 0x9999888877776666ULL;
    cfg.hot_keep_bias = 0.0f;
    br_policy_s1_t *polB = br_policy_s1_create(&cfg);

    /* Generate non-trivial synthetic obs and run 5 PPO minibatch steps on polA so weights & biases evolve */
    __half *h_obs = (__half *)malloc((size_t)Q * BRC_OBS_DIM * sizeof(__half));
    uint64_t rng = 0xABCDEF0123456789ULL;
    for (int i = 0; i < Q * BRC_OBS_DIM; i++) {
        h_obs[i] = __float2half((((float)(s1_sm64(&rng) & 0xFFFFu) / 32768.0f) - 1.0f));
    }
    __half *d_obs;
    brc_s1_action_t *d_acts;
    float *d_lp, *d_val, *d_adv, *d_ret;
    cudaMalloc(&d_obs,  (size_t)Q * BRC_OBS_DIM * sizeof(__half));
    cudaMalloc(&d_acts, (size_t)Q * sizeof(brc_s1_action_t));
    cudaMalloc(&d_lp,   (size_t)Q * sizeof(float));
    cudaMalloc(&d_val,  (size_t)Q * sizeof(float));
    cudaMalloc(&d_adv,  (size_t)Q * sizeof(float));
    cudaMalloc(&d_ret,  (size_t)Q * sizeof(float));
    cudaMemcpy(d_obs, h_obs, (size_t)Q * BRC_OBS_DIM * sizeof(__half), cudaMemcpyHostToDevice);

    float *h_adv = (float *)malloc((size_t)Q * sizeof(float));
    float *h_ret = (float *)malloc((size_t)Q * sizeof(float));
    for (int i = 0; i < Q; i++) {
        h_adv[i] = (((float)(s1_sm64(&rng) & 0xFFFFu) / 32768.0f) - 1.0f) * 2.0f;
        h_ret[i] = (((float)(s1_sm64(&rng) & 0xFFFFu) / 32768.0f) - 1.0f) * 3.0f;
    }
    cudaMemcpy(d_adv, h_adv, (size_t)Q * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(d_ret, h_ret, (size_t)Q * sizeof(float), cudaMemcpyHostToDevice);

    for (int s = 0; s < 5; s++) {
        br_policy_s1_act(polA, d_obs, d_acts, d_lp, d_val, (uint64_t)s, 0);
        br_policy_s1_ppo_minibatch(polA, d_obs, d_acts, d_lp, d_adv, d_ret, NULL, Q, (uint32_t)(s * 17), NULL);
    }
    br_policy_s1_forward(polA, d_obs);
    br_policy_s1_sync(polA);

    float *h_outA = (float *)malloc((size_t)Q * BR_S1_HEAD_PAD * sizeof(float));
    float *h_outB = (float *)malloc((size_t)Q * BR_S1_HEAD_PAD * sizeof(float));
    cudaMemcpy(h_outA, br_policy_s1_head_out_dev(polA), (size_t)Q * BR_S1_HEAD_PAD * sizeof(float), cudaMemcpyDeviceToHost);

    int rc_save = br_policy_s1_save(polA, path1);
    int rc_load = br_policy_s1_load(polB, path1);
    int rc_resave = br_policy_s1_save(polB, path2);

    br_policy_s1_forward(polB, d_obs);
    br_policy_s1_sync(polB);
    cudaMemcpy(h_outB, br_policy_s1_head_out_dev(polB), (size_t)Q * BR_S1_HEAD_PAD * sizeof(float), cudaMemcpyDeviceToHost);

    /* Verify expected exact file size & bit-exact FP32 checkpoint round-trip */
    size_t expected_bytes = sizeof(brc_teacher_hdr_t) +
        sizeof(float) * ((size_t)H * BRC_OBS_DIM + (size_t)H * H + (size_t)BR_S1_HEAD_DIM * H + BR_S1_HEAD_DIM);
    FILE *f1 = fopen(path1, "rb"), *f2 = fopen(path2, "rb");
    size_t sz1 = 0, sz2 = 0;
    int bit_exact = 0;
    if (f1 && f2) {
        fseek(f1, 0, SEEK_END); sz1 = (size_t)ftell(f1); rewind(f1);
        fseek(f2, 0, SEEK_END); sz2 = (size_t)ftell(f2); rewind(f2);
        if (sz1 == expected_bytes && sz2 == expected_bytes) {
            uint8_t *b1 = (uint8_t *)malloc(sz1), *b2 = (uint8_t *)malloc(sz2);
            if (fread(b1, 1, sz1, f1) == sz1 && fread(b2, 1, sz2, f2) == sz2) {
                bit_exact = (memcmp(b1, b2, sz1) == 0);
            }
            free(b1); free(b2);
        }
    }
    if (f1) fclose(f1);
    if (f2) fclose(f2);
    remove(path1);
    remove(path2);

    double max_logit_diff = 0.0, max_val_diff = 0.0;
    for (int i = 0; i < Q; i++) {
        for (int j = 0; j < BR_S1_HEAD_VAL_OFF; j++) {
            double d = fabs((double)h_outA[(size_t)i * BR_S1_HEAD_PAD + j] -
                            (double)h_outB[(size_t)i * BR_S1_HEAD_PAD + j]);
            if (d > max_logit_diff) max_logit_diff = d;
        }
        double dv = fabs((double)h_outA[(size_t)i * BR_S1_HEAD_PAD + BR_S1_HEAD_VAL_OFF] -
                         (double)h_outB[(size_t)i * BR_S1_HEAD_PAD + BR_S1_HEAD_VAL_OFF]);
        if (dv > max_val_diff) max_val_diff = dv;
    }

    int pass = (rc_save == 0 && rc_load == 0 && rc_resave == 0 &&
                sz1 == expected_bytes && bit_exact == 1 &&
                max_logit_diff <= 1e-3 && max_val_diff <= 1e-3);

    printf("  rc_save=%d  rc_load=%d  bytes=%zu (expected=%zu)  fp32_bit_exact=%d\n",
           rc_save, rc_load, sz1, expected_bytes, bit_exact);
    printf("  max_logit_diff=%.3e  max_val_diff=%.3e\n", max_logit_diff, max_val_diff);
    printf("  TEST 2: %s\n\n", pass ? "PASS" : "FAIL");

    br_policy_s1_destroy(polA);
    br_policy_s1_destroy(polB);
    cudaFree(d_obs); cudaFree(d_acts); cudaFree(d_lp); cudaFree(d_val); cudaFree(d_adv); cudaFree(d_ret);
    free(h_obs); free(h_adv); free(h_ret); free(h_outA); free(h_outB);
    return pass ? 0 : 1;
}

/* Synthetic 17-dim factored target-aiming + key-press + hotbar-switch GPU environment for Test 3 */
__global__ void k_test3_gen_obs(
    __half *__restrict__ obs,
    int32_t *__restrict__ tgt_pitch,
    int32_t *__restrict__ tgt_yaw,
    uint16_t *__restrict__ tgt_keys,
    int32_t *__restrict__ tgt_hot,
    int M, uint64_t seed
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= M) return;
    uint64_t r = seed ^ ((uint64_t)(i + 1) * 0x9e3779b97f4a7c15ULL);
    int tp = (int)(s1_sm64(&r) % BRC_CAM_BINS);
    int ty = (int)(s1_sm64(&r) % BRC_CAM_BINS);
    uint16_t tk = (uint16_t)(s1_sm64(&r) & ((1u << BRC_KEYS) - 1u));
    int th = (int)(s1_sm64(&r) % BRC_HOTBAR_CATS);
    tgt_pitch[i] = tp;
    tgt_yaw[i]   = ty;
    tgt_keys[i]  = tk;
    tgt_hot[i]   = th;

    __half *ob = obs + (size_t)i * BRC_OBS_DIM;
    float p_norm = ((float)tp / 40.0f) * 2.0f - 1.0f;
    float y_norm = ((float)ty / 40.0f) * 2.0f - 1.0f;

    /* obs[0..31]: smooth Gaussian + harmonic pitch features */
    for (int j = 0; j < 24; j++) {
        float c = ((float)j / 23.0f) * 2.0f - 1.0f;
        float d = (p_norm - c) * 6.0f;
        ob[j] = __float2half(expf(-d * d));
    }
    for (int j = 0; j < 4; j++) {
        float w = (float)(j + 1) * 3.14159265f;
        ob[24 + 2 * j]     = __float2half(sinf(w * p_norm));
        ob[24 + 2 * j + 1] = __float2half(cosf(w * p_norm));
    }
    /* obs[32..63]: smooth Gaussian + harmonic yaw features */
    for (int j = 0; j < 24; j++) {
        float c = ((float)j / 23.0f) * 2.0f - 1.0f;
        float d = (y_norm - c) * 6.0f;
        ob[32 + j] = __float2half(expf(-d * d));
    }
    for (int j = 0; j < 4; j++) {
        float w = (float)(j + 1) * 3.14159265f;
        ob[56 + 2 * j]     = __float2half(sinf(w * y_norm));
        ob[56 + 2 * j + 1] = __float2half(cosf(w * y_norm));
    }
    /* obs[64..91]: 14 key signals (+/-1) + pairwise XOR/mixed features */
    for (int k = 0; k < BRC_KEYS; k++) {
        float s = ((tk >> k) & 1u) ? 1.0f : -1.0f;
        ob[64 + k] = __float2half(s);
        float s2 = ((tk >> ((k + 1) % BRC_KEYS)) & 1u) ? 1.0f : -1.0f;
        ob[78 + k] = __float2half(0.5f * (s + s2));
    }
    for (int j = 92; j < 96; j++) ob[j] = __float2half(0.0f);
    /* obs[96..111]: 10-way hotbar one-hot + normalized index */
    for (int j = 0; j < BRC_HOTBAR_CATS; j++) {
        ob[96 + j] = __float2half((j == th) ? 1.0f : 0.0f);
    }
    ob[106] = __float2half((float)th / 9.0f);
    for (int j = 107; j < BRC_OBS_DIM; j++) ob[j] = __float2half(0.0f);
}

__global__ void k_test3_eval_rew(
    const brc_s1_action_t *__restrict__ acts,
    const float *__restrict__ vals,
    const int32_t *__restrict__ tgt_pitch,
    const int32_t *__restrict__ tgt_yaw,
    const uint16_t *__restrict__ tgt_keys,
    const int32_t *__restrict__ tgt_hot,
    float *__restrict__ adv_out,
    float *__restrict__ ret_out,
    float *__restrict__ metrics_out, /* [0]=rew, [1]=p_ex, [2]=p_pm1, [3]=y_ex, [4]=y_pm1, [5]=k_acc, [6]=h_ex */
    int M
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    float r = 0.0f, pex = 0.0f, ppm1 = 0.0f, yex = 0.0f, ypm1 = 0.0f, kacc = 0.0f, hex = 0.0f;
    if (i < M) {
        int dp = abs(acts[i].a[0] - tgt_pitch[i]);
        int dy = abs(acts[i].a[1] - tgt_yaw[i]);
        pex  = (dp == 0) ? 1.0f : 0.0f;
        ppm1 = (dp <= 1) ? 1.0f : 0.0f;
        yex  = (dy == 0) ? 1.0f : 0.0f;
        ypm1 = (dy <= 1) ? 1.0f : 0.0f;

        uint16_t tk = tgt_keys[i];
        int km = 0;
        for (int k = 0; k < BRC_KEYS; k++) {
            int bit = (tk >> k) & 1;
            if (acts[i].a[2 + k] == bit) km++;
        }
        kacc = (float)km / (float)BRC_KEYS;
        hex  = (acts[i].a[16] == tgt_hot[i]) ? 1.0f : 0.0f;

        r = (1.0f - (float)dp * 0.05f + pex)
          + (1.0f - (float)dy * 0.05f + yex)
          + 2.5f * kacc
          + 1.5f * hex;
        ret_out[i] = r;
        adv_out[i] = r - vals[i];
    }
    #pragma unroll
    for (int off = 16; off > 0; off >>= 1) {
        r    += __shfl_down_sync(0xffffffffu, r, off);
        pex  += __shfl_down_sync(0xffffffffu, pex, off);
        ppm1 += __shfl_down_sync(0xffffffffu, ppm1, off);
        yex  += __shfl_down_sync(0xffffffffu, yex, off);
        ypm1 += __shfl_down_sync(0xffffffffu, ypm1, off);
        kacc += __shfl_down_sync(0xffffffffu, kacc, off);
        hex  += __shfl_down_sync(0xffffffffu, hex, off);
    }
    if ((threadIdx.x & 31) == 0) {
        atomicAdd(&metrics_out[0], r);
        atomicAdd(&metrics_out[1], pex);
        atomicAdd(&metrics_out[2], ppm1);
        atomicAdd(&metrics_out[3], yex);
        atomicAdd(&metrics_out[4], ypm1);
        atomicAdd(&metrics_out[5], kacc);
        atomicAdd(&metrics_out[6], hex);
    }
}

static int run_test3_convergence_and_speed(void) {
    printf("=== TEST 3: End-to-End Factored 17-Dim PPO Convergence + Throughput (Q=8192, H=1024) ===\n");
    const int Q = 8192, H = 1024;
    const int T_bench = 16;             /* 131,072 samples/iter for 3-epoch PPO latency benchmark */
    const int M_bench = Q * T_bench;
    const int T_conv  = 4;              /* 32,768 samples/iter for 80-iter convergence run */
    const int M_conv  = Q * T_conv;

    __half *d_obs;
    brc_s1_action_t *d_acts;
    float *d_lp, *d_val, *d_adv, *d_ret, *d_metrics;
    int32_t *d_tp, *d_ty, *d_th;
    uint16_t *d_tk;

    cudaMalloc(&d_obs,     (size_t)M_bench * BRC_OBS_DIM * sizeof(__half));
    cudaMalloc(&d_acts,    (size_t)M_bench * sizeof(brc_s1_action_t));
    cudaMalloc(&d_lp,      (size_t)M_bench * sizeof(float));
    cudaMalloc(&d_val,     (size_t)M_bench * sizeof(float));
    cudaMalloc(&d_adv,     (size_t)M_bench * sizeof(float));
    cudaMalloc(&d_ret,     (size_t)M_bench * sizeof(float));
    cudaMalloc(&d_tp,      (size_t)M_bench * sizeof(int32_t));
    cudaMalloc(&d_ty,      (size_t)M_bench * sizeof(int32_t));
    cudaMalloc(&d_tk,      (size_t)M_bench * sizeof(uint16_t));
    cudaMalloc(&d_th,      (size_t)M_bench * sizeof(int32_t));
    cudaMalloc(&d_metrics, 7 * sizeof(float));

    k_test3_gen_obs<<<(M_bench + 255) / 256, 256>>>(d_obs, d_tp, d_ty, d_tk, d_th, M_bench, 0x42424242ULL);
    cudaDeviceSynchronize();

    /* Part 3A: Throughput benchmarks across all 3 backward routes + pure NVFP4 act speed */
    double nvfp4_act_mpol_s = 0.0;
    double nvfp4_ppo_ms = 1e9;
    int any_nan = 0;

    for (int bp = BR_S1_BWD_NVFP4; bp >= BR_S1_BWD_FP16; bp--) {
        br_policy_s1_cfg_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.batch_q         = Q;
        cfg.hidden          = H;
        cfg.use_nvfp4_fwd   = 1;
        cfg.bwd_prec        = bp;
        cfg.lr              = 3e-4f;
        cfg.clip_eps        = 0.2f;
        cfg.vf_coef         = 0.5f;
        cfg.ent_coef        = 0.01f;
        cfg.grad_loss_scale = 16.0f;
        cfg.seed            = 0x20261124ULL;
        br_policy_s1_t *pol = br_policy_s1_create(&cfg);

        for (int t = 0; t < T_bench; t++) {
            size_t off = (size_t)t * Q;
            br_policy_s1_act(pol, d_obs + off * BRC_OBS_DIM, d_acts + off, d_lp + off, d_val + off, (uint64_t)t, 0);
        }
        cudaMemset(d_metrics, 0, 7 * sizeof(float));
        k_test3_eval_rew<<<(M_bench + 255) / 256, 256>>>(d_acts, d_val, d_tp, d_ty, d_tk, d_th, d_adv, d_ret, d_metrics, M_bench);
        br_policy_s1_sync(pol);

        if (bp == BR_S1_BWD_NVFP4) {
            /* Benchmark pure NVFP4 forward + factored s1 sampling throughput */
            for (int w = 0; w < 20; w++) br_policy_s1_act(pol, d_obs, d_acts, d_lp, d_val, (uint64_t)w, 0);
            br_policy_s1_sync(pol);
            double best_act_us = 1e18;
            for (int r = 0; r < 5; r++) {
                double t0 = s1_now_ns();
                for (int k = 0; k < 100; k++) {
                    br_policy_s1_act(pol, d_obs, d_acts, d_lp, d_val, (uint64_t)(100 + k), 0);
                }
                br_policy_s1_sync(pol);
                double us = (s1_now_ns() - t0) / (1000.0 * 100.0);
                if (us < best_act_us) best_act_us = us;
            }
            nvfp4_act_mpol_s = (double)Q / best_act_us;
            printf("  ACT_BENCH (NVFP4 Fwd + s1 Sample, Q=%d, H=%d): %.1f us/chunk -> %.2f M policy-steps/s (K=4: %.2f M ticks/s)\n",
                   Q, H, best_act_us, nvfp4_act_mpol_s, nvfp4_act_mpol_s * 4.0);
        }

        /* Warm up & time 3-epoch PPO update over M_bench = 131,072 samples (48 minibatches) */
        br_policy_s1_stats_t st;
        memset(&st, 0, sizeof(st));
        br_policy_s1_ppo_update(pol, d_obs, d_acts, d_lp, d_adv, d_ret, M_bench, 3, 1u, &st);
        double best_ms = 1e18;
        for (int r = 0; r < 3; r++) {
            br_policy_s1_ppo_update(pol, d_obs, d_acts, d_lp, d_adv, d_ret, M_bench, 3, (uint32_t)(2 + r), &st);
            if (st.update_ms < best_ms) best_ms = st.update_ms;
            if (!isfinite(st.policy_loss) || !isfinite(st.value_loss) || !isfinite(st.entropy)) any_nan = 1;
        }
        double ppo_mpol_s = (double)M_bench / (best_ms * 1e3);
        printf("  PPO_3EP_BENCH (bwd=%-5s, M=%d, Q=%d, H=%d, 48 mbs): %.2f ms/iter (%.2f M PPO-steps/s, K=4: %.2f M ticks/s)\n",
               br_s1_bwd_name(bp), M_bench, Q, H, best_ms, ppo_mpol_s, ppo_mpol_s * 4.0);
        if (bp == BR_S1_BWD_NVFP4) nvfp4_ppo_ms = best_ms;
        br_policy_s1_destroy(pol);
    }

    /* Part 3B: End-to-end 17-dim factored PPO convergence (NVFP4 Fwd + NVFP4-SR Bwd) */
    br_policy_s1_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.batch_q         = Q;
    cfg.hidden          = H;
    cfg.use_nvfp4_fwd   = 1;
    cfg.bwd_prec        = BR_S1_BWD_NVFP4;
    cfg.lr              = 1.0e-4f;
    cfg.clip_eps        = 0.2f;
    cfg.vf_coef         = 0.5f;
    cfg.ent_coef        = 0.01f;
    cfg.grad_loss_scale = 16.0f;
    cfg.seed            = 0x20261124ULL;
    br_policy_s1_t *pol = br_policy_s1_create(&cfg);

    double init_rew = 0.0, final_rew = 0.0;
    double init_vl  = 0.0, final_vl  = 0.0;
    double final_p1 = 0.0, final_y1  = 0.0, final_ka = 0.0, final_ha = 0.0;
    double final_pex = 0.0, final_yex = 0.0;

    const int iters = 100;
    cudaStream_t st_pol = (cudaStream_t)br_policy_s1_stream(pol);
    for (int it = 0; it < iters; it++) {
        k_test3_gen_obs<<<(M_conv + 255) / 256, 256, 0, st_pol>>>(
            d_obs, d_tp, d_ty, d_tk, d_th, M_conv, 0x99990000ULL + (uint64_t)it * 1337ULL);
        for (int t = 0; t < T_conv; t++) {
            size_t off = (size_t)t * Q;
            br_policy_s1_act(pol, d_obs + off * BRC_OBS_DIM, d_acts + off, d_lp + off, d_val + off,
                             (uint64_t)it * 16ULL + (uint64_t)t, 0);
        }
        cudaMemsetAsync(d_metrics, 0, 7 * sizeof(float), st_pol);
        k_test3_eval_rew<<<(M_conv + 255) / 256, 256, 0, st_pol>>>(
            d_acts, d_val, d_tp, d_ty, d_tk, d_th, d_adv, d_ret, d_metrics, M_conv);

        br_policy_s1_stats_t st;
        memset(&st, 0, sizeof(st));
        br_policy_s1_ppo_update(pol, d_obs, d_acts, d_lp, d_adv, d_ret, M_conv, 3, (uint32_t)(it + 1), &st);

        float hm[7];
        cudaMemcpy(hm, d_metrics, sizeof(hm), cudaMemcpyDeviceToHost);
        double rew  = (double)hm[0] / M_conv;
        double pex  = 100.0 * (double)hm[1] / M_conv;
        double ppm1 = 100.0 * (double)hm[2] / M_conv;
        double yex  = 100.0 * (double)hm[3] / M_conv;
        double ypm1 = 100.0 * (double)hm[4] / M_conv;
        double kacc = 100.0 * (double)hm[5] / M_conv;
        double hex  = 100.0 * (double)hm[6] / M_conv;

        if (!isfinite(st.policy_loss) || !isfinite(st.value_loss) || !isfinite(st.entropy) || !isfinite(rew)) {
            any_nan = 1;
        }
        if (it == 0) { init_rew = rew; init_vl = st.value_loss; }
        if (it == iters - 1) {
            final_rew = rew; final_vl = st.value_loss;
            final_pex = pex; final_p1 = ppm1;
            final_yex = yex; final_y1 = ypm1;
            final_ka = kacc; final_ha = hex;
        }
        if (it == 0 || it == 9 || it == 24 || it == 49 || it == 74 || it == iters - 1) {
            printf("  iter=%3d  rew=%5.2f  vl=%6.3f  ent=%5.2f  kl=%+.3f  clip=%4.1f%%  pitch=%5.1f%%/%5.1f%%  yaw=%5.1f%%/%5.1f%%  keys=%5.1f%%  hot=%5.1f%%\n",
                   it, rew, st.value_loss, st.entropy, st.approx_kl, 100.0 * st.clip_frac, pex, ppm1, yex, ypm1, kacc, hex);
        }
    }

    int pass = (!any_nan &&
                nvfp4_act_mpol_s >= 20.0 &&
                nvfp4_ppo_ms < 22.0 &&
                final_rew > init_rew + 3.0 &&
                final_vl < 0.25 * init_vl &&
                final_p1 >= 70.0 && final_y1 >= 70.0 &&
                final_ka >= 90.0 && final_ha >= 85.0);

    printf("  SUMMARY: any_nan=%d  act=%.2fM/s  ppo_3ep=%.2fms  rew=%.2f->%.2f  vl=%.3f->%.3f  p_ex=%.1f%% y_ex=%.1f%%\n",
           any_nan, nvfp4_act_mpol_s, nvfp4_ppo_ms, init_rew, final_rew, init_vl, final_vl, final_pex, final_yex);
    printf("  TEST 3: %s\n\n", pass ? "PASS" : "FAIL");

    br_policy_s1_destroy(pol);
    cudaFree(d_obs); cudaFree(d_acts); cudaFree(d_lp); cudaFree(d_val);
    cudaFree(d_adv); cudaFree(d_ret); cudaFree(d_tp); cudaFree(d_ty);
    cudaFree(d_tk);  cudaFree(d_th);   cudaFree(d_metrics);
    return pass ? 0 : 1;
}

int main(void) {
    int fails = 0;
    fails += run_test1_parity();
    fails += run_test2_checkpoint();
    fails += run_test3_convergence_and_speed();
    printf("=== ALL TESTS %s (fails=%d) ===\n", fails == 0 ? "PASSED" : "FAILED", fails);
    return fails ? 1 : 0;
}

#endif /* BR_POLICY_S1_TEST */
