/* Netherite Bedrock GPU-Resident PPO Trainer:
 * - 5 Overworld-to-Nether-Portal GPU Environments:
 *     wood-punch -> wood-logs3 -> stone-mine -> iron-mine -> nether-portal (lava cast + ignite!)
 * - Forward: Native sm_120a Square-GEMM NVFP4 (CUDA_R_4F_E2M1 + 128x4 swizzled VEC16_UE4M3 + fused ReLU)
 * - Backward: Selectable FP16 | FP8 E4M3 (1,008 TFLOPS) | NVFP4 Stochastic-Rounded (2,015 TFLOPS)!
 * - Split Fast-Path + Warp-Ballot (__ballot_sync) Coherent Slow-Path Reset Queue on Stream B
 */
#include "bedrock.h"
#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cublasLt.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double tr_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static __host__ __device__ __forceinline__ uint64_t tr_sm64(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static __host__ __device__ __forceinline__ uint8_t tr_f32_to_ue4m3(float x) {
    if (x <= 0.0f) return 0x01;
    if (x >= 448.0f) return 0x7E;
    uint32_t u; memcpy(&u, &x, 4);
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

static __host__ __device__ __forceinline__ float tr_ue4m3_to_f32(uint8_t s) {
    int exp = (s >> 3) & 0xF, man = s & 7;
    return (exp == 0) ? ((float)man / 512.0f) : ldexpf(1.0f + (float)man / 8.0f, exp - 7);
}

static __host__ __device__ __forceinline__ size_t tr_swz_idx(int r, int kb, int num_k_tiles) {
    int rt = r >> 7, ri = r & 127, r32 = ri & 31, r4 = ri >> 5;
    int kt = kb >> 2, k4 = kb & 3;
    return (size_t)((rt * num_k_tiles + kt) * 512 + r32 * 16 + r4 * 4 + k4);
}

static __host__ __device__ __forceinline__ size_t tr_swz_bytes(int rows, int K) {
    return (size_t)((rows + 127) / 128) * 128 * (size_t)(((K / 16) + 3) / 4 * 4);
}

static __device__ __forceinline__ uint8_t tr_cvt_e2m1x2(float v0, float v1) {
    uint32_t r;
    asm volatile("{ .reg .b8 tmp; cvt.rn.satfinite.e2m1x2.f32 tmp, %2, %1; mov.b32 %0, {tmp, 0, 0, 0}; }"
                 : "=r"(r) : "f"(v0), "f"(v1));
    return (uint8_t)(r & 0xFFu);
}

static __device__ __forceinline__ uint16_t tr_cvt_e4m3x2(float v0, float v1) {
    uint16_t r;
    asm volatile("cvt.rn.satfinite.e4m3x2.f32 %0, %2, %1;" : "=h"(r) : "f"(v0), "f"(v1));
    return r;
}

/* Unbiased stochastic rounding jitter in E2M1 domain (NVFP4 pretraining paper recipe for backward!) */
static __device__ __forceinline__ float tr_e2m1_sr(float x, uint32_t *rng) {
    uint32_t z = (*rng = (*rng * 1664525u + 1013904223u));
    float u = ((float)(z >> 8) * (1.0f / 16777216.0f)) - 0.5f;
    float ax = fabsf(x);
    /* E2M1 grid spacing: 0.5 for [0,2], 1.0 for [2,4], 2.0 for [4,6] */
    float step = (ax < 2.0f) ? 0.5f : ((ax < 4.0f) ? 1.0f : 2.0f);
    return x + u * step;
}

static uint8_t tr_f32_to_e2m1_host(float v) {
    static const float lut[8] = {0.f, 0.5f, 1.f, 1.5f, 2.f, 3.f, 4.f, 6.f};
    uint8_t s = (v < 0.f) ? 8u : 0u; float av = fabsf(v); int bi = 0; float bd = 1e9f;
    for (int i = 0; i < 8; i++) { float d = fabsf(av - lut[i]); if (d < bd) { bd = d; bi = i; } }
    return (uint8_t)(s | bi);
}

/* ============================================================================
 * Quantization & Transpose Kernels for Forward (NVFP4) & Backward (FP8 / NVFP4)
 * ============================================================================ */
__global__ void k_tr_quant_f16_to_nvfp4_swz(
    const __half *__restrict__ src, uint8_t *__restrict__ dst_q,
    uint8_t *__restrict__ dst_sf, int rows, int K, int do_sr, uint32_t seed_off
) {
    int kb_n = K >> 4, nkt = (kb_n + 3) >> 2;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= rows * kb_n) return;
    int r = idx / kb_n, kb = idx - r * kb_n;
    const __half *p = src + (size_t)r * K + (kb << 4);
    float v[16], am = 0.0f;
    #pragma unroll
    for (int k = 0; k < 16; k++) { v[k] = __half2float(p[k]); am = fmaxf(am, fabsf(v[k])); }
    uint8_t sf = tr_f32_to_ue4m3(am * (1.0f / 6.0f));
    float inv = 1.0f / tr_ue4m3_to_f32(sf);
    dst_sf[tr_swz_idx(r, kb, nkt)] = sf;
    uint8_t *q = dst_q + (size_t)r * (K >> 1) + (kb << 3);
    uint32_t rng = (uint32_t)idx * 747796405u + seed_off + 2891336453u;
    #pragma unroll
    for (int k = 0; k < 16; k += 2) {
        float a0 = v[k] * inv, a1 = v[k + 1] * inv;
        if (do_sr) { a0 = tr_e2m1_sr(a0, &rng); a1 = tr_e2m1_sr(a1, &rng); }
        q[k >> 1] = tr_cvt_e2m1x2(a0, a1);
    }
}

/* Coalesced 64x32 SMEM Tiled Transpose + NVFP4 Block-16 Quantize (128x4 swizzled UE4M3):
 * src [R, C] -> dst_T [C, R] packed E2M1 + swizzled UE4M3.
 * Block(32, 4) = 128 threads: loads 64x32 half tile coalesced, then each thread owns 1 (c, kb) block of 16! */
__global__ void k_tr_quant_trans_nvfp4_swz(
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
    uint8_t sf = tr_f32_to_ue4m3(am * (1.0f / 6.0f));
    float inv = 1.0f / tr_ue4m3_to_f32(sf);
    dst_sf[tr_swz_idx(c, kb, nkt)] = sf;
    uint32_t rng = (uint32_t)(c * kb_n + kb) * 747796405u + seed_off + 1013904223u;
    uint64_t packed = 0;
    #pragma unroll
    for (int k = 0; k < 16; k += 2) {
        float a0 = v[k] * inv, a1 = v[k + 1] * inv;
        if (do_sr) { a0 = tr_e2m1_sr(a0, &rng); a1 = tr_e2m1_sr(a1, &rng); }
        packed |= ((uint64_t)tr_cvt_e2m1x2(a0, a1)) << ((k >> 1) * 8);
    }
    *(uint64_t *)(dst_q + (size_t)c * (R >> 1) + (kb << 3)) = packed;
}

