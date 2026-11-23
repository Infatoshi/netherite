/* The native trainer's 16-bit type and tensor-core product (lane/nth16), one choice a build (NETH_NET_MMA, net.h):
 *
 *   NETH_NET_MMA 0  bf16 inputs, fp32 accumulators (mma ...f32.bf16.bf16.f32; the default, lane/ntlearn's and ntactor's)
 *   NETH_NET_MMA 1  fp16 inputs, fp32 accumulators (...f32.f16.f16.f32)
 *   NETH_NET_MMA 2  fp16 inputs, fp16 accumulators over the whole K (...f16.f16.f16.f16: the GA102's 142 TFLOPS path, twice
 *             the GeForce rate of fp32 accumulation)
 *   NETH_NET_MMA 3  fp16 accumulators added into fp32 sums every NETH_NET_PROMOTE k-steps of 16 (and at the end)
 *   NETH_NET_MMA 4  fp16 accumulation in the forward's and the input gradients' products, fp32 in the weights' gradients
 *             (mma16816w: K the minibatch's rows, sums that cancel, where an fp16 tile's rounding costs most)
 *
 * The 16-bit activations, their gradients and the weights' copies are nt16 (bf16 at 0, fp16 otherwise). An
 * accumulator tile is acc4: the four fp32 values of one m16n8 fragment (PTX's layout: rows g and g + 8, columns 2t and
 * 2t + 1), and with fp16 accumulation two f16x2 registers in the same order; acc_fin adds the fp16 part into the fp32
 * one and clears it (a kernel calls it before reading a tile, and acc_step every k-step for NETH_NET_MMA 3). With fp16 the
 * backward runs on the loss scaled by NETH_NET_LSCALE (net.h; k_loss's 16-bit copy of the heads' gradient; neth_learn_grad divides
 * the gradients back in fp32): the heads' gradients are about 1e-4 at mb 8192, and the input gradients shrink layer by
 * layer below fp16's smallest normal 6.1e-5. */
#ifndef NETH_NET_PREC_CUH
#define NETH_NET_PREC_CUH

#include <cuda_bf16.h>
#include <cuda_fp16.h>
#include <stdint.h>

#include "net.h"

#if NETH_SC_BF16
__host__ __device__ __forceinline__ float neth_sc_f32(neth_sc_t x) { return __bfloat162float(__ushort_as_bfloat16(x)); }
__host__ __device__ __forceinline__ neth_sc_t neth_f32_sc(float x) { return __bfloat16_as_ushort(__float2bfloat16_rn(x)); }
#else
__host__ __device__ __forceinline__ float neth_sc_f32(neth_sc_t x) { return x; }
__host__ __device__ __forceinline__ neth_sc_t neth_f32_sc(float x) { return x; }
#endif

#if NETH_NET_MMA == 0
typedef __nv_bfloat16 nt16;
typedef __nv_bfloat162 nt162;
#define NT16_CUDA CUDA_R_16BF
#define NT16_NAME "bf16"
__host__ __device__ __forceinline__ float h2f(nt16 x) { return __bfloat162float(x); }
__host__ __device__ __forceinline__ nt16 f2h(float x) { return __float2bfloat16_rn(x); }
__device__ __forceinline__ float2 h22f2(nt162 x) { return __bfloat1622float2(x); }
__device__ __forceinline__ nt162 f22h2(float lo, float hi) { return __floats2bfloat162_rn(lo, hi); }
#else
typedef __half nt16;
typedef __half2 nt162;
#define NT16_CUDA CUDA_R_16F
#define NT16_NAME "fp16"
__host__ __device__ __forceinline__ float h2f(nt16 x) { return __half2float(x); }
__host__ __device__ __forceinline__ nt16 f2h(float x) { return __float2half_rn(x); }
__device__ __forceinline__ float2 h22f2(nt162 x) { return __half22float2(x); }
__device__ __forceinline__ nt162 f22h2(float lo, float hi) { return __floats2half2_rn(lo, hi); }
#endif

#define NETH_NET_ACC16 (NETH_NET_MMA >= 2)