/* Fast FP8 E4M3 quantization with 1-step Delayed Scaling (Transformer Engine recipe: zero separate absmax pass!) */
__global__ void k_tr_absmax_f16(const __half *__restrict__ src, float *__restrict__ out_scale, int n) {
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

__global__ void k_tr_fp8_swap_scales(float *sc, float *nxt) {
    int i = threadIdx.x;
    if (i < 3) {
        float s = nxt[i];
        if (s > 1e-6f) sc[i] = s;
        nxt[i] = 0.0f;
    }
}

__global__ void k_tr_quant_f16_to_fp8(
    const __half *__restrict__ src, uint8_t *__restrict__ dst,
    const float *__restrict__ d_scale, float *__restrict__ d_nxt_sc, int n
) {
    int i = (blockIdx.x * blockDim.x + threadIdx.x) * 2;
    float m = 0.0f;
    if (i < n) {
        float inv = 1.0f / (*d_scale);
        float v0 = __half2float(src[i]), v1 = __half2float(src[i + 1]);
        m = fmaxf(fabsf(v0), fabsf(v1));
        *(uint16_t *)(dst + i) = tr_cvt_e4m3x2(v0 * inv, v1 * inv);
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

/* Fast 32x32 Coalesced Tiled Transpose + FP8 E4M3 Quant + Fused Delayed-Scale Amax (Block(32, 8), 4 rows/thread!) */
__global__ void k_tr_quant_trans_fp8(
    const __half *__restrict__ src, uint8_t *__restrict__ dst_T,
    const float *__restrict__ d_scale, float *__restrict__ d_nxt_sc, int R, int C
) {
    __shared__ uint8_t tile[32][33];
    float inv = 1.0f / (*d_scale);
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
                tile[threadIdx.y + j][threadIdx.x] = (uint8_t)(tr_cvt_e4m3x2(v * inv, 0.0f) & 0xFFu);
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

/* ============================================================================
 * 5 Overworld-to-Nether-Portal GPU Environments (Q16.16 + Delta Overlay + Split Queue)
 * ============================================================================ */
#define Q16_ONE      65536
#define Q16_GRAV     5243
#define Q16_YDRAG    64225
#define Q16_GND_DRAG 35783
#define Q16_AIR_DRAG 59638
#define Q16_JUMP     27525
#define ENV_MAX_TICKS 400

static __device__ __forceinline__ int d_blk(int x, int y, int z) {
    return ((y & 31) << 12) | ((z & 63) << 6) | (x & 63);
}

static __device__ __forceinline__ int d_get_blk(
    const uint8_t *__restrict__ blocks, const uint32_t *__restrict__ deltas, int nd, int x, int y, int z
) {
    if ((unsigned)x >= BR_WX || (unsigned)y >= BR_WY || (unsigned)z >= BR_WZ) return BR_BLK_STONE;
    uint32_t key = (uint32_t)((y << 12) | (z << 6) | x);
    for (int d = nd - 1; d >= 0; d--) {
        uint32_t e = deltas[d];
        if ((e & 0x1FFFFu) == key) return (int)((e >> 18) & 0xFFu);
    }
    return (int)__ldg(&blocks[d_blk(x, y, z)]);
}

static __device__ __forceinline__ int d_is_solid(
    const uint32_t *__restrict__ bits, const uint32_t *__restrict__ deltas, int nd, int x, int y, int z
) {
    if ((unsigned)x >= BR_WX || (unsigned)y >= BR_WY || (unsigned)z >= BR_WZ) return 1;
    uint32_t key = (uint32_t)((y << 12) | (z << 6) | x);
    for (int d = nd - 1; d >= 0; d--) {
        uint32_t e = deltas[d];
        if ((e & 0x1FFFFu) == key) return (int)((e >> 17) & 1u);
    }
    int idx = d_blk(x, y, z);
    return (int)((__ldg(&bits[idx >> 5]) >> (idx & 31)) & 1u);
}

static __device__ __forceinline__ void d_env_target(int env_id, float *tx, float *tz, int *tgt_blk, int *need_cnt, int *break_ticks) {
    switch (env_id) {
        case BR_ENV_WOOD_PUNCH:    *tx = 16.5f; *tz = 18.5f; *tgt_blk = BR_BLK_LOG;      *need_cnt = 1; *break_ticks = 20; break;
        case BR_ENV_WOOD_LOGS3:    *tx = 16.5f; *tz = 18.5f; *tgt_blk = BR_BLK_LOG;      *need_cnt = 3; *break_ticks = 16; break;
        case BR_ENV_STONE_MINE:    *tx = 31.5f; *tz = 31.5f; *tgt_blk = BR_BLK_STONE;    *need_cnt = 3; *break_ticks = 12; break;
        case BR_ENV_IRON_MINE:     *tx = 45.0f; *tz = 45.0f; *tgt_blk = BR_BLK_IRON_ORE; *need_cnt = 3; *break_ticks = 15; break;
        default: /* NETHER_PORTAL */ *tx = 23.5f; *tz = 23.0f; *tgt_blk = BR_BLK_LAVA;   *need_cnt = 4; *break_ticks = 8;  break;
    }
}

static __device__ __forceinline__ void d_reset_world(
    int i, int env_id, uint64_t *rng_st,
    int32_t *qx, int32_t *qy, int32_t *qz,
    int32_t *vx, int32_t *vy, int32_t *vz,
    float *yaw, uint8_t *gnd, uint8_t *dticks, uint8_t *prog, uint16_t *ep_tick,
    uint8_t *ndelta, float *prev_dist
) {
    uint64_t r = rng_st[i];
    float tx, tz; int tb, nc, bt;
    d_env_target(env_id, &tx, &tz, &tb, &nc, &bt);
    float ang = (float)(tr_sm64(&r) & 1023) * (6.2831853f / 1024.0f);
    float rad = 2.8f + (float)(tr_sm64(&r) & 255) * (2.2f / 256.0f);
    float sx = fminf(fmaxf(tx + cosf(ang) * rad, 3.5f), 60.5f);
    float sz = fminf(fmaxf(tz + sinf(ang) * rad, 3.5f), 60.5f);
    float sy = 9.0f;
    float to_tgt = atan2f(-(tx - sx), (tz - sz)) * (180.0f / 3.14159265f);
    float jitter = ((float)(tr_sm64(&r) & 255) / 255.0f - 0.5f) * 120.0f;
    qx[i] = (int32_t)rintf(sx * 65536.0f);
    qy[i] = (int32_t)rintf(sy * 65536.0f);
    qz[i] = (int32_t)rintf(sz * 65536.0f);
    vx[i] = 0; vy[i] = 0; vz[i] = 0;
    yaw[i] = to_tgt + jitter;
    gnd[i] = 1; dticks[i] = 0; prog[i] = 0; ep_tick[i] = 0; ndelta[i] = 0;
    prev_dist[i] = sqrtf((tx - sx)*(tx - sx) + (tz - sz)*(tz - sz));
    rng_st[i] = r;
}

__global__ void k_tr_init_worlds(
    int env_id, uint64_t *rng_st, uint64_t base_seed,
    int32_t *qx, int32_t *qy, int32_t *qz,
    int32_t *vx, int32_t *vy, int32_t *vz,
    float *yaw, uint8_t *gnd, uint8_t *dticks, uint8_t *prog, uint16_t *ep_tick,
    uint8_t *ndelta, float *prev_dist, int N
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    rng_st[i] = base_seed ^ ((uint64_t)(i + 1) * 0x9e3779b97f4a7c15ULL);
    d_reset_world(i, env_id, rng_st, qx, qy, qz, vx, vy, vz, yaw, gnd, dticks, prog, ep_tick, ndelta, prev_dist);
}

__global__ void k_tr_step_fast_split(
    int env_id,
    const uint8_t  *__restrict__ seed_blocks,
    const uint32_t *__restrict__ seed_bits,
    int num_seeds,
    int32_t *__restrict__ qx, int32_t *__restrict__ qy, int32_t *__restrict__ qz,
    int32_t *__restrict__ vx, int32_t *__restrict__ vy, int32_t *__restrict__ vz,
    float   *__restrict__ yawp, uint8_t *__restrict__ gndp,
    uint8_t *__restrict__ dtp, uint8_t *__restrict__ progp, uint16_t *__restrict__ eptp,
    uint32_t *__restrict__ g_deltas, uint8_t *__restrict__ g_ndelta,
    float   *__restrict__ prev_distp,
    const uint8_t *__restrict__ acts,
    __half  *__restrict__ obs_out,
    float   *__restrict__ rew_out,
    uint8_t *__restrict__ term_out,
    uint8_t *__restrict__ trunc_out,
    int     *__restrict__ slow_q,
    int     *__restrict__ slow_cnt,
    uint32_t *__restrict__ win_acc,
    int offset, int Q, int N_total, int K,
    int max_ticks, int cfg_need_cnt, int cfg_break_ticks
) {
    int local = blockIdx.x * blockDim.x + threadIdx.x;
    if (local >= Q) return;
    int i = offset + local;
    int sidx = i % num_seeds;
    const uint8_t  *blocks = seed_blocks + (size_t)sidx * BR_BLOCKS;
    const uint32_t *bits   = seed_bits   + (size_t)sidx * BR_BIT_WORDS;

    float tx, tz; int tgt_blk, need_cnt, break_ticks;
    d_env_target(env_id, &tx, &tz, &tgt_blk, &need_cnt, &break_ticks);
    if (cfg_need_cnt > 0) need_cnt = cfg_need_cnt;
    if (cfg_break_ticks > 0) break_ticks = cfg_break_ticks;

    int32_t x = qx[i], y = qy[i], z = qz[i];
    int32_t dx = vx[i], dy = vy[i], dz = vz[i];
    float yaw = yawp[i];
    uint8_t on_gnd = gndp[i], dt = dtp[i], prog = progp[i];
    uint16_t et = eptp[i];
    int nd = g_ndelta[i]; if (nd > 8) nd = 8;
    uint32_t loc_d[8];
    for (int d = 0; d < nd; d++) loc_d[d] = g_deltas[(size_t)d * N_total + i];

    /* 16 discrete raw-key combos (includes MINE & USE/CAST/IGNITE!) */
    uint8_t aid = acts[local] & 15;
    int fwd = 0, str = 0, jmp = 0, mine = 0, use = 0;
    float dyaw = 0.0f;
    switch (aid) {
        case 1:  fwd = 1; break;
        case 2:  fwd = -1; break;
        case 3:  str = 1; break;
        case 4:  str = -1; break;
        case 5:  fwd = 1; jmp = 1; break;
        case 6:  dyaw = -12.0f; break;
        case 7:  dyaw = +12.0f; break;
        case 8:  fwd = 1; dyaw = -12.0f; break;
        case 9:  fwd = 1; dyaw = +12.0f; break;
        case 10: mine = 1; break;
        case 11: fwd = 1; mine = 1; break;
        case 12: dyaw = -8.0f; mine = 1; break;
        case 13: dyaw = +8.0f; mine = 1; break;
        case 14: use = 1; break;
        case 15: fwd = 1; use = 1; break;
        default: break;
    }

    float step_rew = 0.0f;
    int won = 0, timed_out = 0;

    for (int t = 0; t < K; t++) {
        yaw += dyaw * (1.0f / (float)K);
        if (yaw > 180.0f) yaw -= 360.0f;
        if (yaw < -180.0f) yaw += 360.0f;

        if (jmp && on_gnd) dy = Q16_JUMP;
        float yr = yaw * 0.017453292519943295f;
        float sy = sinf(yr), cy = cosf(yr);
        float acc = on_gnd ? 0.10f : 0.02f;
        if (fwd || str) {
            float f = (float)fwd * acc, s = (float)str * acc;
            dx += (int32_t)rintf((s * cy - f * sy) * 65536.0f);
            dz += (int32_t)rintf((f * cy + s * sy) * 65536.0f);
        }

        int bx = x >> 16, by = y >> 16, bz = z >> 16;
        on_gnd = 0;
        if (dy < 0 && d_is_solid(bits, loc_d, nd, bx, by - 1, bz)) { dy = 0; on_gnd = 1; }
        if (dx > 0 && d_is_solid(bits, loc_d, nd, (x + dx + 19661) >> 16, by, bz)) dx = 0;
        if (dx < 0 && d_is_solid(bits, loc_d, nd, (x + dx - 19661) >> 16, by, bz)) dx = 0;
        if (dz > 0 && d_is_solid(bits, loc_d, nd, bx, by, (z + dz + 19661) >> 16)) dz = 0;
        if (dz < 0 && d_is_solid(bits, loc_d, nd, bx, by, (z + dz - 19661) >> 16)) dz = 0;
        x += dx; y += dy; z += dz;

        int32_t drag = on_gnd ? Q16_GND_DRAG : Q16_AIR_DRAG;
        dy = (int32_t)(((int64_t)(dy - Q16_GRAV) * Q16_YDRAG) >> 16);
        dx = (int32_t)(((int64_t)dx * drag) >> 16);
        dz = (int32_t)(((int64_t)dz * drag) >> 16);

        /* Eye ray check against current task objective block within 4.5 blocks */
        float px = (float)x * (1.0f / 65536.0f);
        float py = (float)y * (1.0f / 65536.0f) + 1.62f;
        float pz = (float)z * (1.0f / 65536.0f);
        float rx = -sy, rz = cy;
        float ry = (env_id == BR_ENV_NETHER_PORTAL) ? -0.45f : 0.0f; /* look down toward lava pool */
        int hit_tgt = 0, hx = 0, hy = 0, hz = 0;
        int want_blk = (env_id == BR_ENV_NETHER_PORTAL && prog >= need_cnt) ? BR_BLK_OBSIDIAN : tgt_blk;
        for (int s = 1; s <= 45; s++) {
            float d = (float)s * 0.1f;
            int qxi = (int)floorf(px + rx * d);
            int qyi = (int)floorf(py + ry * d);
            int qzi = (int)floorf(pz + rz * d);
            int bid = d_get_blk(blocks, loc_d, nd, qxi, qyi, qzi);
            if (bid != BR_BLK_AIR) {
                if (bid == want_blk) { hit_tgt = 1; hx = qxi; hy = qyi; hz = qzi; }
                break;
            }
            if (env_id == BR_ENV_WOOD_LOGS3) {
                /* Pitch slightly down/up to chop remaining logs on the same 4-high oak trunk */
                int b_dn = d_get_blk(blocks, loc_d, nd, qxi, qyi - 1, qzi);
                if (b_dn == want_blk) { hit_tgt = 1; hx = qxi; hy = qyi - 1; hz = qzi; break; }
                int b_up = d_get_blk(blocks, loc_d, nd, qxi, qyi + 1, qzi);
                if (b_up == want_blk) { hit_tgt = 1; hx = qxi; hy = qyi + 1; hz = qzi; break; }
                int b_dn2 = d_get_blk(blocks, loc_d, nd, qxi, qyi - 2, qzi);
                if (b_dn2 == want_blk) { hit_tgt = 1; hx = qxi; hy = qyi - 2; hz = qzi; break; }
            }
        }
        if (hit_tgt) step_rew += 0.015f;

        if (env_id == BR_ENV_NETHER_PORTAL) {
            /* Stage 1 (prog < 4): bucket-cast lava (11) -> obsidian (49, solid=1!) into delta ring!
             * Stage 2 (prog == 4): flint & steel on obsidian -> place portal fire (90) -> WON! */
            if (use && hit_tgt) {
                dt++;
                step_rew += 0.03f;
                if (dt >= break_ticks) {
                    if (prog < need_cnt) {
                        /* Cast lava -> solid obsidian frame anchor (id=49, solid=1) */
                        uint32_t e = ((uint32_t)BR_BLK_OBSIDIAN << 18) | (1u << 17) | (uint32_t)((hy << 12) | (hz << 6) | hx);
                        loc_d[nd & 7] = e; if (nd < 8) nd++;
                        prog++;
                        step_rew += 0.35f;
                    } else {
                        /* Ignite Nether Portal interior (id=90, solid=0) above obsidian! */
                        int py_ign = (hy + 1 < BR_WY) ? (hy + 1) : hy;
                        uint32_t e = ((uint32_t)BR_BLK_PORTAL << 18) | (uint32_t)((py_ign << 12) | (hz << 6) | hx);
                        loc_d[nd & 7] = e; if (nd < 8) nd++;
                        step_rew += 1.5f;
                        won = 1;
                        break;
                    }
                    dt = 0;
                }
            } else {
                dt = 0;
            }
        } else {
            /* Mining environments: wood-punch (1), wood-logs3 (3), stone-mine (3), iron-mine (3) */
            if (mine && hit_tgt) {
                dt++;
                step_rew += 0.025f;
                if (dt >= break_ticks) {
                    /* Mine target block -> AIR (id=0, solid=0) into delta ring! */
                    loc_d[nd & 7] = (uint32_t)((hy << 12) | (hz << 6) | hx);
                    if (nd < 8) nd++;
                    prog++;
                    dt = 0;
                    if (prog >= need_cnt) {
                        step_rew += 1.0f;
                        won = 1;
                        break;
                    } else {
                        step_rew += 0.35f;
                    }
                }
            } else {
                dt = 0;
            }
        }

        et++;
        if (et >= max_ticks) { timed_out = 1; break; }
    }

    float px = (float)x * (1.0f / 65536.0f);
    float py = (float)y * (1.0f / 65536.0f);
    float pz = (float)z * (1.0f / 65536.0f);
    float dtx = tx - px, dtz = tz - pz;
    float dist = sqrtf(dtx * dtx + dtz * dtz);
    step_rew += 0.10f * (prev_distp[i] - dist);
    prev_distp[i] = dist;

    /* 128-dim observation: 64 depth rays + 16 target-block hit flags + 16 scalars + pad */
    __half *ob = obs_out + (size_t)local * BR_OBS_DIM;
    float yr = yaw * 0.017453292519943295f;
    float sy = sinf(yr), cy = cosf(yr);
    int bx = (x >> 16) & 63, by = (y >> 16) & 31, bz = (z >> 16) & 63;
    int want_blk = (env_id == BR_ENV_NETHER_PORTAL && prog >= need_cnt) ? BR_BLK_OBSIDIAN : tgt_blk;
    for (int r = 0; r < 64; r++) {
        float ang = yr + ((float)(r & 15) - 7.5f) * 0.14f;
        int rdy = (r >> 4) - 1;
        float rdx = -sinf(ang), rdz = cosf(ang);
        int hit = 8, is_t = 0;
        for (int s = 1; s <= 8; s++) {
            int qxi = bx + (int)rintf(rdx * (float)s);
            int qyi = by + ((env_id == BR_ENV_NETHER_PORTAL) ? 0 : 1) + rdy;
            int qzi = bz + (int)rintf(rdz * (float)s);
            int bid = d_get_blk(blocks, loc_d, nd, qxi, qyi, qzi);
            if (bid != BR_BLK_AIR) { hit = s; is_t = (bid == want_blk); break; }
        }
        ob[r] = __float2half((float)hit * 0.125f);
        if (r >= 16 && r < 32) ob[64 + (r - 16)] = __float2half((float)is_t);
    }
    float invd = 1.0f / (dist + 1e-4f);
    ob[80] = __float2half((-sy * dtx + cy * dtz) * invd);
    ob[81] = __float2half(( cy * dtx + sy * dtz) * invd);
    ob[82] = __float2half(fminf(dist * 0.125f, 1.0f));
    ob[83] = __float2half((float)dt / (float)break_ticks);
    ob[84] = __float2half((float)prog / (float)(need_cnt + (env_id == BR_ENV_NETHER_PORTAL ? 1 : 0)));
    ob[85] = __float2half((float)on_gnd);
    ob[86] = __float2half(sy);
    ob[87] = __float2half(cy);
    ob[88] = __float2half((float)dx * (1.0f / 65536.0f));
    ob[89] = __float2half((float)dy * (1.0f / 65536.0f));
    ob[90] = __float2half((float)dz * (1.0f / 65536.0f));
    ob[91] = __float2half((float)et / (float)max_ticks);
    ob[92] = __float2half(py * 0.05f);
    ob[93] = __float2half((float)env_id * 0.25f);
    for (int k = 94; k < BR_OBS_DIM; k++) ob[k] = __float2half(0.0f);

    qx[i] = x; qy[i] = y; qz[i] = z; vx[i] = dx; vy[i] = dy; vz[i] = dz;
    yawp[i] = yaw; gndp[i] = on_gnd; dtp[i] = dt; progp[i] = prog; eptp[i] = et;
    for (int d = 0; d < nd; d++) g_deltas[(size_t)d * N_total + i] = loc_d[d];
    g_ndelta[i] = (uint8_t)nd;

    rew_out[local]   = step_rew;
    term_out[local]  = (uint8_t)won;
    trunc_out[local] = (uint8_t)timed_out;

    int need_reset = won | timed_out;
    uint32_t rmask = __ballot_sync(0xffffffffu, need_reset);
    if (rmask) {
        int lane = threadIdx.x & 31, n = __popc(rmask), base = 0;
        if (lane == 0) base = atomicAdd(slow_cnt, n);
        base = __shfl_sync(0xffffffffu, base, 0);
        if (need_reset) slow_q[base + __popc(rmask & ((1u << lane) - 1u))] = i;
    }
    uint32_t wmask = __ballot_sync(0xffffffffu, won);
    if (wmask && (threadIdx.x & 31) == 0) atomicAdd(win_acc, (uint32_t)__popc(wmask));
}

__global__ void k_tr_slow_reset_drain(
    int env_id, const int *__restrict__ slow_q, const int *__restrict__ slow_cnt,
    uint64_t *rng_st,
    int32_t *qx, int32_t *qy, int32_t *qz,
    int32_t *vx, int32_t *vy, int32_t *vz,
    float *yaw, uint8_t *gnd, uint8_t *dticks, uint8_t *prog, uint16_t *ep_tick,
    uint8_t *ndelta, float *prev_dist
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int n = *slow_cnt;
    if (idx >= n) return;
    int i = slow_q[idx];
    d_reset_world(i, env_id, rng_st, qx, qy, qz, vx, vy, vz, yaw, gnd, dticks, prog, ep_tick, ndelta, prev_dist);
}

/* ============================================================================
 * PPO Sampling, GAE, Clipped Loss, ReLU Backward, AdamW + NVFP4/FP8 Requant
 * ============================================================================ */
__global__ void k_tr_sample_act(
    const __half *__restrict__ logits, const __half *__restrict__ vals,
    uint64_t *__restrict__ rng_st,
    uint8_t *__restrict__ act_out, float *__restrict__ logp_out, float *__restrict__ val_out,
    int offset, int Q
) {
    int local = blockIdx.x * blockDim.x + threadIdx.x;
    if (local >= Q) return;
    const __half *row = logits + (size_t)local * BR_NUM_ACTS;
    float lg[BR_NUM_ACTS], mx = -1e9f;
    #pragma unroll
    for (int k = 0; k < BR_NUM_ACTS; k++) { lg[k] = __half2float(row[k]); mx = fmaxf(mx, lg[k]); }
    float sum = 0.0f, p[BR_NUM_ACTS];
    #pragma unroll
    for (int k = 0; k < BR_NUM_ACTS; k++) { p[k] = expf(lg[k] - mx); sum += p[k]; }
    float inv = 1.0f / (sum + 1e-8f);
    uint64_t r = rng_st[offset + local];
    float u = (float)(tr_sm64(&r) & 0xFFFFFFu) * (1.0f / 16777216.0f);
    rng_st[offset + local] = r;
    float cdf = 0.0f; int act = BR_NUM_ACTS - 1;
    #pragma unroll
    for (int k = 0; k < BR_NUM_ACTS; k++) { cdf += p[k] * inv; if (u <= cdf) { act = k; break; } }
    act_out[local]  = (uint8_t)act;
    logp_out[local] = logf(fmaxf(p[act] * inv, 1e-8f));
    val_out[local]  = __half2float(vals[local]);
}

__global__ void k_tr_gae(
    const float *__restrict__ rew, const float *__restrict__ val,
    const uint8_t *__restrict__ term, const uint8_t *__restrict__ trunc,
    float *__restrict__ adv, float *__restrict__ ret,
    float *__restrict__ stat_sum, int N, int T, float gamma, float lam
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    float gae = 0.0f, next_v = val[(size_t)(T - 1) * N + i], rsum = 0.0f;
    for (int t = T - 1; t >= 0; t--) {
        size_t idx = (size_t)t * N + i;
        float r = rew[idx], v = val[idx];
        rsum += r;
        float boot = term[idx] ? 0.0f : 1.0f;
        float cont = (term[idx] | trunc[idx]) ? 0.0f : 1.0f;
        float delta = r + gamma * next_v * boot - v;
        gae = delta + gamma * lam * cont * gae;
        adv[idx] = gae;
        ret[idx] = gae + v;
        next_v = v;
    }
    atomicAdd(&stat_sum[0], rsum);
}

__global__ void k_tr_adv_stats(const float *adv, float *out_sum_sq, int M) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    float a = (i < M) ? adv[i] : 0.0f, a2 = a * a;
    #pragma unroll
    for (int off = 16; off > 0; off >>= 1) {
        a  += __shfl_down_sync(0xffffffffu, a, off);
        a2 += __shfl_down_sync(0xffffffffu, a2, off);
    }
    if ((threadIdx.x & 31) == 0) { atomicAdd(&out_sum_sq[0], a); atomicAdd(&out_sum_sq[1], a2); }
}

__global__ void k_tr_ppo_loss_grad(
    const __half *__restrict__ logits, const __half *__restrict__ vals,
    const uint8_t *__restrict__ acts, const float *__restrict__ old_logp,
    const float *__restrict__ adv, const float *__restrict__ ret,
    const float *__restrict__ adv_stats, int total_samples,
    __half *__restrict__ dlogits, __half *__restrict__ dvals,
    float *__restrict__ loss_acc, int B,
    float clip_eps, float ent_coef, float gscale
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= B) return;
    float mean = adv_stats[0] / (float)total_samples;
    float var  = fmaxf(adv_stats[1] / (float)total_samples - mean * mean, 1e-6f);
    float a_norm = (adv[i] - mean) * rsqrtf(var + 1e-6f);

    const __half *row = logits + (size_t)i * BR_NUM_ACTS;
    float lg[BR_NUM_ACTS], mx = -1e9f;
    #pragma unroll
    for (int k = 0; k < BR_NUM_ACTS; k++) { lg[k] = __half2float(row[k]); mx = fmaxf(mx, lg[k]); }
    float sum = 0.0f, p[BR_NUM_ACTS];
    #pragma unroll
    for (int k = 0; k < BR_NUM_ACTS; k++) { p[k] = expf(lg[k] - mx); sum += p[k]; }
    float inv = 1.0f / (sum + 1e-8f), ent = 0.0f;
    #pragma unroll
    for (int k = 0; k < BR_NUM_ACTS; k++) { p[k] *= inv; ent -= p[k] * logf(fmaxf(p[k], 1e-8f)); }
    int act = acts[i] & 15;
    float logp = logf(fmaxf(p[act], 1e-8f));
    float ratio = expf(fminf(fmaxf(logp - old_logp[i], -5.0f), 5.0f));
    float lo = 1.0f - clip_eps, hi = 1.0f + clip_eps;
    float clipped = (a_norm > 0.0f && ratio > hi) || (a_norm < 0.0f && ratio < lo);
    float coeff = clipped ? 0.0f : (-ratio * a_norm);

    __half *drow = dlogits + (size_t)i * BR_NUM_ACTS;
    #pragma unroll
    for (int k = 0; k < BR_NUM_ACTS; k++) {
        float ind = (k == act) ? 1.0f : 0.0f;
        float g_pg  = coeff * (ind - p[k]);
        float g_ent = ent_coef * p[k] * (logf(fmaxf(p[k], 1e-8f)) + ent);
        drow[k] = __float2half((g_pg + g_ent) * gscale);
    }
    float v = __half2float(vals[i]), dv = (v - ret[i]);
    dvals[i] = __float2half(0.5f * dv * gscale);

    float pl = -fminf(ratio * a_norm, fminf(fmaxf(ratio, lo), hi) * a_norm);
    float vl = 0.5f * dv * dv;
    #pragma unroll
    for (int off = 16; off > 0; off >>= 1) {
        pl += __shfl_down_sync(0xffffffffu, pl, off);
        vl += __shfl_down_sync(0xffffffffu, vl, off);
        ent += __shfl_down_sync(0xffffffffu, ent, off);
    }
    if ((threadIdx.x & 31) == 0) {
        atomicAdd(&loss_acc[0], pl); atomicAdd(&loss_acc[1], vl); atomicAdd(&loss_acc[2], ent);
    }
}

/* Vectorized 8-half ReLU backward (uint4 loads/stores -> 2x memory throughput) */
__global__ void k_tr_relu_bwd(__half *__restrict__ dact, const __half *__restrict__ act, int n) {
    int i = (blockIdx.x * blockDim.x + threadIdx.x) * 8;
    if (i + 7 < n) {
        uint4 da = *(const uint4 *)(dact + i);
        uint4 ac = *(const uint4 *)(act  + i);
        __half2 *dh = (__half2 *)&da;
        const __half2 *ah = (const __half2 *)&ac;
        const __half2 zero2 = __float2half2_rn(0.0f);
        #pragma unroll
        for (int k = 0; k < 4; k++) {
            float2 a = __half22float2(ah[k]);
            float2 d = __half22float2(dh[k]);
            if (a.x <= 0.0f) d.x = 0.0f;
            if (a.y <= 0.0f) d.y = 0.0f;
            dh[k] = __float22half2_rn(d);
        }
        (void)zero2;
        *(uint4 *)(dact + i) = da;
    } else {
        for (int j = i; j < n; j++) if (__half2float(act[j]) <= 0.0f) dact[j] = __float2half(0.0f);
    }
}

/* Warp-Coalesced AdamW + Swizzled NVFP4 Requant:
 * 16 consecutive threads per 16-element block (100% coalesced 128B transactions + 4-cycle half-warp shuffle!) */
__global__ void k_tr_adamw_requant_swz(
    float *__restrict__ w32, float *__restrict__ m1, float *__restrict__ m2,
    const __half *__restrict__ grad, __half *__restrict__ w16,
    uint8_t *__restrict__ w_q, uint8_t *__restrict__ w_sf,
    int rows, int K, float lr, float bc1, float bc2, int do_nvfp4
) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    int total = rows * K;
    if (tid >= total) return;
    float g = fminf(fmaxf(__half2float(grad[tid]), -1.0f), 1.0f);
    float mm = 0.9f * m1[tid] + 0.1f * g;
    float vv = 0.999f * m2[tid] + 0.001f * g * g;
    float w = w32[tid] - lr * (mm * bc1) / (sqrtf(vv * bc2) + 1e-8f);
    w32[tid] = w; m1[tid] = mm; m2[tid] = vv;
    w16[tid] = __float2half(w);
    if (do_nvfp4 && w_q && w_sf) {
        float am = fabsf(w);
        am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 8));
        am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 4));
        am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 2));
        am = fmaxf(am, __shfl_xor_sync(0xffffffffu, am, 1));
        uint8_t sf = tr_f32_to_ue4m3(am * (1.0f / 6.0f));
        float inv = 1.0f / tr_ue4m3_to_f32(sf);
        float ws = w * inv;
        float partner = __shfl_xor_sync(0xffffffffu, ws, 1);
        int r = tid / K, c = tid - r * K;
        int kb = c >> 4, lane16 = c & 15;
        if (lane16 == 0) {
            int nkt = ((K >> 4) + 3) >> 2;
            w_sf[tr_swz_idx(r, kb, nkt)] = sf;
        }
        if ((lane16 & 1) == 0) {
            w_q[(size_t)r * (K >> 1) + (c >> 1)] = tr_cvt_e2m1x2(ws, partner);
        }
    }
}

/* ============================================================================
 * cublasLt Plan Builders (NVFP4, FP8 E4M3, FP16)
 * ============================================================================ */
typedef struct {
    cublasLtMatmulDesc_t desc;
    cublasLtMatrixLayout_t lA, lB, lD;
    cublasLtMatmulAlgo_t algo;
    int valid;
} lt_plan_t;

static lt_plan_t make_lt_nvfp4_plan(cublasLtHandle_t lt, int M, int N, int K,
                                    const uint8_t *dSA, const uint8_t *dSB,
                                    int relu_epilogue, size_t ws_bytes) {
    lt_plan_t p; memset(&p, 0, sizeof(p));
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

static lt_plan_t make_lt_fp8_plan(cublasLtHandle_t lt, int M, int N, int K,
                                  const float *dSA, const float *dSB, size_t ws_bytes) {
    lt_plan_t p; memset(&p, 0, sizeof(p));
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

static lt_plan_t make_lt_fp16_plan(cublasLtHandle_t lt, cublasOperation_t opA, cublasOperation_t opB,
                                   int M, int N, int K, int rowsA, int colsA, int rowsB, int colsB,
                                   int relu_epilogue, size_t ws_bytes) {
    lt_plan_t p; memset(&p, 0, sizeof(p));
    cublasLtMatmulDescCreate(&p.desc, CUBLAS_COMPUTE_32F, CUDA_R_32F);
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_TRANSA, &opA, sizeof(opA));
    cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_TRANSB, &opB, sizeof(opB));
    if (relu_epilogue) {
        cublasLtEpilogue_t ep = CUBLASLT_EPILOGUE_RELU;
        cublasLtMatmulDescSetAttribute(p.desc, CUBLASLT_MATMUL_DESC_EPILOGUE, &ep, sizeof(ep));
    }
    cublasLtMatrixLayoutCreate(&p.lA, CUDA_R_16F, rowsA, colsA, rowsA);
    cublasLtMatrixLayoutCreate(&p.lB, CUDA_R_16F, rowsB, colsB, rowsB);
    cublasLtMatrixLayoutCreate(&p.lD, CUDA_R_16F, M, N, M);
    cublasLtMatmulPreference_t pref; cublasLtMatmulPreferenceCreate(&pref);
    cublasLtMatmulPreferenceSetAttribute(pref, CUBLASLT_MATMUL_PREF_MAX_WORKSPACE_BYTES, &ws_bytes, sizeof(ws_bytes));
    cublasLtMatmulHeuristicResult_t hr; int found = 0;
    if (cublasLtMatmulAlgoGetHeuristic(lt, p.desc, p.lA, p.lB, p.lD, p.lD, pref, 1, &hr, &found) == CUBLAS_STATUS_SUCCESS && found > 0) {
        p.algo = hr.algo; p.valid = 1;
    }
    cublasLtMatmulPreferenceDestroy(pref);
    return p;
}