struct acc4 {
#if NETH_NET_ACC16
    uint32_t h[2];
#endif
    float f[4];
    __device__ __forceinline__ float &operator[](int i) { return f[i]; }
    __device__ __forceinline__ float operator[](int i) const { return f[i]; }
};

/* d += a b, one m16n8k16 tile (a: four registers of 16-bit pairs, b: two) */
__device__ __forceinline__ void mma16816(acc4 &d, const uint32_t *a, const uint32_t *b) {
#if NETH_NET_MMA == 0
    asm volatile("mma.sync.aligned.m16n8k16.row.col.f32.bf16.bf16.f32 {%0,%1,%2,%3}, {%4,%5,%6,%7}, {%8,%9}, "
                 "{%0,%1,%2,%3};\n"
                 : "+f"(d.f[0]), "+f"(d.f[1]), "+f"(d.f[2]), "+f"(d.f[3])
                 : "r"(a[0]), "r"(a[1]), "r"(a[2]), "r"(a[3]), "r"(b[0]), "r"(b[1]));
#elif NETH_NET_MMA == 1
    asm volatile("mma.sync.aligned.m16n8k16.row.col.f32.f16.f16.f32 {%0,%1,%2,%3}, {%4,%5,%6,%7}, {%8,%9}, "
                 "{%0,%1,%2,%3};\n"
                 : "+f"(d.f[0]), "+f"(d.f[1]), "+f"(d.f[2]), "+f"(d.f[3])
                 : "r"(a[0]), "r"(a[1]), "r"(a[2]), "r"(a[3]), "r"(b[0]), "r"(b[1]));
#else
    asm volatile("mma.sync.aligned.m16n8k16.row.col.f16.f16.f16.f16 {%0,%1}, {%2,%3,%4,%5}, {%6,%7}, {%0,%1};\n"
                 : "+r"(d.h[0]), "+r"(d.h[1])
                 : "r"(a[0]), "r"(a[1]), "r"(a[2]), "r"(a[3]), "r"(b[0]), "r"(b[1]));
#endif
}

/* a weight gradient's product (the learner's dw kernels, k_cwgrad): mma16816, but fp32 accumulation at NETH_NET_MMA 4 */
__device__ __forceinline__ void mma16816w(acc4 &d, const uint32_t *a, const uint32_t *b) {
#if NETH_NET_MMA == 4
    asm volatile("mma.sync.aligned.m16n8k16.row.col.f32.f16.f16.f32 {%0,%1,%2,%3}, {%4,%5,%6,%7}, {%8,%9}, "
                 "{%0,%1,%2,%3};\n"
                 : "+f"(d.f[0]), "+f"(d.f[1]), "+f"(d.f[2]), "+f"(d.f[3])
                 : "r"(a[0]), "r"(a[1]), "r"(a[2]), "r"(a[3]), "r"(b[0]), "r"(b[1]));
#else
    mma16816(d, a, b);
#endif
}

/* the fp16 accumulators into the fp32 sums */
__device__ __forceinline__ void acc_fin(acc4 &d) {
#if NETH_NET_ACC16
    float2 lo = __half22float2(*(const __half2 *)&d.h[0]), hi = __half22float2(*(const __half2 *)&d.h[1]);
    d.f[0] += lo.x, d.f[1] += lo.y, d.f[2] += hi.x, d.f[3] += hi.y;
    d.h[0] = d.h[1] = 0u;
#endif
}
template <int N>
__device__ __forceinline__ void acc_fin(acc4 (&c)[N]) {
#pragma unroll
    for (int i = 0; i < N; i++) acc_fin(c[i]);
}
template <int M, int N>
__device__ __forceinline__ void acc_fin(acc4 (&c)[M][N]) {
#pragma unroll
    for (int i = 0; i < M; i++) acc_fin(c[i]);
}

#ifndef NETH_NET_PROMOTE
#define NETH_NET_PROMOTE 8
#endif
/* after k-step k (from 0) of the tiles c: their fp32 sums take the fp16 accumulators every NETH_NET_PROMOTE steps */
template <class C>
__device__ __forceinline__ void acc_step(C &c, int k) {
#if NETH_NET_MMA == 3
    if ((k + 1) % NETH_NET_PROMOTE == 0) acc_fin(c);
#endif
}

#endif