static void destroy_lt_plan(lt_plan_t *p) {
    if (p->lA) cublasLtMatrixLayoutDestroy(p->lA);
    if (p->lB) cublasLtMatrixLayoutDestroy(p->lB);
    if (p->lD) cublasLtMatrixLayoutDestroy(p->lD);
    if (p->desc) cublasLtMatmulDescDestroy(p->desc);
}

int br_check_nvfp4_gemm(double *out_cos_sim, double *out_rmse) {
    const int M = 256, N = 256, K = 128;
    float *hA = (float *)malloc(M*K*4), *hB = (float *)malloc(N*K*4);
    for (int i = 0; i < M*K; i++) hA[i] = sinf((float)i * 0.13f) * (1.0f + (float)((i/16)%5));
    for (int i = 0; i < N*K; i++) hB[i] = cosf((float)i * 0.17f) * (0.5f + (float)((i/16)%3));
    size_t szSA = tr_swz_bytes(M, K), szSB = tr_swz_bytes(N, K);
    uint8_t *qA = (uint8_t *)calloc(M*K/2, 1), *qB = (uint8_t *)calloc(N*K/2, 1);
    uint8_t *sA = (uint8_t *)calloc(szSA, 1),  *sB = (uint8_t *)calloc(szSB, 1);
    int nkt = ((K/16) + 3) / 4;
    auto qhost = [&](const float *X, int rows, uint8_t *q, uint8_t *sf) {
        for (int r = 0; r < rows; r++) for (int kb = 0; kb < K/16; kb++) {
            float am = 0.f; for (int k = 0; k < 16; k++) am = fmaxf(am, fabsf(X[r*K+kb*16+k]));
            uint8_t s = tr_f32_to_ue4m3(am / 6.f); float inv = 1.f / tr_ue4m3_to_f32(s);
            sf[tr_swz_idx(r, kb, nkt)] = s;
            for (int k = 0; k < 16; k += 2)
                q[r*(K/2)+(kb*16+k)/2] = (uint8_t)(tr_f32_to_e2m1_host(X[r*K+kb*16+k]*inv) | (tr_f32_to_e2m1_host(X[r*K+kb*16+k+1]*inv) << 4));
        }
    };
    qhost(hA, M, qA, sA); qhost(hB, N, qB, sB);

    uint8_t *dA, *dB, *dSA, *dSB; __half *dD; void *ws; size_t wsz = 4*1024*1024;
    cudaMalloc(&dA, M*K/2); cudaMalloc(&dB, N*K/2); cudaMalloc(&dSA, szSA); cudaMalloc(&dSB, szSB);
    cudaMalloc(&dD, M*N*2); cudaMalloc(&ws, wsz);
    cudaMemcpy(dA, qA, M*K/2, cudaMemcpyHostToDevice); cudaMemcpy(dB, qB, N*K/2, cudaMemcpyHostToDevice);
    cudaMemcpy(dSA, sA, szSA, cudaMemcpyHostToDevice); cudaMemcpy(dSB, sB, szSB, cudaMemcpyHostToDevice);

    cublasLtHandle_t lt; cublasLtCreate(&lt);
    lt_plan_t p = make_lt_nvfp4_plan(lt, M, N, K, dSA, dSB, 1, wsz);
    float alpha = 1.f, beta = 0.f;
    cublasLtMatmul(lt, p.desc, &alpha, dA, p.lA, dB, p.lB, &beta, dD, p.lD, dD, p.lD, &p.algo, ws, wsz, 0);
    cudaDeviceSynchronize();
    __half *hD = (__half *)malloc(M*N*2); cudaMemcpy(hD, dD, M*N*2, cudaMemcpyDeviceToHost);
    double dot = 0, n1 = 0, n2 = 0, sq = 0;
    for (int n = 0; n < N; n++) for (int m = 0; m < M; m++) {
        float r = 0.f; for (int k = 0; k < K; k++) r += hA[m*K+k] * hB[n*K+k];
        if (r < 0.f) r = 0.f;
        float g = __half2float(hD[n*M + m]);
        dot += r*g; n1 += r*r; n2 += g*g; sq += (r-g)*(r-g);
    }
    *out_cos_sim = dot / (sqrt(n1) * sqrt(n2));
    *out_rmse    = sqrt(sq / (double)(M * N));

    destroy_lt_plan(&p); cublasLtDestroy(lt);
    cudaFree(dA); cudaFree(dB); cudaFree(dSA); cudaFree(dSB); cudaFree(dD); cudaFree(ws);
    free(hA); free(hB); free(qA); free(qB); free(sA); free(sB); free(hD);
    return (*out_cos_sim >= 0.995) ? 0 : 1;
}

void br_bench_square_and_queue(void) {
    cudaStream_t st; cudaStreamCreate(&st);
    cublasLtHandle_t lt; cublasLtCreate(&lt);
    size_t wsz = 32 * 1024 * 1024; void *ws; cudaMalloc(&ws, wsz);

    const int Hs[] = {1024, 2048, 4096};
    const int M = 8192;
    for (int hi = 0; hi < 3; hi++) {
        int H = Hs[hi];
        uint8_t *dA, *dB, *dSA, *dSB; __half *dD;
        cudaMalloc(&dA, (size_t)H*H/2); cudaMalloc(&dB, (size_t)H*M/2);
        cudaMalloc(&dSA, tr_swz_bytes(H, H)); cudaMalloc(&dSB, tr_swz_bytes(M, H));
        cudaMalloc(&dD, (size_t)H*M*2);
        cudaMemset(dA, 0x22, (size_t)H*H/2); cudaMemset(dB, 0x22, (size_t)H*M/2);
        cudaMemset(dSA, 0x38, tr_swz_bytes(H, H)); cudaMemset(dSB, 0x38, tr_swz_bytes(M, H));

        lt_plan_t p = make_lt_nvfp4_plan(lt, H, M, H, dSA, dSB, 1, wsz);
        float alpha = 1.f, beta = 0.f;
        cublasLtMatmul(lt, p.desc, &alpha, dA, p.lA, dB, p.lB, &beta, dD, p.lD, dD, p.lD, &p.algo, ws, wsz, st);
        cudaStreamSynchronize(st);
        int iters = (H >= 4096) ? 40 : 100;
        double best = 1e18;
        for (int r = 0; r < 5; r++) {
            double t0 = tr_now_ns();
            for (int k = 0; k < iters; k++)
                cublasLtMatmul(lt, p.desc, &alpha, dA, p.lA, dB, p.lB, &beta, dD, p.lD, dD, p.lD, &p.algo, ws, wsz, st);
            cudaStreamSynchronize(st);
            double us = (tr_now_ns() - t0) / (1000.0 * iters);
            if (us < best) best = us;
        }
        double tflops = (2.0 * (double)M * (double)H * (double)H) / (best * 1e6);
        printf("SQUARE_NVFP4_RELU\tH=%d\tM=%d\tus=%.2f\tTFLOPS=%.1f\tpct_spec=%.1f\tMpol_layer_s=%.1f\n",
               H, M, best, tflops, 100.0 * tflops / 2015.0, (double)M / best);
        destroy_lt_plan(&p);
        cudaFree(dA); cudaFree(dB); cudaFree(dSA); cudaFree(dSB); cudaFree(dD);
    }
    cudaFree(ws); cublasLtDestroy(lt); cudaStreamDestroy(st);
}

/* ============================================================================
 * Trainer State & Backward Pass Routes (FP16 | FP8 E4M3 | NVFP4 Stochastic-Round)
 * ============================================================================ */
struct br_trainer {
    br_train_cfg_t cfg;
    int num_seeds, n_chunks;
    cudaStream_t stA, stB;
    cudaEvent_t  evFast;
    cublasLtHandle_t lt;
    void *d_ws; size_t ws_bytes;

    uint8_t *d_seed_blocks; uint32_t *d_seed_bits;
    uint64_t *d_rng;
    int32_t *d_qx, *d_qy, *d_qz, *d_vx, *d_vy, *d_vz;
    float *d_yaw, *d_prev_dist;
    uint8_t *d_gnd, *d_dt, *d_prog, *d_ndelta;
    uint16_t *d_ept;
    uint32_t *d_deltas;
    int *d_slow_q, *d_slow_cnt;

    __half  *d_obs_buf;
    uint8_t *d_act_buf, *d_term_buf, *d_trunc_buf;
    float   *d_rew_buf, *d_logp_buf, *d_val_buf, *d_adv_buf, *d_ret_buf;

    /* Forward NVFP4 working buffers */
    uint8_t *d_obs_q, *d_obs_sf, *d_h1_q, *d_h1_sf;
    __half  *d_h1, *d_h2, *d_logits, *d_vals;
    __half  *d_dlogits, *d_dvals, *d_dh2, *d_dh1;

    /* Backward FP8 E4M3 buffers & 1-step delayed scales [0=W2, 1=dh2, 2=h1] */
    uint8_t *d_W2T_f8, *d_dh2_f8, *d_dh2T_f8, *d_h1T_f8, *d_dh1T_f8, *d_obT_f8;
    float   *d_fp8_sc, *d_fp8_nxt;

    /* Backward NVFP4 buffers (packed e2m1 + 128x4 swizzled UE4M3) */
    uint8_t *d_W2T_q, *d_W2T_sf, *d_dh2_q, *d_dh2_sf;
    uint8_t *d_dh2T_q, *d_dh2T_sf, *d_h1T_q, *d_h1T_sf;
    uint8_t *d_dh1T_q, *d_dh1T_sf, *d_obT_q, *d_obT_sf;

    /* Weights */
    float *d_W1_32, *d_W1_m1, *d_W1_m2; __half *d_W1_16, *d_gW1; uint8_t *d_W1_q, *d_W1_sf;
    float *d_W2_32, *d_W2_m1, *d_W2_m2; __half *d_W2_16, *d_gW2; uint8_t *d_W2_q, *d_W2_sf;
    float *d_Wp_32, *d_Wp_m1, *d_Wp_m2; __half *d_Wp_16, *d_gWp;
    float *d_Wv_32, *d_Wv_m1, *d_Wv_m2; __half *d_Wv_16, *d_gWv;

    /* Plans */
    lt_plan_t p_f1_nv4, p_f2_nv4, p_f1_f16, p_f2_f16, p_fp_f16, p_fv_f16;
    lt_plan_t p_bp_d, p_bp_w, p_bv_w;
    lt_plan_t p_b2_d_f16, p_b2_w_f16, p_b1_w_f16;
    lt_plan_t p_b2_d_f8,  p_b2_w_f8,  p_b1_w_f8;
    lt_plan_t p_b2_d_nv4, p_b2_w_nv4, p_b1_w_nv4;

    uint32_t *d_win_acc;
    float *d_stat_sum, *d_adv_stats, *d_loss_acc;
    int adam_step;
};

static void init_weight_block(float **d32, float **dm1, float **dm2, __half **d16, __half **dg,
                              uint8_t **dq, uint8_t **dsf, int rows, int K, float scale, uint64_t *rng) {
    size_t n = (size_t)rows * K;
    float *h32 = (float *)malloc(n * 4);
    __half *h16 = (__half *)malloc(n * 2);
    for (size_t i = 0; i < n; i++) {
        float s = 0.0f;
        for (int j = 0; j < 6; j++) s += ((float)(tr_sm64(rng) & 0xFFFF) / 65536.0f) - 0.5f;
        h32[i] = s * 1.4142135f * scale;
        h16[i] = __float2half(h32[i]);
    }
    cudaMalloc(d32, n*4); cudaMalloc(dm1, n*4); cudaMalloc(dm2, n*4);
    cudaMalloc(d16, n*2); cudaMalloc(dg, n*2);
    cudaMemcpy(*d32, h32, n*4, cudaMemcpyHostToDevice);
    cudaMemcpy(*d16, h16, n*2, cudaMemcpyHostToDevice);
    cudaMemset(*dm1, 0, n*4); cudaMemset(*dm2, 0, n*4);
    if (dq && dsf) {
        size_t qsz = (size_t)rows * (K / 2), ssz = tr_swz_bytes(rows, K);
        uint8_t *hq = (uint8_t *)calloc(qsz, 1), *hs = (uint8_t *)calloc(ssz, 1);
        int nkt = ((K / 16) + 3) / 4;
        for (int r = 0; r < rows; r++) for (int kb = 0; kb < K / 16; kb++) {
            float am = 0.f; for (int k = 0; k < 16; k++) am = fmaxf(am, fabsf(h32[r*K+kb*16+k]));
            uint8_t sf = tr_f32_to_ue4m3(am / 6.f); float inv = 1.f / tr_ue4m3_to_f32(sf);
            hs[tr_swz_idx(r, kb, nkt)] = sf;
            for (int k = 0; k < 16; k += 2)
                hq[r*(K/2)+(kb*16+k)/2] = (uint8_t)(tr_f32_to_e2m1_host(h32[r*K+kb*16+k]*inv) | (tr_f32_to_e2m1_host(h32[r*K+kb*16+k+1]*inv) << 4));
        }
        cudaMalloc(dq, qsz); cudaMalloc(dsf, ssz);
        cudaMemcpy(*dq, hq, qsz, cudaMemcpyHostToDevice);
        cudaMemcpy(*dsf, hs, ssz, cudaMemcpyHostToDevice);
        free(hq); free(hs);
    }
    free(h32); free(h16);
}

br_trainer_t *br_trainer_create(const br_train_cfg_t *cfg) {
    br_trainer_t *tr = (br_trainer_t *)calloc(1, sizeof(br_trainer_t));
    tr->cfg = *cfg;
    if (tr->cfg.chunk_q <= 0 || tr->cfg.chunk_q > tr->cfg.n_worlds) tr->cfg.chunk_q = tr->cfg.n_worlds;
    if (tr->cfg.cow_seeds <= 0) tr->cfg.cow_seeds = 64;
    if (tr->cfg.persisting_l2_mb <= 0) tr->cfg.persisting_l2_mb = 32;
    if (tr->cfg.episode_max_ticks <= 0) tr->cfg.episode_max_ticks = ENV_MAX_TICKS;
    if (tr->cfg.ent_coef <= 0.0f) tr->cfg.ent_coef = 0.01f;
    if (tr->cfg.clip_eps <= 0.0f) tr->cfg.clip_eps = 0.2f;
    if (tr->cfg.gamma <= 0.0f) tr->cfg.gamma = 0.99f;
    if (tr->cfg.gae_lam <= 0.0f) tr->cfg.gae_lam = 0.95f;
    if (tr->cfg.grad_loss_scale <= 0.0f) tr->cfg.grad_loss_scale = 16.0f;
    tr->n_chunks = tr->cfg.n_worlds / tr->cfg.chunk_q;
    tr->num_seeds = tr->cfg.cow_seeds;

    cudaStreamCreate(&tr->stA); cudaStreamCreate(&tr->stB);
    cudaEventCreateWithFlags(&tr->evFast, cudaEventDisableTiming);
    cublasLtCreate(&tr->lt);
    tr->ws_bytes = 32 * 1024 * 1024;
    cudaMalloc(&tr->d_ws, tr->ws_bytes);

    uint8_t *sb = (uint8_t *)malloc((size_t)tr->num_seeds * BR_BLOCKS);
    uint32_t *sm = (uint32_t *)malloc((size_t)tr->num_seeds * BR_BIT_WORDS * 4);
    br_gen_seeds(sb, sm, tr->num_seeds, cfg->seed ? cfg->seed : 0x20261123ULL);
    cudaMalloc(&tr->d_seed_blocks, (size_t)tr->num_seeds * BR_BLOCKS);
    cudaMalloc(&tr->d_seed_bits,   (size_t)tr->num_seeds * BR_BIT_WORDS * 4);
    cudaMemcpy(tr->d_seed_blocks, sb, (size_t)tr->num_seeds * BR_BLOCKS, cudaMemcpyHostToDevice);
    cudaMemcpy(tr->d_seed_bits,   sm, (size_t)tr->num_seeds * BR_BIT_WORDS * 4, cudaMemcpyHostToDevice);
    free(sb); free(sm);

    cudaDeviceSetLimit(cudaLimitPersistingL2CacheSize, (size_t)tr->cfg.persisting_l2_mb * 1024 * 1024);
    cudaStreamAttrValue attr; memset(&attr, 0, sizeof(attr));
    attr.accessPolicyWindow.base_ptr  = tr->d_seed_blocks;
    attr.accessPolicyWindow.num_bytes = (size_t)tr->num_seeds * BR_BLOCKS;
    attr.accessPolicyWindow.hitRatio  = 1.0f;
    attr.accessPolicyWindow.hitProp   = cudaAccessPropertyPersisting;
    attr.accessPolicyWindow.missProp  = cudaAccessPropertyStreaming;
    cudaStreamSetAttribute(tr->stA, cudaStreamAttributeAccessPolicyWindow, &attr);

    int N = cfg->n_worlds, Q = tr->cfg.chunk_q, T = cfg->rollout_t, H = cfg->hidden;
    cudaMalloc(&tr->d_rng, N*8);
    cudaMalloc(&tr->d_qx, N*4); cudaMalloc(&tr->d_qy, N*4); cudaMalloc(&tr->d_qz, N*4);
    cudaMalloc(&tr->d_vx, N*4); cudaMalloc(&tr->d_vy, N*4); cudaMalloc(&tr->d_vz, N*4);
    cudaMalloc(&tr->d_yaw, N*4); cudaMalloc(&tr->d_prev_dist, N*4);
    cudaMalloc(&tr->d_gnd, N); cudaMalloc(&tr->d_dt, N); cudaMalloc(&tr->d_prog, N); cudaMalloc(&tr->d_ndelta, N);
    cudaMalloc(&tr->d_ept, N*2);
    cudaMalloc(&tr->d_deltas, (size_t)BR_DELTA_CAP * N * 4);
    cudaMalloc(&tr->d_slow_q, N*4); cudaMalloc(&tr->d_slow_cnt, 4);

    size_t TN = (size_t)T * N;
    cudaMalloc(&tr->d_obs_buf, TN * BR_OBS_DIM * 2);
    cudaMalloc(&tr->d_act_buf, TN); cudaMalloc(&tr->d_term_buf, TN); cudaMalloc(&tr->d_trunc_buf, TN);
    cudaMalloc(&tr->d_rew_buf, TN*4); cudaMalloc(&tr->d_logp_buf, TN*4);
    cudaMalloc(&tr->d_val_buf, TN*4); cudaMalloc(&tr->d_adv_buf, TN*4); cudaMalloc(&tr->d_ret_buf, TN*4);
    cudaMemset(tr->d_act_buf, 1, TN);

    cudaMalloc(&tr->d_obs_q,  (size_t)Q * (BR_OBS_DIM / 2));
    cudaMalloc(&tr->d_obs_sf, tr_swz_bytes(Q, BR_OBS_DIM));
    cudaMalloc(&tr->d_h1_q,   (size_t)Q * (H / 2));
    cudaMalloc(&tr->d_h1_sf,  tr_swz_bytes(Q, H));
    cudaMalloc(&tr->d_h1,      (size_t)Q * H * 2);
    cudaMalloc(&tr->d_h2,      (size_t)Q * H * 2);
    cudaMalloc(&tr->d_logits,  (size_t)Q * BR_NUM_ACTS * 2);
    cudaMalloc(&tr->d_vals,    (size_t)Q * 2);
    cudaMalloc(&tr->d_dlogits, (size_t)Q * BR_NUM_ACTS * 2);
    cudaMalloc(&tr->d_dvals,   (size_t)Q * 2);
    cudaMalloc(&tr->d_dh2,     (size_t)Q * H * 2);
    cudaMalloc(&tr->d_dh1,     (size_t)Q * H * 2);

    /* Backward FP8 buffers + 3 delayed scale pairs [0=W2, 1=dh2, 2=h1] */
    cudaMalloc(&tr->d_W2T_f8,  (size_t)H * H);
    cudaMalloc(&tr->d_dh2_f8,  (size_t)Q * H);
    cudaMalloc(&tr->d_dh2T_f8, (size_t)H * Q);
    cudaMalloc(&tr->d_h1T_f8,  (size_t)H * Q);
    cudaMalloc(&tr->d_dh1T_f8, (size_t)H * Q);
    cudaMalloc(&tr->d_obT_f8,  (size_t)BR_OBS_DIM * Q);
    cudaMalloc(&tr->d_fp8_sc,  3 * sizeof(float));
    cudaMalloc(&tr->d_fp8_nxt, 3 * sizeof(float));
    cudaMemset(tr->d_fp8_sc,  0, 3 * sizeof(float));
    cudaMemset(tr->d_fp8_nxt, 0, 3 * sizeof(float));

    /* Backward NVFP4 buffers */
    cudaMalloc(&tr->d_W2T_q,  (size_t)H * (H / 2));         cudaMalloc(&tr->d_W2T_sf,  tr_swz_bytes(H, H));
    cudaMalloc(&tr->d_dh2_q,  (size_t)Q * (H / 2));         cudaMalloc(&tr->d_dh2_sf,  tr_swz_bytes(Q, H));
    cudaMalloc(&tr->d_dh2T_q, (size_t)H * (Q / 2));         cudaMalloc(&tr->d_dh2T_sf, tr_swz_bytes(H, Q));
    cudaMalloc(&tr->d_h1T_q,  (size_t)H * (Q / 2));         cudaMalloc(&tr->d_h1T_sf,  tr_swz_bytes(H, Q));
    cudaMalloc(&tr->d_dh1T_q, (size_t)H * (Q / 2));         cudaMalloc(&tr->d_dh1T_sf, tr_swz_bytes(H, Q));
    cudaMalloc(&tr->d_obT_q,  (size_t)BR_OBS_DIM * (Q / 2));cudaMalloc(&tr->d_obT_sf,  tr_swz_bytes(BR_OBS_DIM, Q));

    uint64_t wrng = cfg->seed ? cfg->seed : 0x7777ULL;
    init_weight_block(&tr->d_W1_32, &tr->d_W1_m1, &tr->d_W1_m2, &tr->d_W1_16, &tr->d_gW1, &tr->d_W1_q, &tr->d_W1_sf, H, BR_OBS_DIM, sqrtf(2.0f / BR_OBS_DIM), &wrng);
    init_weight_block(&tr->d_W2_32, &tr->d_W2_m1, &tr->d_W2_m2, &tr->d_W2_16, &tr->d_gW2, &tr->d_W2_q, &tr->d_W2_sf, H, H, sqrtf(2.0f / H), &wrng);
    init_weight_block(&tr->d_Wp_32, &tr->d_Wp_m1, &tr->d_Wp_m2, &tr->d_Wp_16, &tr->d_gWp, NULL, NULL, BR_NUM_ACTS, H, 0.05f / sqrtf((float)H), &wrng);
    init_weight_block(&tr->d_Wv_32, &tr->d_Wv_m1, &tr->d_Wv_m2, &tr->d_Wv_16, &tr->d_gWv, NULL, NULL, 1, H, 0.1f / sqrtf((float)H), &wrng);

    /* Forward plans */
    tr->p_f1_nv4 = make_lt_nvfp4_plan(tr->lt, H, Q, BR_OBS_DIM, tr->d_W1_sf, tr->d_obs_sf, 1, tr->ws_bytes);
    tr->p_f2_nv4 = make_lt_nvfp4_plan(tr->lt, H, Q, H,          tr->d_W2_sf, tr->d_h1_sf,  1, tr->ws_bytes);
    tr->p_f1_f16 = make_lt_fp16_plan(tr->lt, CUBLAS_OP_T, CUBLAS_OP_N, H, Q, BR_OBS_DIM, BR_OBS_DIM, H, BR_OBS_DIM, Q, 1, tr->ws_bytes);
    tr->p_f2_f16 = make_lt_fp16_plan(tr->lt, CUBLAS_OP_T, CUBLAS_OP_N, H, Q, H,          H,          H, H,          Q, 1, tr->ws_bytes);
    tr->p_fp_f16 = make_lt_fp16_plan(tr->lt, CUBLAS_OP_T, CUBLAS_OP_N, BR_NUM_ACTS, Q, H, H, BR_NUM_ACTS, H, Q, 0, tr->ws_bytes);
    tr->p_fv_f16 = make_lt_fp16_plan(tr->lt, CUBLAS_OP_T, CUBLAS_OP_N, 1,           Q, H, H, 1,           H, Q, 0, tr->ws_bytes);

    /* Head backward plans (always FP16/FP32 per NVFP4 paper!) */
    tr->p_bp_d = make_lt_fp16_plan(tr->lt, CUBLAS_OP_N, CUBLAS_OP_N, H, Q, BR_NUM_ACTS, H, BR_NUM_ACTS, BR_NUM_ACTS, Q, 0, tr->ws_bytes);
    tr->p_bp_w = make_lt_fp16_plan(tr->lt, CUBLAS_OP_N, CUBLAS_OP_T, H, BR_NUM_ACTS, Q, H, Q, BR_NUM_ACTS, Q, 0, tr->ws_bytes);
    tr->p_bv_w = make_lt_fp16_plan(tr->lt, CUBLAS_OP_N, CUBLAS_OP_T, H, 1,           Q, H, Q, 1,           Q, 0, tr->ws_bytes);

    /* Trunk backward plans: FP16 | FP8 E4M3 | NVFP4 */
    tr->p_b2_d_f16 = make_lt_fp16_plan(tr->lt, CUBLAS_OP_N, CUBLAS_OP_N, H, Q, H,          H, H, H,          Q, 0, tr->ws_bytes);
    tr->p_b2_w_f16 = make_lt_fp16_plan(tr->lt, CUBLAS_OP_N, CUBLAS_OP_T, H, H, Q,          H, Q, H,          Q, 0, tr->ws_bytes);
    tr->p_b1_w_f16 = make_lt_fp16_plan(tr->lt, CUBLAS_OP_N, CUBLAS_OP_T, BR_OBS_DIM, H, Q, BR_OBS_DIM, Q, H, Q, 0, tr->ws_bytes);

    tr->p_b2_d_f8  = make_lt_fp8_plan(tr->lt, H, Q, H,          tr->d_fp8_sc + 0, tr->d_fp8_sc + 1, tr->ws_bytes);
    tr->p_b2_w_f8  = make_lt_fp8_plan(tr->lt, H, H, Q,          tr->d_fp8_sc + 2, tr->d_fp8_sc + 1, tr->ws_bytes);
    tr->p_b1_w_f8  = make_lt_fp8_plan(tr->lt, BR_OBS_DIM, H, Q, tr->d_fp8_sc + 2, tr->d_fp8_sc + 1, tr->ws_bytes);

    tr->p_b2_d_nv4 = make_lt_nvfp4_plan(tr->lt, H, Q, H,          tr->d_W2T_sf,  tr->d_dh2_sf,  0, tr->ws_bytes);
    tr->p_b2_w_nv4 = make_lt_nvfp4_plan(tr->lt, H, H, Q,          tr->d_h1T_sf,  tr->d_dh2T_sf, 0, tr->ws_bytes);
    tr->p_b1_w_nv4 = make_lt_nvfp4_plan(tr->lt, BR_OBS_DIM, H, Q, tr->d_obT_sf,  tr->d_dh1T_sf, 0, tr->ws_bytes);

    cudaMalloc(&tr->d_win_acc, 4);
    cudaMalloc(&tr->d_stat_sum, 8);
    cudaMalloc(&tr->d_adv_stats, 8);
    cudaMalloc(&tr->d_loss_acc, 16);

    k_tr_init_worlds<<<(N + 127) / 128, 128, 0, tr->stA>>>(
        cfg->env_id, tr->d_rng, cfg->seed ? cfg->seed : 0x12345ULL,
        tr->d_qx, tr->d_qy, tr->d_qz, tr->d_vx, tr->d_vy, tr->d_vz,
        tr->d_yaw, tr->d_gnd, tr->d_dt, tr->d_prog, tr->d_ept, tr->d_ndelta, tr->d_prev_dist, N);
    cudaStreamSynchronize(tr->stA);
    return tr;
}

static void run_forward_chunk(br_trainer_t *tr, const __half *obs_chunk) {
    int Q = tr->cfg.chunk_q, H = tr->cfg.hidden;
    float alpha = 1.0f, beta = 0.0f;
    if (tr->cfg.use_nvfp4 && tr->p_f1_nv4.valid && tr->p_f2_nv4.valid) {
        int nb0 = Q * (BR_OBS_DIM / 16);
        k_tr_quant_f16_to_nvfp4_swz<<<(nb0 + 255) / 256, 256, 0, tr->stA>>>(obs_chunk, tr->d_obs_q, tr->d_obs_sf, Q, BR_OBS_DIM, 0, 0);
        cublasLtMatmul(tr->lt, tr->p_f1_nv4.desc, &alpha, tr->d_W1_q, tr->p_f1_nv4.lA, tr->d_obs_q, tr->p_f1_nv4.lB, &beta, tr->d_h1, tr->p_f1_nv4.lD, tr->d_h1, tr->p_f1_nv4.lD, &tr->p_f1_nv4.algo, tr->d_ws, tr->ws_bytes, tr->stA);
        int nb1 = Q * (H / 16);
        k_tr_quant_f16_to_nvfp4_swz<<<(nb1 + 255) / 256, 256, 0, tr->stA>>>(tr->d_h1, tr->d_h1_q, tr->d_h1_sf, Q, H, 0, 0);
        cublasLtMatmul(tr->lt, tr->p_f2_nv4.desc, &alpha, tr->d_W2_q, tr->p_f2_nv4.lA, tr->d_h1_q, tr->p_f2_nv4.lB, &beta, tr->d_h2, tr->p_f2_nv4.lD, tr->d_h2, tr->p_f2_nv4.lD, &tr->p_f2_nv4.algo, tr->d_ws, tr->ws_bytes, tr->stA);
    } else {
        cublasLtMatmul(tr->lt, tr->p_f1_f16.desc, &alpha, tr->d_W1_16, tr->p_f1_f16.lA, obs_chunk, tr->p_f1_f16.lB, &beta, tr->d_h1, tr->p_f1_f16.lD, tr->d_h1, tr->p_f1_f16.lD, &tr->p_f1_f16.algo, tr->d_ws, tr->ws_bytes, tr->stA);
        cublasLtMatmul(tr->lt, tr->p_f2_f16.desc, &alpha, tr->d_W2_16, tr->p_f2_f16.lA, tr->d_h1,    tr->p_f2_f16.lB, &beta, tr->d_h2, tr->p_f2_f16.lD, tr->d_h2, tr->p_f2_f16.lD, &tr->p_f2_f16.algo, tr->d_ws, tr->ws_bytes, tr->stA);
    }
    cublasLtMatmul(tr->lt, tr->p_fp_f16.desc, &alpha, tr->d_Wp_16, tr->p_fp_f16.lA, tr->d_h2, tr->p_fp_f16.lB, &beta, tr->d_logits, tr->p_fp_f16.lD, tr->d_logits, tr->p_fp_f16.lD, &tr->p_fp_f16.algo, tr->d_ws, tr->ws_bytes, tr->stA);
    cublasLtMatmul(tr->lt, tr->p_fv_f16.desc, &alpha, tr->d_Wv_16, tr->p_fv_f16.lA, tr->d_h2, tr->p_fv_f16.lB, &beta, tr->d_vals,   tr->p_fv_f16.lD, tr->d_vals,   tr->p_fv_f16.lD, &tr->p_fv_f16.algo, tr->d_ws, tr->ws_bytes, tr->stA);
}

static void run_backward_trunk(br_trainer_t *tr, const __half *ob, uint32_t sr_seed) {
    int Q = tr->cfg.chunk_q, H = tr->cfg.hidden;
    float alpha_d = 1.0f, alpha_w = 1.0f / (tr->cfg.grad_loss_scale * (float)Q), beta = 0.0f;
    int relu_grd = ((Q * H + 7) / 8 + 255) / 256;

    if (tr->cfg.bwd_prec == BR_BWD_FP8 && tr->p_b2_d_f8.valid && tr->p_b2_w_f8.valid) {
        /* 1-step Delayed Scaling FP8 E4M3 (zero separate absmax pass after step 0!)
         * Keep skinny input layer b1_w (128 x H) in FP16 per NVFP4/FP8 training recipe! */
        if (tr->adam_step == 0) {
            cudaMemsetAsync(tr->d_fp8_sc, 0, 3 * sizeof(float), tr->stA);
            k_tr_absmax_f16<<<128, 256, 0, tr->stA>>>(tr->d_W2_16, tr->d_fp8_sc + 0, H * H);
            k_tr_absmax_f16<<<256, 256, 0, tr->stA>>>(tr->d_dh2,   tr->d_fp8_sc + 1, Q * H);
            k_tr_absmax_f16<<<256, 256, 0, tr->stA>>>(tr->d_h1,    tr->d_fp8_sc + 2, Q * H);
        }
        dim3 blkT(32, 8);
        k_tr_quant_trans_fp8<<<dim3((H+31)/32, (H+31)/32), blkT, 0, tr->stA>>>(tr->d_W2_16, tr->d_W2T_f8, tr->d_fp8_sc + 0, tr->d_fp8_nxt + 0, H, H);
        k_tr_quant_f16_to_fp8<<<(Q*H/2 + 255)/256, 256, 0, tr->stA>>>(tr->d_dh2, tr->d_dh2_f8, tr->d_fp8_sc + 1, tr->d_fp8_nxt + 1, Q * H);
        /* dgrad2: dH1 [H, Q] = opT(W2^T [H, H]) * opN(dH2 [H, Q]) */
        cublasLtMatmul(tr->lt, tr->p_b2_d_f8.desc, &alpha_d, tr->d_W2T_f8, tr->p_b2_d_f8.lA, tr->d_dh2_f8, tr->p_b2_d_f8.lB, &beta, tr->d_dh1, tr->p_b2_d_f8.lD, tr->d_dh1, tr->p_b2_d_f8.lD, &tr->p_b2_d_f8.algo, tr->d_ws, tr->ws_bytes, tr->stA);

        /* wgrad2: gW2 [H, H] = alpha_w * opT(H1^T [Q, H]) * opN(dH2^T [Q, H]) */
        k_tr_quant_trans_fp8<<<dim3((H+31)/32, (Q+31)/32), blkT, 0, tr->stA>>>(tr->d_h1,  tr->d_h1T_f8,  tr->d_fp8_sc + 2, tr->d_fp8_nxt + 2, Q, H);
        k_tr_quant_trans_fp8<<<dim3((H+31)/32, (Q+31)/32), blkT, 0, tr->stA>>>(tr->d_dh2, tr->d_dh2T_f8, tr->d_fp8_sc + 1, NULL,              Q, H);
        cublasLtMatmul(tr->lt, tr->p_b2_w_f8.desc, &alpha_w, tr->d_h1T_f8, tr->p_b2_w_f8.lA, tr->d_dh2T_f8, tr->p_b2_w_f8.lB, &beta, tr->d_gW2, tr->p_b2_w_f8.lD, tr->d_gW2, tr->p_b2_w_f8.lD, &tr->p_b2_w_f8.algo, tr->d_ws, tr->ws_bytes, tr->stA);
        k_tr_fp8_swap_scales<<<1, 32, 0, tr->stA>>>(tr->d_fp8_sc, tr->d_fp8_nxt);

        /* ReLU bwd + skinny input layer wgrad1 in FP16 (15 us, zero transpose/quant!) */
        k_tr_relu_bwd<<<relu_grd, 256, 0, tr->stA>>>(tr->d_dh1, tr->d_h1, Q * H);
        cublasLtMatmul(tr->lt, tr->p_b1_w_f16.desc, &alpha_w, ob, tr->p_b1_w_f16.lA, tr->d_dh1, tr->p_b1_w_f16.lB, &beta, tr->d_gW1, tr->p_b1_w_f16.lD, tr->d_gW1, tr->p_b1_w_f16.lD, &tr->p_b1_w_f16.algo, tr->d_ws, tr->ws_bytes, tr->stA);
    } else if (tr->cfg.bwd_prec == BR_BWD_NVFP4 && tr->p_b2_d_nv4.valid && tr->p_b2_w_nv4.valid) {
        /* NVFP4 Backward: 64x32 SMEM Tiled Coalesced Transpose + Unbiased SR on Gradients + FP16 skinny input layer! */
        dim3 blk4(32, 4);
        k_tr_quant_trans_nvfp4_swz<<<dim3((H+31)/32, (H+63)/64), blk4, 0, tr->stA>>>(tr->d_W2_16, tr->d_W2T_q, tr->d_W2T_sf, H, H, 0, sr_seed);
        k_tr_quant_f16_to_nvfp4_swz<<<(Q*(H/16) + 255)/256, 256, 0, tr->stA>>>(tr->d_dh2, tr->d_dh2_q, tr->d_dh2_sf, Q, H, 1, sr_seed + 1);
        cublasLtMatmul(tr->lt, tr->p_b2_d_nv4.desc, &alpha_d, tr->d_W2T_q, tr->p_b2_d_nv4.lA, tr->d_dh2_q, tr->p_b2_d_nv4.lB, &beta, tr->d_dh1, tr->p_b2_d_nv4.lD, tr->d_dh1, tr->p_b2_d_nv4.lD, &tr->p_b2_d_nv4.algo, tr->d_ws, tr->ws_bytes, tr->stA);

        k_tr_quant_trans_nvfp4_swz<<<dim3((H+31)/32, (Q+63)/64), blk4, 0, tr->stA>>>(tr->d_h1,  tr->d_h1T_q,  tr->d_h1T_sf,  Q, H, 0, sr_seed + 2);
        k_tr_quant_trans_nvfp4_swz<<<dim3((H+31)/32, (Q+63)/64), blk4, 0, tr->stA>>>(tr->d_dh2, tr->d_dh2T_q, tr->d_dh2T_sf, Q, H, 1, sr_seed + 3);
        cublasLtMatmul(tr->lt, tr->p_b2_w_nv4.desc, &alpha_w, tr->d_h1T_q, tr->p_b2_w_nv4.lA, tr->d_dh2T_q, tr->p_b2_w_nv4.lB, &beta, tr->d_gW2, tr->p_b2_w_nv4.lD, tr->d_gW2, tr->p_b2_w_nv4.lD, &tr->p_b2_w_nv4.algo, tr->d_ws, tr->ws_bytes, tr->stA);

        /* ReLU bwd + skinny input layer wgrad1 in FP16 (NVFP4 paper: keep first/last skinny layers higher precision!) */
        k_tr_relu_bwd<<<relu_grd, 256, 0, tr->stA>>>(tr->d_dh1, tr->d_h1, Q * H);
        cublasLtMatmul(tr->lt, tr->p_b1_w_f16.desc, &alpha_w, ob, tr->p_b1_w_f16.lA, tr->d_dh1, tr->p_b1_w_f16.lB, &beta, tr->d_gW1, tr->p_b1_w_f16.lD, tr->d_gW1, tr->p_b1_w_f16.lD, &tr->p_b1_w_f16.algo, tr->d_ws, tr->ws_bytes, tr->stA);
    } else {
        /* FP16 Backward baseline */
        cublasLtMatmul(tr->lt, tr->p_b2_d_f16.desc, &alpha_d, tr->d_W2_16, tr->p_b2_d_f16.lA, tr->d_dh2, tr->p_b2_d_f16.lB, &beta, tr->d_dh1, tr->p_b2_d_f16.lD, tr->d_dh1, tr->p_b2_d_f16.lD, &tr->p_b2_d_f16.algo, tr->d_ws, tr->ws_bytes, tr->stA);
        cublasLtMatmul(tr->lt, tr->p_b2_w_f16.desc, &alpha_w, tr->d_h1,    tr->p_b2_w_f16.lA, tr->d_dh2, tr->p_b2_w_f16.lB, &beta, tr->d_gW2, tr->p_b2_w_f16.lD, tr->d_gW2, tr->p_b2_w_f16.lD, &tr->p_b2_w_f16.algo, tr->d_ws, tr->ws_bytes, tr->stA);
        k_tr_relu_bwd<<<relu_grd, 256, 0, tr->stA>>>(tr->d_dh1, tr->d_h1, Q * H);
        cublasLtMatmul(tr->lt, tr->p_b1_w_f16.desc, &alpha_w, ob,          tr->p_b1_w_f16.lA, tr->d_dh1, tr->p_b1_w_f16.lB, &beta, tr->d_gW1, tr->p_b1_w_f16.lD, tr->d_gW1, tr->p_b1_w_f16.lD, &tr->p_b1_w_f16.algo, tr->d_ws, tr->ws_bytes, tr->stA);
    }
}

void br_trainer_step(br_trainer_t *tr, int iter, br_train_stats_t *out) {
    int N = tr->cfg.n_worlds, Q = tr->cfg.chunk_q, T = tr->cfg.rollout_t;
    int H = tr->cfg.hidden, K = tr->cfg.ticks_per_step;

    cudaMemsetAsync(tr->d_win_acc, 0, 4, tr->stA);
    cudaMemsetAsync(tr->d_stat_sum, 0, 8, tr->stA);
    cudaMemsetAsync(tr->d_adv_stats, 0, 8, tr->stA);
    cudaMemsetAsync(tr->d_loss_acc, 0, 16, tr->stA);
    cudaStreamSynchronize(tr->stA);

    double t0 = tr_now_ns();
    for (int s = 0; s < T; s++) {
        size_t s_off = (size_t)s * N;
        size_t prev_off = (size_t)(s > 0 ? s - 1 : T - 1) * N;
        for (int c = 0; c < tr->n_chunks; c++) {
            int off = c * Q;
            cudaMemsetAsync(tr->d_slow_cnt, 0, 4, tr->stA);
            k_tr_step_fast_split<<<(Q + 63) / 64, 64, 0, tr->stA>>>(
                tr->cfg.env_id, tr->d_seed_blocks, tr->d_seed_bits, tr->num_seeds,
                tr->d_qx, tr->d_qy, tr->d_qz, tr->d_vx, tr->d_vy, tr->d_vz,
                tr->d_yaw, tr->d_gnd, tr->d_dt, tr->d_prog, tr->d_ept, tr->d_deltas, tr->d_ndelta,
                tr->d_prev_dist, tr->d_act_buf + prev_off + off,
                tr->d_obs_buf + (s_off + off) * BR_OBS_DIM,
                tr->d_rew_buf + s_off + off, tr->d_term_buf + s_off + off,
                tr->d_trunc_buf + s_off + off, tr->d_slow_q, tr->d_slow_cnt,
                tr->d_win_acc, off, Q, N, K,
                tr->cfg.episode_max_ticks, tr->cfg.target_count, tr->cfg.break_ticks);
            cudaEventRecord(tr->evFast, tr->stA);
            cudaStreamWaitEvent(tr->stB, tr->evFast, 0);
            k_tr_slow_reset_drain<<<(Q / 8 + 127) / 128, 128, 0, tr->stB>>>(
                tr->cfg.env_id, tr->d_slow_q, tr->d_slow_cnt, tr->d_rng,
                tr->d_qx, tr->d_qy, tr->d_qz, tr->d_vx, tr->d_vy, tr->d_vz,
                tr->d_yaw, tr->d_gnd, tr->d_dt, tr->d_prog, tr->d_ept, tr->d_ndelta, tr->d_prev_dist);
            run_forward_chunk(tr, tr->d_obs_buf + (s_off + off) * BR_OBS_DIM);
            k_tr_sample_act<<<(Q + 127) / 128, 128, 0, tr->stA>>>(
                tr->d_logits, tr->d_vals, tr->d_rng,
                tr->d_act_buf + s_off + off, tr->d_logp_buf + s_off + off,
                tr->d_val_buf + s_off + off, off, Q);
        }
    }
    cudaStreamSynchronize(tr->stA);
    cudaStreamSynchronize(tr->stB);
    double t1 = tr_now_ns();

    k_tr_gae<<<(N + 127) / 128, 128, 0, tr->stA>>>(
        tr->d_rew_buf, tr->d_val_buf, tr->d_term_buf, tr->d_trunc_buf,
        tr->d_adv_buf, tr->d_ret_buf, tr->d_stat_sum, N, T, tr->cfg.gamma, tr->cfg.gae_lam);
    int total_s = N * T;
    k_tr_adv_stats<<<(total_s + 255) / 256, 256, 0, tr->stA>>>(tr->d_adv_buf, tr->d_adv_stats, total_s);
    cudaStreamSynchronize(tr->stA);
    double t2 = tr_now_ns();

    int n_mb = total_s / Q;
    float alpha = 1.0f, alpha_w = 1.0f / (tr->cfg.grad_loss_scale * (float)Q), beta = 0.0f;
    int relu_grd = ((Q * H + 7) / 8 + 255) / 256;
    for (int ep = 0; ep < tr->cfg.ppo_epochs; ep++) {
        for (int mb = 0; mb < n_mb; mb++) {
            size_t off = (size_t)mb * Q;
            const __half *ob = tr->d_obs_buf + off * BR_OBS_DIM;
            run_forward_chunk(tr, ob);
            k_tr_ppo_loss_grad<<<(Q + 255) / 256, 256, 0, tr->stA>>>(
                tr->d_logits, tr->d_vals, tr->d_act_buf + off, tr->d_logp_buf + off,
                tr->d_adv_buf + off, tr->d_ret_buf + off, tr->d_adv_stats, total_s,
                tr->d_dlogits, tr->d_dvals, tr->d_loss_acc, Q,
                tr->cfg.clip_eps, tr->cfg.ent_coef, tr->cfg.grad_loss_scale);
            /* Output heads always backward in FP16/FP32 */
            cublasLtMatmul(tr->lt, tr->p_bp_d.desc, &alpha,   tr->d_Wp_16, tr->p_bp_d.lA, tr->d_dlogits, tr->p_bp_d.lB, &beta, tr->d_dh2, tr->p_bp_d.lD, tr->d_dh2, tr->p_bp_d.lD, &tr->p_bp_d.algo, tr->d_ws, tr->ws_bytes, tr->stA);
            cublasLtMatmul(tr->lt, tr->p_bp_w.desc, &alpha_w, tr->d_h2,    tr->p_bp_w.lA, tr->d_dlogits, tr->p_bp_w.lB, &beta, tr->d_gWp, tr->p_bp_w.lD, tr->d_gWp, tr->p_bp_w.lD, &tr->p_bp_w.algo, tr->d_ws, tr->ws_bytes, tr->stA);
            cublasLtMatmul(tr->lt, tr->p_bv_w.desc, &alpha_w, tr->d_h2,    tr->p_bv_w.lA, tr->d_dvals,   tr->p_bv_w.lB, &beta, tr->d_gWv, tr->p_bv_w.lD, tr->d_gWv, tr->p_bv_w.lD, &tr->p_bv_w.algo, tr->d_ws, tr->ws_bytes, tr->stA);
            k_tr_relu_bwd<<<relu_grd, 256, 0, tr->stA>>>(tr->d_dh2, tr->d_h2, Q * H);

            /* Trunk backward: FP16 | FP8 E4M3 (Delayed Scaling) | NVFP4 Stochastic-Round */
            run_backward_trunk(tr, ob, (uint32_t)(iter * 997 + ep * 31 + mb * 7));

            tr->adam_step++;
            float bc1 = 1.0f / (1.0f - powf(0.9f, (float)tr->adam_step));
            float bc2 = 1.0f / (1.0f - powf(0.999f, (float)tr->adam_step));
            int req = tr->cfg.use_nvfp4;
            k_tr_adamw_requant_swz<<<(H * BR_OBS_DIM + 255) / 256, 256, 0, tr->stA>>>(tr->d_W1_32, tr->d_W1_m1, tr->d_W1_m2, tr->d_gW1, tr->d_W1_16, tr->d_W1_q, tr->d_W1_sf, H, BR_OBS_DIM, tr->cfg.lr, bc1, bc2, req);
            k_tr_adamw_requant_swz<<<(H * H + 255) / 256,          256, 0, tr->stA>>>(tr->d_W2_32, tr->d_W2_m1, tr->d_W2_m2, tr->d_gW2, tr->d_W2_16, tr->d_W2_q, tr->d_W2_sf, H, H, tr->cfg.lr, bc1, bc2, req);
            k_tr_adamw_requant_swz<<<(BR_NUM_ACTS * H + 255) / 256,256, 0, tr->stA>>>(tr->d_Wp_32, tr->d_Wp_m1, tr->d_Wp_m2, tr->d_gWp, tr->d_Wp_16, NULL, NULL, BR_NUM_ACTS, H, tr->cfg.lr, bc1, bc2, 0);
            k_tr_adamw_requant_swz<<<(H + 255) / 256,              256, 0, tr->stA>>>(tr->d_Wv_32, tr->d_Wv_m1, tr->d_Wv_m2, tr->d_gWv, tr->d_Wv_16, NULL, NULL, 1, H, tr->cfg.lr, bc1, bc2, 0);
        }
    }
    cudaStreamSynchronize(tr->stA);
    double t3 = tr_now_ns();

    uint32_t h_wins = 0; float h_rsum = 0.0f, h_loss[3] = {0};
    cudaMemcpy(&h_wins, tr->d_win_acc, 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(&h_rsum, tr->d_stat_sum, 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(h_loss,  tr->d_loss_acc, 12, cudaMemcpyDeviceToHost);

    double r_ms = (t1 - t0) / 1e6, g_ms = (t2 - t1) / 1e6, l_ms = (t3 - t2) / 1e6;
    double tot_ms = r_ms + g_ms + l_ms;
    double denom = (double)tr->cfg.ppo_epochs * (double)total_s;
    out->iter          = iter;
    out->mean_step_rew = (double)h_rsum / (double)total_s;
    out->total_wins    = (double)h_wins;
    out->win_rate_pct  = 100.0 * (double)h_wins / (double)N;
    out->policy_loss   = (double)h_loss[0] / denom;
    out->value_loss    = (double)h_loss[1] / denom;
    out->entropy       = (double)h_loss[2] / denom;
    out->rollout_ms    = r_ms;
    out->gae_ms        = g_ms;
    out->learner_ms    = l_ms;
    out->total_ms      = tot_ms;
    out->msteps_s      = ((double)total_s * (double)K) / (tot_ms * 1e3);
    out->mpol_s        = (double)total_s / (tot_ms * 1e3);
}

int br_trainer_dump_trace(br_trainer_t *tr, const char *path, int max_steps) {
    int N = tr->cfg.n_worlds, Q = tr->cfg.chunk_q, K = tr->cfg.ticks_per_step;
    int cand = (Q < 256) ? Q : 256;
    if (max_steps <= 0 || max_steps > 120) max_steps = 80;

    /* Reset all worlds cleanly so we trace from tick 0 of fresh episodes */
    k_tr_init_worlds<<<(N + 127) / 128, 128, 0, tr->stA>>>(
        tr->cfg.env_id, tr->d_rng, (tr->cfg.seed ? tr->cfg.seed : 0x12345ULL) ^ 0xCAFEBABEULL,
        tr->d_qx, tr->d_qy, tr->d_qz, tr->d_vx, tr->d_vy, tr->d_vz,
        tr->d_yaw, tr->d_gnd, tr->d_dt, tr->d_prog, tr->d_ept, tr->d_ndelta, tr->d_prev_dist, N);
    cudaMemsetAsync(tr->d_act_buf, 0, (size_t)N, tr->stA);
    cudaStreamSynchronize(tr->stA);

    typedef struct {
        float px, py, pz, yaw, rew, val;
        uint8_t dt, prog, ndelta, act, term;
        uint32_t deltas[8];
        float obs[94];
        float probs[BR_NUM_ACTS];
    } trace_rec_t;

    trace_rec_t *recs = (trace_rec_t *)calloc((size_t)max_steps * cand, sizeof(trace_rec_t));
    int32_t *hqx = (int32_t *)malloc(cand * 4), *hqy = (int32_t *)malloc(cand * 4), *hqz = (int32_t *)malloc(cand * 4);
    float *hyaw = (float *)malloc(cand * 4), *hrew = (float *)malloc(cand * 4), *hval = (float *)malloc(cand * 4);
    uint8_t *hdt = (uint8_t *)malloc(cand), *hprog = (uint8_t *)malloc(cand), *hnd = (uint8_t *)malloc(cand);
    uint8_t *hact = (uint8_t *)malloc(cand), *hterm = (uint8_t *)malloc(cand);
    uint32_t *hdel = (uint32_t *)malloc(8 * cand * 4);
    __half *hobs = (__half *)malloc((size_t)cand * BR_OBS_DIM * 2);
    __half *hlog = (__half *)malloc((size_t)cand * BR_NUM_ACTS * 2);

    int winner_w = -1, winner_s = max_steps - 1;
    int best_prog_w = 0, best_prog = -1;

    for (int s = 0; s < max_steps; s++) {
        cudaMemsetAsync(tr->d_slow_cnt, 0, 4, tr->stA);
        k_tr_step_fast_split<<<(Q + 63) / 64, 64, 0, tr->stA>>>(
            tr->cfg.env_id, tr->d_seed_blocks, tr->d_seed_bits, tr->num_seeds,
            tr->d_qx, tr->d_qy, tr->d_qz, tr->d_vx, tr->d_vy, tr->d_vz,
            tr->d_yaw, tr->d_gnd, tr->d_dt, tr->d_prog, tr->d_ept, tr->d_deltas, tr->d_ndelta,
            tr->d_prev_dist, tr->d_act_buf,
            tr->d_obs_buf, tr->d_rew_buf, tr->d_term_buf, tr->d_trunc_buf,
            tr->d_slow_q, tr->d_slow_cnt, tr->d_win_acc, 0, Q, N, K,
            tr->cfg.episode_max_ticks, tr->cfg.target_count, tr->cfg.break_ticks);
        run_forward_chunk(tr, tr->d_obs_buf);
        k_tr_sample_act<<<(Q + 127) / 128, 128, 0, tr->stA>>>(
            tr->d_logits, tr->d_vals, tr->d_rng,
            tr->d_act_buf, tr->d_logp_buf, tr->d_val_buf, 0, Q);
        cudaStreamSynchronize(tr->stA);

        cudaMemcpy(hqx, tr->d_qx, cand * 4, cudaMemcpyDeviceToHost);
        cudaMemcpy(hqy, tr->d_qy, cand * 4, cudaMemcpyDeviceToHost);
        cudaMemcpy(hqz, tr->d_qz, cand * 4, cudaMemcpyDeviceToHost);
        cudaMemcpy(hyaw, tr->d_yaw, cand * 4, cudaMemcpyDeviceToHost);
        cudaMemcpy(hdt, tr->d_dt, cand, cudaMemcpyDeviceToHost);
        cudaMemcpy(hprog, tr->d_prog, cand, cudaMemcpyDeviceToHost);
        cudaMemcpy(hnd, tr->d_ndelta, cand, cudaMemcpyDeviceToHost);
        cudaMemcpy(hrew, tr->d_rew_buf, cand * 4, cudaMemcpyDeviceToHost);
        cudaMemcpy(hterm, tr->d_term_buf, cand, cudaMemcpyDeviceToHost);
        cudaMemcpy(hact, tr->d_act_buf, cand, cudaMemcpyDeviceToHost);
        cudaMemcpy(hval, tr->d_val_buf, cand * 4, cudaMemcpyDeviceToHost);
        cudaMemcpy(hobs, tr->d_obs_buf, (size_t)cand * BR_OBS_DIM * 2, cudaMemcpyDeviceToHost);
        cudaMemcpy(hlog, tr->d_logits,  (size_t)cand * BR_NUM_ACTS * 2, cudaMemcpyDeviceToHost);
        for (int d = 0; d < 8; d++)
            cudaMemcpy(hdel + d * cand, tr->d_deltas + (size_t)d * N, cand * 4, cudaMemcpyDeviceToHost);

        for (int w = 0; w < cand; w++) {
            trace_rec_t *r = &recs[(size_t)s * cand + w];
            r->px = (float)hqx[w] / 65536.0f;
            r->py = (float)hqy[w] / 65536.0f;
            r->pz = (float)hqz[w] / 65536.0f;
            r->yaw = hyaw[w]; r->rew = hrew[w]; r->val = hval[w];
            r->dt = hdt[w]; r->prog = hprog[w]; r->ndelta = hnd[w];
            r->act = hact[w]; r->term = hterm[w];
            for (int d = 0; d < 8; d++) r->deltas[d] = hdel[d * cand + w];
            for (int k = 0; k < 94; k++) r->obs[k] = __half2float(hobs[(size_t)w * BR_OBS_DIM + k]);
            float mx = -1e9f, sm = 0.0f, lg[BR_NUM_ACTS];
            for (int k = 0; k < BR_NUM_ACTS; k++) { lg[k] = __half2float(hlog[(size_t)w * BR_NUM_ACTS + k]); if (lg[k] > mx) mx = lg[k]; }
            for (int k = 0; k < BR_NUM_ACTS; k++) { r->probs[k] = expf(lg[k] - mx); sm += r->probs[k]; }
            for (int k = 0; k < BR_NUM_ACTS; k++) r->probs[k] /= (sm + 1e-8f);
            if (r->prog > best_prog) { best_prog = r->prog; best_prog_w = w; }
            if (r->term && winner_w < 0 && s >= 12) { winner_w = w; winner_s = s; }
        }
        /* Drain resets for other worlds (so we keep clean episode continuity) */
        k_tr_slow_reset_drain<<<(Q / 8 + 127) / 128, 128, 0, tr->stA>>>(
            tr->cfg.env_id, tr->d_slow_q, tr->d_slow_cnt, tr->d_rng,
            tr->d_qx, tr->d_qy, tr->d_qz, tr->d_vx, tr->d_vy, tr->d_vz,
            tr->d_yaw, tr->d_gnd, tr->d_dt, tr->d_prog, tr->d_ept, tr->d_ndelta, tr->d_prev_dist);
        cudaStreamSynchronize(tr->stA);
        if (winner_w >= 0 && s >= winner_s) break;
    }

    int chosen_w = (winner_w >= 0) ? winner_w : best_prog_w;
    int n_out = (winner_w >= 0) ? (winner_s + 1) : max_steps;
    int sidx = chosen_w % tr->num_seeds;

    /* Copy chosen world's CoW seed terrain map (top surface block at each x,z) */
    uint8_t *hseed = (uint8_t *)malloc(BR_BLOCKS);
    cudaMemcpy(hseed, tr->d_seed_blocks + (size_t)sidx * BR_BLOCKS, BR_BLOCKS, cudaMemcpyDeviceToHost);

    FILE *fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "META\tenv=%s\tH=%d\tfwd=%s\tbwd=%s\tworld=%d\tseed=%d\tsteps=%d\twon=%d\n",
                br_env_name(tr->cfg.env_id), tr->cfg.hidden,
                tr->cfg.use_nvfp4 ? "nvfp4" : "fp16", br_bwd_name(tr->cfg.bwd_prec),
                chosen_w, sidx, n_out, winner_w >= 0 ? 1 : 0);
        for (int z = 0; z < BR_WZ; z++) {
            fprintf(fp, "MAP\t%d", z);
            for (int x = 0; x < BR_WX; x++) {
                int top_b = 0;
                for (int y = 12; y >= 7; y--) {
                    uint8_t b = hseed[((y & 31) << 12) | ((z & 63) << 6) | (x & 63)];
                    if (b != 0) { top_b = b; break; }
                }
                fprintf(fp, "\t%d", top_b);
            }
            fprintf(fp, "\n");
        }
        for (int s = 0; s < n_out; s++) {
            trace_rec_t *r = &recs[(size_t)s * cand + chosen_w];
            fprintf(fp, "STEP\t%d\t%.3f\t%.3f\t%.3f\t%.2f\t%u\t%u\t%u\t%u\t%.4f\t%.4f\t%u",
                    s, r->px, r->py, r->pz, r->yaw, r->prog, r->dt, r->ndelta, r->act, r->rew, r->val, r->term);
            for (int d = 0; d < 8; d++) fprintf(fp, "\t%08x", (d < r->ndelta) ? r->deltas[d] : 0u);
            for (int k = 0; k < 94; k++) fprintf(fp, "\t%.4f", r->obs[k]);
            for (int k = 0; k < BR_NUM_ACTS; k++) fprintf(fp, "\t%.4f", r->probs[k]);
            fprintf(fp, "\n");
        }
        fclose(fp);
    }

    free(hseed); free(recs); free(hqx); free(hqy); free(hqz); free(hyaw);
    free(hrew); free(hval); free(hdt); free(hprog); free(hnd); free(hact);
    free(hterm); free(hdel); free(hobs); free(hlog);
    return (winner_w >= 0) ? 1 : 0;
}

void br_trainer_destroy(br_trainer_t *tr) {
    if (!tr) return;
    destroy_lt_plan(&tr->p_f1_nv4); destroy_lt_plan(&tr->p_f2_nv4);
    destroy_lt_plan(&tr->p_f1_f16); destroy_lt_plan(&tr->p_f2_f16);
    destroy_lt_plan(&tr->p_fp_f16); destroy_lt_plan(&tr->p_fv_f16);
    destroy_lt_plan(&tr->p_bp_d); destroy_lt_plan(&tr->p_bp_w); destroy_lt_plan(&tr->p_bv_w);
    destroy_lt_plan(&tr->p_b2_d_f16); destroy_lt_plan(&tr->p_b2_w_f16); destroy_lt_plan(&tr->p_b1_w_f16);
    destroy_lt_plan(&tr->p_b2_d_f8);  destroy_lt_plan(&tr->p_b2_w_f8);  destroy_lt_plan(&tr->p_b1_w_f8);
    destroy_lt_plan(&tr->p_b2_d_nv4); destroy_lt_plan(&tr->p_b2_w_nv4); destroy_lt_plan(&tr->p_b1_w_nv4);
    cudaFree(tr->d_ws); cudaFree(tr->d_seed_blocks); cudaFree(tr->d_seed_bits);
    cudaFree(tr->d_rng); cudaFree(tr->d_qx); cudaFree(tr->d_qy); cudaFree(tr->d_qz);
    cudaFree(tr->d_vx); cudaFree(tr->d_vy); cudaFree(tr->d_vz);
    cudaFree(tr->d_yaw); cudaFree(tr->d_prev_dist); cudaFree(tr->d_gnd); cudaFree(tr->d_dt);
    cudaFree(tr->d_prog); cudaFree(tr->d_ndelta); cudaFree(tr->d_ept); cudaFree(tr->d_deltas);
    cudaFree(tr->d_slow_q); cudaFree(tr->d_slow_cnt);
    cudaFree(tr->d_obs_buf); cudaFree(tr->d_act_buf); cudaFree(tr->d_term_buf); cudaFree(tr->d_trunc_buf);
    cudaFree(tr->d_rew_buf); cudaFree(tr->d_logp_buf); cudaFree(tr->d_val_buf);
    cudaFree(tr->d_adv_buf); cudaFree(tr->d_ret_buf);
    cudaFree(tr->d_obs_q); cudaFree(tr->d_obs_sf); cudaFree(tr->d_h1_q); cudaFree(tr->d_h1_sf);
    cudaFree(tr->d_h1); cudaFree(tr->d_h2); cudaFree(tr->d_logits); cudaFree(tr->d_vals);
    cudaFree(tr->d_dlogits); cudaFree(tr->d_dvals); cudaFree(tr->d_dh2); cudaFree(tr->d_dh1);
    cudaFree(tr->d_W2T_f8); cudaFree(tr->d_dh2_f8); cudaFree(tr->d_dh2T_f8); cudaFree(tr->d_h1T_f8); cudaFree(tr->d_dh1T_f8); cudaFree(tr->d_obT_f8);
    cudaFree(tr->d_fp8_sc); cudaFree(tr->d_fp8_nxt);
    cudaFree(tr->d_W2T_q); cudaFree(tr->d_W2T_sf); cudaFree(tr->d_dh2_q); cudaFree(tr->d_dh2_sf);
    cudaFree(tr->d_dh2T_q); cudaFree(tr->d_dh2T_sf); cudaFree(tr->d_h1T_q); cudaFree(tr->d_h1T_sf);
    cudaFree(tr->d_dh1T_q); cudaFree(tr->d_dh1T_sf); cudaFree(tr->d_obT_q); cudaFree(tr->d_obT_sf);
    cudaFree(tr->d_W1_32); cudaFree(tr->d_W1_m1); cudaFree(tr->d_W1_m2); cudaFree(tr->d_W1_16); cudaFree(tr->d_gW1); cudaFree(tr->d_W1_q); cudaFree(tr->d_W1_sf);
    cudaFree(tr->d_W2_32); cudaFree(tr->d_W2_m1); cudaFree(tr->d_W2_m2); cudaFree(tr->d_W2_16); cudaFree(tr->d_gW2); cudaFree(tr->d_W2_q); cudaFree(tr->d_W2_sf);
    cudaFree(tr->d_Wp_32); cudaFree(tr->d_Wp_m1); cudaFree(tr->d_Wp_m2); cudaFree(tr->d_Wp_16); cudaFree(tr->d_gWp);
    cudaFree(tr->d_Wv_32); cudaFree(tr->d_Wv_m1); cudaFree(tr->d_Wv_m2); cudaFree(tr->d_Wv_16); cudaFree(tr->d_gWv);
    cudaFree(tr->d_win_acc); cudaFree(tr->d_stat_sum); cudaFree(tr->d_adv_stats); cudaFree(tr->d_loss_acc);
    cudaEventDestroy(tr->evFast); cublasLtDestroy(tr->lt);
    cudaStreamDestroy(tr->stA); cudaStreamDestroy(tr->stB);
    free(tr);
}
