/* The native trainer's learner (learn.h; lane/ntlearn): fastppo.py's Learner as CUDA kernels and cuBLAS GEMMs.
 *
 * Every layer is a row-major GEMM over channels-last activations: a convolution is its im2col ([rows = sample x
 * output position][kernel cells x in]) times the weight transposed; its backward is the weight's gradient (dY^T A),
 * the input's (dY W, then col2im gathering each input cell's terms, no atomics) and the bias's (dY's column sums, a
 * GEMM against ones). conv2's and conv3's columns are ordered (ky, kx, ci), so a row's run of channels is one cell's
 * and the im2col and col2im move 16 bytes a thread; their weights are copied into that order (and their gradients
 * back) each step, conv1's (3 channels) keep PyTorch's (ci, ky, kx). The activations and their gradients are T: fp32,
 * or bf16 (NETH_LEARN_BF16, every GEMM on the tensor cores accumulating in fp32, the weights' gradients written in fp32).
 * The loss is one thread a sample over its 97 head outputs (fp32), writing their gradient in place; the books are
 * per-block partial sums summed in a fixed order, so a run is deterministic. Adam is torch's fused kernel's
 * arithmetic, the clip's norm reduced in the same kernel. */
#include <cublas_v2.h>
#include <cuda_runtime.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <type_traits>

#include "learn.h"
#include "prec.cuh"

#define CK(x)                                                                                        \
    do {                                                                                             \
        cudaError_t e_ = (x);                                                                        \
        if (e_ != cudaSuccess) {                                                                     \
            fprintf(stderr, "learn.cu:%d %s: %s\n", __LINE__, #x, cudaGetErrorString(e_));           \
            abort();                                                                                 \
        }                                                                                            \
    } while (0)
#define CB(x)                                                                                        \
    do {                                                                                             \
        cublasStatus_t e_ = (x);                                                                     \
        if (e_ != CUBLAS_STATUS_SUCCESS) {                                                           \
            fprintf(stderr, "learn.cu:%d %s: cublas %d\n", __LINE__, #x, (int)e_);                   \
            abort();                                                                                 \
        }                                                                                            \
    } while (0)

typedef nt16 bf;    /* the 16-bit path's type: bf16, or fp16 at NETH_NET_MMA 1 to 4 (prec.cuh); the name kept */

/* positions a sample (8x8 after conv1, 4x4 after conv2, 2x2 after conv3 at 16x16) and each layer's k */
#define OS (NETH_NET_SIDE / 2)    /* the first layer's output side: conv1's, or the stem's (NETH_NET_STEM) */
#define P0 (OS * OS)        /* the first layer's positions */
#define P1 (NETH_NET_O1 * NETH_NET_O1)
#define P2 (NETH_NET_O2 * NETH_NET_O2)
#define P3 (NETH_NET_O3 * NETH_NET_O3)
#define K0 (NETH_NET_C * 16)      /* 48: the first layer's, from the bytes, in PyTorch's (ci, ky, kx) order */
#define K1 (NETH_NET_CI * 16)     /* conv1's: 48 from the bytes, 256 after the stem ((ky, kx, ci) order) */
#define K2 (NETH_NET_C1 * 16)     /* 512 at wm 1 */
#define K3 (NETH_NET_C2 * 9)      /* 576 at wm 1 */
#define Z0LD ((NETH_NET_FLAT + 7) / 8 * 8)    /* z0's row padded to 16 bytes of bf16 (the tensor cores' alignment): 416
                                               * (lane/audit-feat: the run's, net.h NETH_NET_NS) */
#define HS NETH_LEARN_HSTRIDE      /* the heads' row: 97 padded likewise */
#define NPART 1024          /* the reductions' partial sums at most */

/* NETH_LEARN_SPEC: the bf16 tensor-core convolutions below (lane/ntlearn's: their tiles and shared-memory paddings C2X_LD,
 * C2F_LD, C3F_LD and the warps a cell are written for 16x16x3 and 32, 64, 64 channels) serve the wm 1 16x16 build;
 * every other variant runs its convolutions as im2col and cuBLAS GEMMs in either precision (lane/ntwidth) */
#ifndef NETH_LEARN_SPEC
#define NETH_LEARN_SPEC (NETH_NET_WM == 1 && !NETH_NET_STEM)
#endif
/* conv2 as implicit GEMMs in bf16 (lane/ntwidth), at the variants where they beat im2col, cuBLAS and col2im: NETH_LEARN_IC2X
 * its input gradient by parity, up to C1 128 (wm 8: 135.0 against 128.1 ms an update); NETH_LEARN_IC2 its forward and weight
 * gradient over h1, relu(x1 + b1) padded, up to C2 128 (wm 4, C2 256: 41.3 against 38.5) */
#ifndef NETH_LEARN_IC2X
#define NETH_LEARN_IC2X (!NETH_LEARN_SPEC && NETH_NET_C1 <= 128)
#endif
#ifndef NETH_LEARN_IC2
#define NETH_LEARN_IC2 (!NETH_LEARN_SPEC && NETH_NET_C2 <= 128)
#endif
/* lane/ntbytes's fusions at the stem nets in bf16, off by default (variant builds V.b, V.f; learn.mk): NETH_LEARN_S1B conv1's
 * whole backward and the stem's weight gradient in one kernel (k_s1b), h0 recomputed from the bytes, dx0 never
 * stored; NETH_LEARN_S1F the stem's forward inside conv1's (k_s1f), h0 never stored */
#ifndef NETH_LEARN_S1B
#define NETH_LEARN_S1B 0
#endif
#ifndef NETH_LEARN_S1F
#define NETH_LEARN_S1F 0
#endif
#if (NETH_LEARN_S1B || NETH_LEARN_S1F) && !(NETH_NET_STEM && NETH_NET_C0 == 16 && NETH_NET_C1 == 32)
#error "NETH_LEARN_S1B and NETH_LEARN_S1F are written for the stem's 16 channels and conv1's 32 (s1)"
#endif
#if NETH_LEARN_S1F && !NETH_LEARN_S1B
#error "NETH_LEARN_S1F stores no h0, which only NETH_LEARN_S1B's backward does without (build V.b.f)"
#endif
/* in bf16 the stem nets keep h0 and h1 only, not x0 and x1, where every relu' reads h > 0 (x + b > 0) instead */
#define X1_KEPT (!(NETH_NET_STEM && NETH_LEARN_IC2 && NETH_LEARN_IC2X))
/* NETH_LEARN_FC (lane/ntfc): the 16-bit path's minibatch runs fc1, fc2, the heads, the loss and their input gradients down to
 * dx3 as one kernel (fc.cuh's k_fcrow), the weights' gradients on cuBLAS as before; 0 the per-layer path */
#ifndef NETH_LEARN_FC
#define NETH_LEARN_FC 0
#endif
#if NETH_LEARN_FC && (NETH_NET_D > 512 || NETH_NET_HOTBAR)   /* past D 512 (wm 4, 8) k_fcrow's rows do not fit shared memory, and its
                                            * loss has no hotbar (lane/nthotbar): the per-layer path */
#undef NETH_LEARN_FC
#define NETH_LEARN_FC 0
#endif
#if NETH_LEARN_FC && NETH_NET_RNN       /* lane/ntgru's minGRU sits between fc2 and the heads, which k_fcrow does not know: the
                            * per-layer path */
#undef NETH_LEARN_FC
#define NETH_LEARN_FC 0
#endif
#if NETH_LEARN_SPEC
static_assert(NETH_NET_H == 16 && NETH_NET_W == 16 && NETH_NET_C == 3 && NETH_NET_C1 == 32 && NETH_NET_C2 == 64 && NETH_NET_C3 == 64,
              "learn.cu's bf16 convolution kernels assume net.h's 16x16x3, 32, 64, 64");
#endif
static_assert(NETH_LEARN_HEADS <= HS, "the heads' padded rows");

__device__ __forceinline__ float ldf(float x) { return x; }
__device__ __forceinline__ float ldf(bf x) { return h2f(x); }
template <class T> __device__ __forceinline__ T cvt(float x);
template <> __device__ __forceinline__ float cvt<float>(float x) { return x; }
template <> __device__ __forceinline__ bf cvt<bf>(float x) { return f2h(x); }

/* 16 bytes of T: VN elements, loaded to and stored from floats */
template <class T> struct vec { static constexpr int n = 16 / sizeof(T); };
__device__ __forceinline__ void vld(const float *p, float *f) {
    float4 u = *(const float4 *)p;
    f[0] = u.x, f[1] = u.y, f[2] = u.z, f[3] = u.w;
}
__device__ __forceinline__ void vst(float *p, const float *f) { *(float4 *)p = make_float4(f[0], f[1], f[2], f[3]); }
__device__ __forceinline__ void vld(const bf *p, float *f) {
    uint4 u = *(const uint4 *)p;
    const nt162 *h = (const nt162 *)&u;
    for (int i = 0; i < 4; i++) {
        float2 g = h22f2(h[i]);
        f[2 * i] = g.x, f[2 * i + 1] = g.y;
    }
}
__device__ __forceinline__ void vst(bf *p, const float *f) {
    uint4 u;
    nt162 *h = (nt162 *)&u;
    for (int i = 0; i < 4; i++) h[i] = f22h2(f[2 * i], f[2 * i + 1]);
    *(uint4 *)p = u;
}

struct neth_learn {
    cudaStream_t s;
    cublasHandle_t cb;
    int nmax, mb, prec;
    int nsm;                    /* the device's SMs (k_s1b's and k_s1f's waves) */
    cudaDataType_t ab;          /* the activations' and the GEMMs' input type */
    cublasComputeType_t ct;
    size_t es;                  /* its size */
    void *a0, *x0, *dx0;        /* the stem's (NETH_NET_STEM): its im2col from the bytes, its output, its gradient */
    void *h0;                   /* (NETH_NET_STEM, bf16) relu(x0 + b0) padded by one: conv1's implicit GEMMs' input */
    void *h1;                   /* (bf16, not NETH_LEARN_SPEC) relu(x1 + b1) padded by one: conv2's implicit GEMMs' input */
    float *cwpart;              /* k_cwgrad's slice partials */
    void *a1, *x1, *a2, *x2, *a3, *x3, *z0, *z1, *z2, *d1, *d2, *dx1, *dx2, *dx3, *dh, *ones;
    void *wt;                   /* the GEMMs' weights, T: conv2's and conv3's in (ky, kx, ci) order, fc1's rows Z0LD */
    float *hd, *gw, *part, *nm, *books, *gn, *adv, *ret, *mask;     /* gw: conv2's and conv3's gradients reordered */
    float *cpart, *c2part, *c3part;     /* conv1's, conv2's and conv3's weight gradients' block partials (bf16) */
    void *fcpk;                 /* (NETH_LEARN_FC) fc.cuh's packed weights */
    float *fcpart;              /* (NETH_LEARN_FC) its biases' block partials */
    void *hpart;                /* (NETH_NET_ACC16) an fp32-output GEMM's fp16 K-slices before their fp32 sum, hparts halves */
    long hparts;
    const float *wref;          /* lane/ntppo: the reference weights (neth_learn_set_ref; NULL none) */
    void *rhd;                  /* their logits over the segment [nmax][NETH_LEARN_RSTRIDE] (nt16 in 16-bit mode, else fp32) */
    void *wt_ref;               /* cached converted reference weights [WT_N] (converted once in neth_learn_set_ref) */
    struct neth_learn_rnn *rnn;        /* (NETH_NET_RNN) the minGRU's buffers and its segment (rnn.cuh) */
    struct neth_critic *C;              /* lane/acrl: the privileged critic (neth_learn_set_critic; NULL none) */
    const neth_sc_t *psc;       /* its segment's full scalars [N][NETH_NET_NS] */
    const float *val0;          /* hyper vclip: the segment's actor values (neth_learn_update's val) */
    int *kstop;                 /* hyper kl_stop: [0] this update stopped, [1] steps taken, [2] updates stopped */
    float *tstat;               /* telemetry [6]: gnorm_sum, gnorm_max, gnorm_cnt, nan_skips, ev_sum, ev_cnt */
};

neth_learn_hyper neth_learn_hyper_default(void) {
    neth_learn_hyper h = {0.998, 0.95, 0.2f, 0.5f, 0.003f, 3e-4f, 0.9f, 0.999f, 1e-5f, 0.5f};
    return h;
}

/* the layout in its order: net.h's index, the offset, the elements (lane/audit-feat: the run's, made at each call) */
struct lay_t {
    int ntw, off, n;
};
#define NETH_LEARN_LAY                                                              \
    const lay_t lay[NETH_NET_TENSORS] = {                                           \
        NETH_LEARN_LAYOUT(NETH_LEARN_S)                                             \
    }
#define NETH_LEARN_S(n, i, k) {(i), NETH_LEARN_OFF(n), (k)},

void neth_learn_from_ntw(const float *const ntw[NETH_NET_TENSORS], float *flat) {
    NETH_LEARN_LAY;
    for (int t = 0; t < NETH_NET_TENSORS; t++)
        for (int k = 0; k < lay[t].n; k++) flat[lay[t].off + k] = ntw[lay[t].ntw][k];
}

void neth_learn_to_ntw(const float *flat, float *const ntw[NETH_NET_TENSORS]) {
    NETH_LEARN_LAY;
    for (int t = 0; t < NETH_NET_TENSORS; t++)
        for (int k = 0; k < lay[t].n; k++) ntw[lay[t].ntw][k] = flat[lay[t].off + k];
}
#undef NETH_LEARN_LAY
#undef NETH_LEARN_S

/* ---- GEMMs, row-major: C[M][N] = op(A) op(B), A and B the learner's type, C of type ct ---- */

#if NETH_NET_ACC16
/* NETH_NET_MMA 2 to 4 (prec.cuh), the 16-bit path's GEMMs accumulating in fp16 (CUBLAS_COMPUTE_16F): a 16-bit output
 * directly; an fp32 output (the weights' gradients, the heads' logits; not at NETH_NET_MMA 4, which sums those in fp32) as
 * gemm_slices K-slices of fp16 accumulation into hpart, then their fp32 sum in order (NETH_NET_MMA 2: one slice, the whole
 * K; NETH_NET_MMA 3: slices of NETH_NET_PROMOTE k-steps of 16, at most GS_MAX of them, longer when K is) */
#define GS_MAX 64
static int gemm_slices(int K, int *ks) {
#if NETH_NET_MMA != 3
    *ks = K;
    return 1;
#else
    int ns = (K + NETH_NET_PROMOTE * 16 - 1) / (NETH_NET_PROMOTE * 16);
    if (ns > GS_MAX) ns = GS_MAX;
    *ks = ((K + ns - 1) / ns + 15) / 16 * 16;
    return (K + *ks - 1) / *ks;
#endif
}
__global__ void k_hsum(long n, int N, int ns, const __half *part, float *c, int ldc) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    float s = 0.0f;
    for (int k = 0; k < ns; k++) s += __half2float(part[(long)k * n + i]);
    c[i / N * ldc + i % N] = s;
}
static void gemm16(neth_learn *L, int ta, int tb, int M, int N, int K, const void *A, int lda, const void *B, int ldb, void *C,
                   cudaDataType_t ct, int ldc) {
    const __half one = __float2half(1.0f), zero = __float2half(0.0f);
    cublasOperation_t oa = ta ? CUBLAS_OP_T : CUBLAS_OP_N, ob = tb ? CUBLAS_OP_T : CUBLAS_OP_N;
    if (ct != CUDA_R_32F) {
        CB(cublasGemmEx(L->cb, ob, oa, N, M, K, &one, B, CUDA_R_16F, ldb, A, CUDA_R_16F, lda, &zero, C, CUDA_R_16F,
                        ldc, CUBLAS_COMPUTE_16F, CUBLAS_GEMM_DEFAULT));
        return;
    }
    int ks, ns = gemm_slices(K, &ks), full = K / ks, rem = K - full * ks;
    long mn = (long)M * N, sa = ta ? (long)ks * lda : ks, sb = tb ? ks : (long)ks * ldb;
    if ((long)ns * mn > L->hparts) {
        fprintf(stderr, "learn.cu: gemm16 %dx%dx%d needs %ld halves of slices, %ld allocated\n", M, N, K, ns * mn,
                L->hparts);
        abort();
    }
    __half *P = (__half *)L->hpart;
    if (full)
        CB(cublasGemmStridedBatchedEx(L->cb, ob, oa, N, M, ks, &one, B, CUDA_R_16F, ldb, sb, A, CUDA_R_16F, lda, sa,
                                      &zero, P, CUDA_R_16F, N, mn, full, CUBLAS_COMPUTE_16F, CUBLAS_GEMM_DEFAULT));
    if (rem)
        CB(cublasGemmEx(L->cb, ob, oa, N, M, rem, &one, (const __half *)B + full * sb, CUDA_R_16F, ldb,
                        (const __half *)A + full * sa, CUDA_R_16F, lda, &zero, P + full * mn, CUDA_R_16F, N,
                        CUBLAS_COMPUTE_16F, CUBLAS_GEMM_DEFAULT));
    k_hsum<<<(unsigned)((mn + 255) / 256), 256, 0, L->s>>>(mn, N, ns, P, (float *)C, ldc);
}
/* the halves gemm16's slices need at most, for every fp32-output GEMM of the 16-bit path at minibatch b */
static long gemm16_parts(long b) {
    struct {
        long m, n, k;
    } g[] = {{NETH_LEARN_HEADS, NETH_NET_D, b}, {NETH_NET_D, NETH_NET_D, b}, {NETH_NET_D, Z0LD, b}, {b, HS, NETH_NET_D}, {NETH_NET_C3, K3, b * P3},
             {NETH_NET_C2, K2, b * P2}, {NETH_NET_C1, K1, b * P1}, {NETH_NET_C0, K0, b * P0}};
    long mx = 0;
    for (size_t i = 0; i < sizeof g / sizeof *g; i++) {
        int ks;
        long n = gemm_slices((int)g[i].k, &ks) * g[i].m * g[i].n;
        mx = n > mx ? n : mx;
    }
    return mx;
}
#endif

static void gemm(neth_learn *L, int ta, int tb, int M, int N, int K, const void *A, int lda, const void *B, int ldb, void *C,
                 cudaDataType_t ct, int ldc, int acc32 = 0) {
#if NETH_NET_ACC16
    /* NETH_NET_MMA 4: an fp32 output (the weights' gradients, the heads' logits) accumulates in fp32 below */
    if (L->prec == NETH_LEARN_BF16 && !acc32 && !(NETH_NET_MMA == 4 && ct == CUDA_R_32F)) {
        gemm16(L, ta, tb, M, N, K, A, lda, B, ldb, C, ct, ldc);
        return;
    }
#endif
    const float one = 1.0f, zero = 0.0f;
    /* row-major C = op(A) op(B) is column-major C^T = op(B)^T op(A)^T: B first, the flags as they are */
    CB(cublasGemmEx(L->cb, tb ? CUBLAS_OP_T : CUBLAS_OP_N, ta ? CUBLAS_OP_T : CUBLAS_OP_N, N, M, K, &one, B, L->ab,
                    ldb, A, L->ab, lda, &zero, C, ct, ldc, L->ct, CUBLAS_GEMM_DEFAULT));
}

/* db[C] (fp32) = the column sums of dY [R][C] (row stride ld): fp32 sums at every NETH_NET_MMA (a reduction bound by
 * reading dY, not by the tensor cores) */
static void colsum(neth_learn *L, int R, int C, const void *dY, int ld, float *db) {
    gemm(L, 0, 0, 1, C, R, L->ones, R, dY, ld, db, CUDA_R_32F, C, 1);
}

/* ---- the forward's kernels ---- */

/* the first layer's im2col (conv1's, or the stem's), a thread VN columns: a[n * P0 + oy * OS + ox][ci * 16 + ky * 4 +
 * kx] = obs[idx[n]][2oy - 1 + ky][2ox - 1 + kx][ci] / 255 */
template <class T>
__global__ void k_im2col1(int b, const int32_t *idx, const uint8_t *obs, T *a1) {
    constexpr int VN = vec<T>::n;
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= (long)b * P0 * K0 / VN) return;
    int k0 = (t % (K0 / VN)) * VN;
    long r = t / (K0 / VN);
    int n = r / P0, p = r % P0;
    const uint8_t *o = obs + (long)(idx ? idx[n] : n) * NETH_NET_H * NETH_NET_W * NETH_NET_C;
    float f[VN];
#pragma unroll
    for (int j = 0; j < VN; j++) {
        int k = k0 + j, ci = k / 16, ky = (k / 4) % 4, kx = k % 4;
        int iy = 2 * (p / OS) - 1 + ky, ix = 2 * (p % OS) - 1 + kx;
        f[j] = iy >= 0 && iy < NETH_NET_H && ix >= 0 && ix < NETH_NET_W ? (float)o[(iy * NETH_NET_W + ix) * NETH_NET_C + ci] * (float)(1.0 / 255)
                                                           : 0.0f;
    }
    vst(a1 + r * K0 + k0, f);
}

/* ---- conv1 on the tensor cores straight from the bytes (NETH_LEARN_BF16): no im2col ---- */

__device__ __forceinline__ uint32_t pack2(float lo, float hi) {
    nt162 h = f22h2(lo, hi);
    return *(uint32_t *)&h;
}

/* mma16816 (prec.cuh): d += a b, one m16n8k16 tile into an acc4 (PTX's fragment layouts: g = lane / 4, t = lane % 4);
 * each kernel's tiles take acc_fin before they are read, and acc_step after each k-step (NETH_NET_MMA 3's promotion) */

#if NETH_LEARN_SPEC
/* conv1's input at position p (8x8), channel ci, kernel cell (ky, kx): the byte / 255, 0 in the padding */
__device__ __forceinline__ float px1(const uint8_t *o, int p, int ci, int ky, int kx) {
    int iy = 2 * (p >> 3) - 1 + ky, ix = 2 * (p & 7) - 1 + kx;
    return iy >= 0 && iy < NETH_NET_H && ix >= 0 && ix < NETH_NET_W ? (float)o[(iy * NETH_NET_W + ix) * NETH_NET_C + ci] * (float)(1.0 / 255)
                                                       : 0.0f;
}

/* y1 = a1 W1^T (no bias: the next im2col adds it), a warp 16 rows (a quarter sample's positions) at a time: K 48 is
 * three k-steps of 16 whose step is the channel (k = ci * 16 + ky * 4 + kx), the weights' 12 fragments held in
 * registers for every tile the warp takes */
#define C1F_BLK 512
#define C1F_WARPS 8
#define OBSB (NETH_NET_H * NETH_NET_W * NETH_NET_C)
__global__ void __launch_bounds__(C1F_WARPS * 32) k_conv1_mma(int b, const int32_t *idx, const uint8_t *obs,
                                                             const bf *w1, bf *y1) {
    int lane = threadIdx.x & 31, g = lane >> 2, t = lane & 3;
    uint32_t B[NETH_NET_C][4][2];
#pragma unroll
    for (int kk = 0; kk < NETH_NET_C; kk++)
#pragma unroll
        for (int nt = 0; nt < 4; nt++) {
            const bf *wr = w1 + (nt * 8 + g) * K1 + kk * 16 + 2 * t;
            B[kk][nt][0] = *(const uint32_t *)wr;
            B[kk][nt][1] = *(const uint32_t *)(wr + 8);
        }
    int ky = t >> 1, kx = (t & 1) * 2, wib = threadIdx.x >> 5, nwb = blockDim.x >> 5;
    /* a block's warps take as many samples, their bytes staged in shared memory 16 bytes a thread (the byte gathers
     * from global memory took 76 us a step), a warp its sample's four tiles */
    __shared__ __align__(16) uint8_t so[C1F_WARPS][OBSB];
    for (long n0 = (long)blockIdx.x * nwb; n0 < b; n0 += (long)gridDim.x * nwb) {
        __syncthreads();
        for (int i = threadIdx.x; i < nwb * OBSB / 16; i += blockDim.x) {
            int s = i / (OBSB / 16), k = (i % (OBSB / 16)) * 16;
            long n = n0 + s;
            *(uint4 *)&so[s][k] =
                n < b ? *(const uint4 *)(obs + (idx ? (long)idx[n] : n) * OBSB + k) : make_uint4(0, 0, 0, 0);
        }
        __syncthreads();
        long n = n0 + wib;
        if (n >= b) continue;
        const uint8_t *o = so[wib];
#pragma unroll 1
        for (int tq = 0; tq < 4; tq++) {
            int pa = tq * 16 + g, pb = pa + 8;
            acc4 c[4] = {};
#pragma unroll
            for (int kk = 0; kk < NETH_NET_C; kk++) {
                uint32_t A[4] = {pack2(px1(o, pa, kk, ky, kx), px1(o, pa, kk, ky, kx + 1)),
                                 pack2(px1(o, pb, kk, ky, kx), px1(o, pb, kk, ky, kx + 1)),
                                 pack2(px1(o, pa, kk, ky + 2, kx), px1(o, pa, kk, ky + 2, kx + 1)),
                                 pack2(px1(o, pb, kk, ky + 2, kx), px1(o, pb, kk, ky + 2, kx + 1))};
#pragma unroll
                for (int nt = 0; nt < 4; nt++) mma16816(c[nt], A, B[kk][nt]);
            }
            acc_fin(c);
            bf *ya = y1 + (n * P1 + pa) * NETH_NET_C1, *yb = y1 + (n * P1 + pb) * NETH_NET_C1;
#pragma unroll
            for (int nt = 0; nt < 4; nt++) {
                *(uint32_t *)(ya + nt * 8 + 2 * t) = pack2(c[nt][0], c[nt][1]);
                *(uint32_t *)(yb + nt * 8 + 2 * t) = pack2(c[nt][2], c[nt][3]);
            }
        }
    }
}

/* conv1's weight and bias gradients: D[co][kc] = sum over rows r of dx1[r][co] a1[r][kc], kc 0..47 a1's columns
 * (rebuilt from the bytes), kc 48 a column of ones (the bias), 49..55 zero; M = co (two m-tiles), N = kc (seven
 * n-tiles), K = the rows, 16 a step. C1W_BLK blocks of 4 warps take 4 samples at a time through shared memory (dx1's
 * rows, padded to 40, and the bytes, loaded 16 bytes a thread; from global memory 122 us a step), a warp a sample's
 * four tiles; the warps' partials summed in warp order into the block's part[blk][32][49]; k_conv1_dwsum sums the
 * blocks in order */
#define C1W_BLK 256
#define CW (K1 + 1)
#define C1W_DL 40
__global__ void __launch_bounds__(128) k_conv1_dw_mma(int b, const int32_t *idx, const uint8_t *obs, const bf *dx1,
                                                      float *part) {
    /* the staging (dx1 [4][64][40] u16, the bytes [4][768]) and, after the loop, the warps' partials share it */
    __shared__ __align__(16) float sm[4 * NETH_NET_C1 * 56];
    unsigned short(*sd)[P1][C1W_DL] = (unsigned short(*)[P1][C1W_DL])sm;
    uint8_t(*so)[OBSB] = (uint8_t(*)[OBSB])(sm + 4 * P1 * C1W_DL / 2);
    int lane = threadIdx.x & 31, g = lane >> 2, t = lane & 3, wib = threadIdx.x >> 5;
    acc4 c[2][7] = {};
    int ks = 0;     /* k-steps taken (acc_step) */
    for (long n0 = (long)blockIdx.x * 4; n0 < b; n0 += (long)gridDim.x * 4) {
        __syncthreads();
        for (int i = threadIdx.x; i < 4 * P1 * NETH_NET_C1 / 8; i += blockDim.x) {
            int s = i / (P1 * NETH_NET_C1 / 8), r = (i / (NETH_NET_C1 / 8)) % P1, c8 = (i % (NETH_NET_C1 / 8)) * 8;
            *(uint4 *)&sd[s][r][c8] =
                n0 + s < b ? *(const uint4 *)(dx1 + ((n0 + s) * P1 + r) * NETH_NET_C1 + c8) : make_uint4(0, 0, 0, 0);
        }
        for (int i = threadIdx.x; i < 4 * OBSB / 16; i += blockDim.x) {
            int s = i / (OBSB / 16), k = (i % (OBSB / 16)) * 16;
            long n = n0 + s;
            *(uint4 *)&so[s][k] =
                n < b ? *(const uint4 *)(obs + (idx ? (long)idx[n] : n) * OBSB + k) : make_uint4(0, 0, 0, 0);
        }
        __syncthreads();
        const uint8_t *o = so[wib];
#pragma unroll 1
        for (int tq = 0; tq < 4; tq++) {
            int p0 = tq * 16, r = p0 + 2 * t;
            uint32_t A[2][4], B[7][2];
#pragma unroll
            for (int mt = 0; mt < 2; mt++) {
                int co = mt * 16 + g;
                A[mt][0] = sd[wib][r][co] | (uint32_t)sd[wib][r + 1][co] << 16;
                A[mt][1] = sd[wib][r][co + 8] | (uint32_t)sd[wib][r + 1][co + 8] << 16;
                A[mt][2] = sd[wib][r + 8][co] | (uint32_t)sd[wib][r + 9][co] << 16;
                A[mt][3] = sd[wib][r + 8][co + 8] | (uint32_t)sd[wib][r + 9][co + 8] << 16;
            }
#pragma unroll
            for (int nt = 0; nt < 6; nt++) {
                int kc = nt * 8 + g, ci = kc >> 4, ky = (kc >> 2) & 3, kx = kc & 3;
                B[nt][0] = pack2(px1(o, r, ci, ky, kx), px1(o, r + 1, ci, ky, kx));
                B[nt][1] = pack2(px1(o, r + 8, ci, ky, kx), px1(o, r + 9, ci, ky, kx));
            }
            B[6][0] = B[6][1] = g == 0 ? pack2(1.0f, 1.0f) : 0u;
#pragma unroll
            for (int mt = 0; mt < 2; mt++)
#pragma unroll
                for (int nt = 0; nt < 7; nt++) mma16816w(c[mt][nt], A[mt], B[nt]);
            acc_step(c, ks++);
        }
    }
    acc_fin(c);
    __syncthreads();
    float(*red)[NETH_NET_C1 * 56] = (float(*)[NETH_NET_C1 * 56])sm;
#pragma unroll
    for (int mt = 0; mt < 2; mt++)
#pragma unroll
        for (int nt = 0; nt < 7; nt++) {
            float *r = red[wib] + (mt * 16 + g) * 56 + nt * 8 + 2 * t;
            r[0] = c[mt][nt][0], r[1] = c[mt][nt][1], r[8 * 56] = c[mt][nt][2], r[8 * 56 + 1] = c[mt][nt][3];
        }
    __syncthreads();
    for (int i = threadIdx.x; i < NETH_NET_C1 * CW; i += blockDim.x) {
        int co = i / CW, kc = i % CW, j = co * 56 + kc;
        part[(long)blockIdx.x * NETH_NET_C1 * CW + i] = (red[0][j] + red[1][j]) + (red[2][j] + red[3][j]);
    }
}

/* the blocks' partials in block order into conv1's weight gradient [32][48] and bias gradient [32] */
__global__ void k_conv1_dwsum(int nblk, const float *part, float *gw, float *gb) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= NETH_NET_C1 * CW) return;
    float s = 0.0f;
    for (int k = 0; k < nblk; k++) s += part[(long)k * NETH_NET_C1 * CW + i];
    int co = i / CW, kc = i % CW;
    if (kc == K1)
        gb[co] = s;
    else
        gw[co * K1 + kc] = s;
}

/* conv2's input gradient on the tensor cores without da2 and col2im (NETH_LEARN_BF16): an input pixel (iy, ix) of parity
 * (py, px) is read by the kernel cells ky = 1 - py + 2 sy, kx = 1 - px + 2 sx (sy, sx in 0, 1) at the output (oy, ox)
 * = (iy / 2 + py - sy, ix / 2 + px - sx), so a sample's 16 pixels of one parity are one m16 tile of a GEMM with K =
 * (sy, sx, co), 256: A = dx2's rows (pairs of channels, one 32-bit load), B = conv2's weights as [cell][ci][co] in
 * shared memory, N = ci (four n-tiles). A warp a (sample, parity) tile; the output times relu' (y1 + bias > 0) */
#define C2X_BLK 256
#define C2X_LD 36   /* a (cell, ci) row of 32 co pairs padded to 36 words: lanes g * 36 + t on 32 banks (32: 4) */
#define C2X_SMEM (16 * NETH_NET_C1 * C2X_LD * 4)
__global__ void __launch_bounds__(512) k_conv2_dx_mma(int b, const bf *dx2, const bf *w2t, const bf *y1,
                                                      const float *b1, bf *dx1) {
    extern __shared__ uint32_t sw[];    /* [cell 16][ci 32][co pairs 32, padded to C2X_LD] */
    const uint32_t *gw = (const uint32_t *)w2t;
    for (int i = threadIdx.x; i < NETH_LEARN_N_conv2_w / 2; i += blockDim.x) sw[(i >> 5) * C2X_LD + (i & 31)] = gw[i];
    __syncthreads();
    int lane = threadIdx.x & 31, g = lane >> 2, t = lane & 3;
    long warp = (long)blockIdx.x * (blockDim.x >> 5) + (threadIdx.x >> 5), nw = (long)gridDim.x * (blockDim.x >> 5);
    const uint32_t *d2 = (const uint32_t *)dx2;     /* [r][co pairs 32] */
    int ia = g >> 2, ib = ia + 2, j = g & 3;
    /* a tile two samples of one parity: the B fragments (the parity's cells) loaded once for both */
    for (long tile = warp; tile < (long)(b + 1) / 2 * 4; tile += nw) {
        long n0 = (tile >> 2) * 2;
        int py = (int)(tile >> 1) & 1, px = (int)tile & 1;
        acc4 c[2][4] = {};
#pragma unroll
        for (int sy = 0; sy < 2; sy++) {
            int ky = 1 - py + 2 * sy, oya = ia + py - sy, oyb = ib + py - sy;
#pragma unroll
            for (int sx = 0; sx < 2; sx++) {
                int kx = 1 - px + 2 * sx, ox = j + px - sx;
                bool vx = ox >= 0 && ox < 4, va = vx && oya >= 0 && oya < 4, vb = vx && oyb >= 0 && oyb < 4;
                const uint32_t *wc = sw + (ky * 4 + kx) * NETH_NET_C1 * C2X_LD;
#pragma unroll
                for (int cs = 0; cs < 4; cs++) {
                    int q = cs * 8 + t;
                    uint32_t A[2][4];
#pragma unroll
                    for (int s = 0; s < 2; s++) {
                        long n = n0 + s;
                        bool sa = va && n < b, sb = vb && n < b;
                        const uint32_t *ra = d2 + (n * P2 + oya * 4 + ox) * 32, *rb = d2 + (n * P2 + oyb * 4 + ox) * 32;
                        A[s][0] = sa ? ra[q] : 0u, A[s][1] = sb ? rb[q] : 0u;
                        A[s][2] = sa ? ra[q + 4] : 0u, A[s][3] = sb ? rb[q + 4] : 0u;
                    }
#pragma unroll
                    for (int nt = 0; nt < 4; nt++) {
                        const uint32_t *wr = wc + (nt * 8 + g) * C2X_LD;
                        uint32_t B[2] = {wr[q], wr[q + 4]};
                        mma16816(c[0][nt], A[0], B);
                        mma16816(c[1][nt], A[1], B);
                    }
                    acc_step(c, (sy * 2 + sx) * 4 + cs);
                }
            }
        }
        acc_fin(c);
#pragma unroll
        for (int s = 0; s < 2; s++) {
            long n = n0 + s;
            if (n >= b) break;
            long pa = n * P1 + (2 * ia + py) * 8 + 2 * j + px, pb = n * P1 + (2 * ib + py) * 8 + 2 * j + px;
#pragma unroll
            for (int nt = 0; nt < 4; nt++) {
                int ci = nt * 8 + 2 * t;
                float2 ya = h22f2(*(const nt162 *)(y1 + pa * NETH_NET_C1 + ci));
                float2 yb = h22f2(*(const nt162 *)(y1 + pb * NETH_NET_C1 + ci));
                float b0 = b1[ci], b1v = b1[ci + 1];
                *(uint32_t *)(dx1 + pa * NETH_NET_C1 + ci) =
                    pack2(ya.x + b0 > 0.0f ? c[s][nt][0] : 0.0f, ya.y + b1v > 0.0f ? c[s][nt][1] : 0.0f);
                *(uint32_t *)(dx1 + pb * NETH_NET_C1 + ci) =
                    pack2(yb.x + b0 > 0.0f ? c[s][nt][2] : 0.0f, yb.y + b1v > 0.0f ? c[s][nt][3] : 0.0f);
            }
        }
    }
}

/* relu(y + bias) on a bf16 pair */
__device__ __forceinline__ uint32_t act2(uint32_t u, float b0, float b1) {
    float2 f = h22f2(*(const nt162 *)&u);
    return pack2(fmaxf(f.x + b0, 0.0f), fmaxf(f.y + b1, 0.0f));
}

/* conv2's forward without im2col (NETH_LEARN_BF16): y2 = a2 W2^T with a2's rows built in registers, a warp one sample (its
 * 16 output positions an m16 tile), K = (cell, ci), 32 k-steps of 16 channels of one cell: A conv1's output pairs
 * (relu(y1 + bias) applied as they load), B conv2's weights [co][(cell, ci)] in shared memory, a row of 256 words
 * padded to 260 (lanes g * 260 + t on 32 banks), N = co (eight n-tiles) */
#define C2F_LD 260
#define C2F_SMEM (NETH_NET_C2 * C2F_LD * 4)
#define C2F_BLK 164
__global__ void __launch_bounds__(512) k_conv2_mma(int b, const bf *y1, const float *b1, const bf *w2r, bf *y2) {
    extern __shared__ uint32_t sw[];
    const uint32_t *gw = (const uint32_t *)w2r;
    for (int i = threadIdx.x; i < NETH_NET_C2 * K2 / 2; i += blockDim.x) sw[(i >> 8) * C2F_LD + (i & 255)] = gw[i];
    __syncthreads();
    int lane = threadIdx.x & 31, g = lane >> 2, t = lane & 3;
    long warp = (long)blockIdx.x * (blockDim.x >> 5) + (threadIdx.x >> 5), nw = (long)gridDim.x * (blockDim.x >> 5);
    float bb[2][4];
#pragma unroll
    for (int h = 0; h < 2; h++) {
        bb[h][0] = b1[h * 16 + 2 * t], bb[h][1] = b1[h * 16 + 2 * t + 1];
        bb[h][2] = b1[h * 16 + 2 * t + 8], bb[h][3] = b1[h * 16 + 2 * t + 9];
    }
    int oya = g >> 2, ox = g & 3, oyb = oya + 2;
    const uint32_t *y = (const uint32_t *)y1;   /* [pixel][16 channel pairs] */
    for (long n = warp; n < b; n += nw) {
        acc4 c[8] = {};
        const uint32_t *ys = y + n * P1 * 16;
#pragma unroll 4
        for (int cell = 0; cell < 16; cell++) {
            int ky = cell >> 2, kx = cell & 3;
            int iya = 2 * oya - 1 + ky, iyb = 2 * oyb - 1 + ky, ix = 2 * ox - 1 + kx;
            bool vx = ix >= 0 && ix < 8, va = vx && iya >= 0 && iya < 8, vb = vx && iyb >= 0 && iyb < 8;
            const uint32_t *ra = ys + (iya * 8 + ix) * 16, *rb = ys + (iyb * 8 + ix) * 16;
#pragma unroll
            for (int h = 0; h < 2; h++) {
                int q = h * 8 + t, kq = cell * 16 + h * 8 + t;
                uint32_t A[4] = {va ? act2(ra[q], bb[h][0], bb[h][1]) : 0u, vb ? act2(rb[q], bb[h][0], bb[h][1]) : 0u,
                                 va ? act2(ra[q + 4], bb[h][2], bb[h][3]) : 0u,
                                 vb ? act2(rb[q + 4], bb[h][2], bb[h][3]) : 0u};
#pragma unroll
                for (int nt = 0; nt < 8; nt++) {
                    const uint32_t *wr = sw + (nt * 8 + g) * C2F_LD;
                    uint32_t B[2] = {wr[kq], wr[kq + 4]};
                    mma16816(c[nt], A, B);
                }
                acc_step(c, cell * 2 + h);
            }
        }
        acc_fin(c);
        bf *ya = y2 + (n * P2 + g) * NETH_NET_C2, *yb = y2 + (n * P2 + g + 8) * NETH_NET_C2;
#pragma unroll
        for (int nt = 0; nt < 8; nt++) {
            *(uint32_t *)(ya + nt * 8 + 2 * t) = pack2(c[nt][0], c[nt][1]);
            *(uint32_t *)(yb + nt * 8 + 2 * t) = pack2(c[nt][2], c[nt][3]);
        }
    }
}

/* conv2's weight gradient without im2col (NETH_LEARN_BF16): D[co][(cell, ci)] = sum over rows of dx2[r][co] a2[r][(cell,
 * ci)], a k-step a sample's 16 positions; a block's 8 warps take 64 of the 512 columns each (two cells, eight
 * n-tiles) over all 64 co (four m-tiles) for the block's share of the samples, the block's [64][512] to part, summed
 * in block order by k_dwsum. The samples come C2W_S at a time through shared memory (dx2's rows and conv1's
 * relu(y1 + bias), loaded 16 bytes a thread; rows padded against bank conflicts, a zero pixel for the padding): the
 * fragments' 16-bit gathers read it there (from global memory they took 668 us a step) */
#define C2W_BLK 82
#define C2W_S 4
#define C2W_DL 72   /* dx2's row, bf16 */
#define C2W_XL 40   /* a pixel's 32 channels, bf16 */
#define C2W_MT 2                    /* m-tiles (16 co) a warp: 4 / C2W_MT warps share a column group */
#define C2W_T (8 * (4 / C2W_MT) * 32)
__global__ void __launch_bounds__(C2W_T) k_conv2_dw_mma(int b, const bf *dx2, const bf *y1, const float *b1,
                                                        float *part) {
    __shared__ __align__(16) unsigned short sd[C2W_S][P2][C2W_DL], sx[C2W_S][P1 + 1][C2W_XL];
    int lane = threadIdx.x & 31, g = lane >> 2, t = lane & 3, w = (threadIdx.x >> 5) & 7, m0 = (threadIdx.x >> 8) * C2W_MT;
    acc4 c[C2W_MT][8] = {};
    int ks = 0;
    int pidx[2][4];     /* positions 2t, 2t + 1, 2t + 8, 2t + 9's pixel under the warp's two cells (P1: padding) */
#pragma unroll
    for (int h = 0; h < 2; h++) {
        int cell = 2 * w + h, ky = cell >> 2, kx = cell & 3;
#pragma unroll
        for (int j = 0; j < 4; j++) {
            int p = 2 * t + (j & 1) + (j >> 1) * 8, iy = 2 * (p >> 2) - 1 + ky, ix = 2 * (p & 3) - 1 + kx;
            pidx[h][j] = iy >= 0 && iy < 8 && ix >= 0 && ix < 8 ? iy * 8 + ix : P1;
        }
    }
    for (int i = threadIdx.x; i < C2W_S * C2W_XL; i += blockDim.x) sx[i / C2W_XL][P1][i % C2W_XL] = 0;
    for (long n0 = (long)blockIdx.x * C2W_S; n0 < b; n0 += (long)gridDim.x * C2W_S) {
        __syncthreads();
        for (int i = threadIdx.x; i < C2W_S * P2 * NETH_NET_C2 / 8; i += blockDim.x) {     /* dx2: 8 channels a thread */
            int s = i / (P2 * NETH_NET_C2 / 8), r = (i / (NETH_NET_C2 / 8)) % P2, c8 = (i % (NETH_NET_C2 / 8)) * 8;
            uint4 u = n0 + s < b ? *(const uint4 *)(dx2 + ((n0 + s) * P2 + r) * NETH_NET_C2 + c8) : make_uint4(0, 0, 0, 0);
            *(uint4 *)&sd[s][r][c8] = u;
        }
        for (int i = threadIdx.x; i < C2W_S * P1 * NETH_NET_C1 / 8; i += blockDim.x) {     /* relu(y1 + bias) */
            int s = i / (P1 * NETH_NET_C1 / 8), p = (i / (NETH_NET_C1 / 8)) % P1, c8 = (i % (NETH_NET_C1 / 8)) * 8;
            float f[8];
            if (n0 + s < b) {
                vld(y1 + ((n0 + s) * P1 + p) * NETH_NET_C1 + c8, f);
                float4 b0 = __ldg((const float4 *)(b1 + c8)), b4 = __ldg((const float4 *)(b1 + c8 + 4));
                f[0] = fmaxf(f[0] + b0.x, 0.0f), f[1] = fmaxf(f[1] + b0.y, 0.0f), f[2] = fmaxf(f[2] + b0.z, 0.0f);
                f[3] = fmaxf(f[3] + b0.w, 0.0f), f[4] = fmaxf(f[4] + b4.x, 0.0f), f[5] = fmaxf(f[5] + b4.y, 0.0f);
                f[6] = fmaxf(f[6] + b4.z, 0.0f), f[7] = fmaxf(f[7] + b4.w, 0.0f);
            } else
                for (int j = 0; j < 8; j++) f[j] = 0.0f;
            vst((bf *)&sx[s][p][c8], f);
        }
        __syncthreads();
#pragma unroll 1
        for (int s = 0; s < C2W_S; s++) {
            uint32_t A[C2W_MT][4], B[8][2];
            int p0 = 2 * t;
#pragma unroll
            for (int mt = 0; mt < C2W_MT; mt++) {
                int co = (m0 + mt) * 16 + g;
                A[mt][0] = sd[s][p0][co] | (uint32_t)sd[s][p0 + 1][co] << 16;
                A[mt][1] = sd[s][p0][co + 8] | (uint32_t)sd[s][p0 + 1][co + 8] << 16;
                A[mt][2] = sd[s][p0 + 8][co] | (uint32_t)sd[s][p0 + 9][co] << 16;
                A[mt][3] = sd[s][p0 + 8][co + 8] | (uint32_t)sd[s][p0 + 9][co + 8] << 16;
            }
#pragma unroll
            for (int nt = 0; nt < 8; nt++) {
                int h = nt >> 2, ci = (nt & 3) * 8 + g;
                B[nt][0] = sx[s][pidx[h][0]][ci] | (uint32_t)sx[s][pidx[h][1]][ci] << 16;
                B[nt][1] = sx[s][pidx[h][2]][ci] | (uint32_t)sx[s][pidx[h][3]][ci] << 16;
            }
#pragma unroll
            for (int mt = 0; mt < C2W_MT; mt++)
#pragma unroll
                for (int nt = 0; nt < 8; nt++) mma16816w(c[mt][nt], A[mt], B[nt]);
            acc_step(c, ks++);
        }
    }
    acc_fin(c);
    float *o = part + (long)blockIdx.x * NETH_NET_C2 * K2;
#pragma unroll
    for (int mt = 0; mt < C2W_MT; mt++)
#pragma unroll
        for (int nt = 0; nt < 8; nt++) {
            int co = (m0 + mt) * 16 + g, kc = w * 64 + nt * 8 + 2 * t;
            o[co * K2 + kc] = c[mt][nt][0], o[co * K2 + kc + 1] = c[mt][nt][1];
            o[(co + 8) * K2 + kc] = c[mt][nt][2], o[(co + 8) * K2 + kc + 1] = c[mt][nt][3];
        }
}

/* the blocks' partials (nblk of n) in block order into gw's n */
__global__ void k_dwsum(int n, int nblk, const float *part, float *gw) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    float s = 0.0f;
    for (int k = 0; k < nblk; k++) s += part[(long)k * n + i];
    gw[i] = s;
}

/* conv3's forward without im2col (NETH_LEARN_BF16): a tile 4 samples' 4 output positions, K = (cell, ci), 36 k-steps of 16
 * channels of one of the 9 cells: A conv2's output pairs with relu(y2 + bias) as they load, B conv3's weights
 * [co][(cell, ci)] in shared memory, a row of 288 words padded to 292, N = co (eight n-tiles) */
#define C3F_LD 292
#define C3F_SMEM (NETH_NET_C3 * C3F_LD * 4)
#define C3F_BLK 164
__global__ void __launch_bounds__(512) k_conv3_mma(int b, const bf *y2, const float *b2, const bf *w3r, bf *y3) {
    extern __shared__ uint32_t sw[];
    const uint32_t *gw = (const uint32_t *)w3r;
    for (int i = threadIdx.x; i < NETH_NET_C3 * K3 / 2; i += blockDim.x) sw[(i / (K3 / 2)) * C3F_LD + i % (K3 / 2)] = gw[i];
    __syncthreads();
    int lane = threadIdx.x & 31, g = lane >> 2, t = lane & 3;
    long warp = (long)blockIdx.x * (blockDim.x >> 5) + (threadIdx.x >> 5), nw = (long)gridDim.x * (blockDim.x >> 5);
    float bb[4][4];
#pragma unroll
    for (int cs = 0; cs < 4; cs++) {
        bb[cs][0] = b2[cs * 16 + 2 * t], bb[cs][1] = b2[cs * 16 + 2 * t + 1];
        bb[cs][2] = b2[cs * 16 + 2 * t + 8], bb[cs][3] = b2[cs * 16 + 2 * t + 9];
    }
    int p = g & 3, oy = p >> 1, ox = p & 1;
    const uint32_t *y = (const uint32_t *)y2;   /* [pixel][32 channel pairs] */
    for (long tile = warp; tile < (long)(b + 3) / 4; tile += nw) {
        long na = tile * 4 + (g >> 2), nb = na + 2;
        bool ha = na < b, hb = nb < b;
        acc4 c[8] = {};
#pragma unroll 3
        for (int cell = 0; cell < 9; cell++) {
            int ky = cell / 3, kx = cell % 3, iy = 2 * oy - 1 + ky, ix = 2 * ox - 1 + kx;
            bool v = iy >= 0 && iy < 4 && ix >= 0 && ix < 4, va = v && ha, vb = v && hb;
            const uint32_t *ra = y + (na * P2 + iy * 4 + ix) * 32, *rb = y + (nb * P2 + iy * 4 + ix) * 32;
#pragma unroll
            for (int cs = 0; cs < 4; cs++) {
                int q = cs * 8 + t, kq = cell * 32 + cs * 8 + t;
                uint32_t A[4] = {va ? act2(ra[q], bb[cs][0], bb[cs][1]) : 0u, vb ? act2(rb[q], bb[cs][0], bb[cs][1]) : 0u,
                                 va ? act2(ra[q + 4], bb[cs][2], bb[cs][3]) : 0u,
                                 vb ? act2(rb[q + 4], bb[cs][2], bb[cs][3]) : 0u};
#pragma unroll
                for (int nt = 0; nt < 8; nt++) {
                    const uint32_t *wr = sw + (nt * 8 + g) * C3F_LD;
                    uint32_t B[2] = {wr[kq], wr[kq + 4]};
                    mma16816(c[nt], A, B);
                }
                acc_step(c, cell * 4 + cs);
            }
        }
        acc_fin(c);
#pragma unroll
        for (int nt = 0; nt < 8; nt++) {
            if (ha) *(uint32_t *)(y3 + (na * P3 + p) * NETH_NET_C3 + nt * 8 + 2 * t) = pack2(c[nt][0], c[nt][1]);
            if (hb) *(uint32_t *)(y3 + (nb * P3 + p) * NETH_NET_C3 + nt * 8 + 2 * t) = pack2(c[nt][2], c[nt][3]);
        }
    }
}

/* conv3's weight gradient without im2col (NETH_LEARN_BF16): D[co][(cell, ci)] over rows of 4 samples' 4 positions a k-step;
 * 8 samples at a time through shared memory (dx3's rows; conv2's relu(y2 + bias) with a zero pixel); a block's 18
 * warps a cell (eight n-tiles of 64 ci) and half the co (two m-tiles) each, the block's [64][576] to part, summed in
 * block order by k_dwsum */
#define C3W_BLK 82
#define C3W_S 8
#define C3W_L 72    /* a row of 64 bf16, padded */
__global__ void __launch_bounds__(576) k_conv3_dw_mma(int b, const bf *dx3, const bf *y2, const float *b2,
                                                      float *part) {
    __shared__ __align__(16) unsigned short sd[C3W_S * P3][C3W_L], sx[C3W_S][P2 + 1][C3W_L];
    int lane = threadIdx.x & 31, g = lane >> 2, t = lane & 3, cell = (threadIdx.x >> 5) % 9,
        m0 = (threadIdx.x >> 5) / 9 * 2;
    int ky = cell / 3, kx = cell % 3, pp = (t & 1) * 2;     /* rows 2t, 2t + 1: positions pp, pp + 1 */
    int pix[2];
#pragma unroll
    for (int j = 0; j < 2; j++) {
        int q = pp + j, iy = 2 * (q >> 1) - 1 + ky, ix = 2 * (q & 1) - 1 + kx;
        pix[j] = iy >= 0 && iy < 4 && ix >= 0 && ix < 4 ? iy * 4 + ix : P2;
    }
    acc4 c[2][8] = {};
    int kn = 0;
    for (int i = threadIdx.x; i < C3W_S * C3W_L; i += blockDim.x) sx[i / C3W_L][P2][i % C3W_L] = 0;
    for (long n0 = (long)blockIdx.x * C3W_S; n0 < b; n0 += (long)gridDim.x * C3W_S) {
        __syncthreads();
        for (int i = threadIdx.x; i < C3W_S * P3 * NETH_NET_C3 / 8; i += blockDim.x) {
            int r = i / (NETH_NET_C3 / 8), c8 = (i % (NETH_NET_C3 / 8)) * 8;
            long n = n0 + r / P3;
            *(uint4 *)&sd[r][c8] = n < b ? *(const uint4 *)(dx3 + (n0 * P3 + r) * NETH_NET_C3 + c8) : make_uint4(0, 0, 0, 0);
        }
        for (int i = threadIdx.x; i < C3W_S * P2 * NETH_NET_C2 / 8; i += blockDim.x) {
            int s = i / (P2 * NETH_NET_C2 / 8), q = (i / (NETH_NET_C2 / 8)) % P2, c8 = (i % (NETH_NET_C2 / 8)) * 8;
            float f[8];
            if (n0 + s < b) {
                vld(y2 + ((n0 + s) * P2 + q) * NETH_NET_C2 + c8, f);
                float4 b0 = __ldg((const float4 *)(b2 + c8)), b4 = __ldg((const float4 *)(b2 + c8 + 4));
                f[0] = fmaxf(f[0] + b0.x, 0.0f), f[1] = fmaxf(f[1] + b0.y, 0.0f), f[2] = fmaxf(f[2] + b0.z, 0.0f);
                f[3] = fmaxf(f[3] + b0.w, 0.0f), f[4] = fmaxf(f[4] + b4.x, 0.0f), f[5] = fmaxf(f[5] + b4.y, 0.0f);
                f[6] = fmaxf(f[6] + b4.z, 0.0f), f[7] = fmaxf(f[7] + b4.w, 0.0f);
            } else
                for (int j = 0; j < 8; j++) f[j] = 0.0f;
            vst((bf *)&sx[s][q][c8], f);
        }
        __syncthreads();
#pragma unroll 1
        for (int ks = 0; ks < C3W_S / 4; ks++) {
            int r0 = ks * 16 + 2 * t, sa = ks * 4 + (t >> 1), sb = sa + 2;   /* rows 2t, 2t+1 and + 8, + 9 */
            uint32_t A[2][4], B[8][2];
#pragma unroll
            for (int mt = 0; mt < 2; mt++) {
                int co = (m0 + mt) * 16 + g;
                A[mt][0] = sd[r0][co] | (uint32_t)sd[r0 + 1][co] << 16;
                A[mt][1] = sd[r0][co + 8] | (uint32_t)sd[r0 + 1][co + 8] << 16;
                A[mt][2] = sd[r0 + 8][co] | (uint32_t)sd[r0 + 9][co] << 16;
                A[mt][3] = sd[r0 + 8][co + 8] | (uint32_t)sd[r0 + 9][co + 8] << 16;
            }
#pragma unroll
            for (int nt = 0; nt < 8; nt++) {
                int ci = nt * 8 + g;
                B[nt][0] = sx[sa][pix[0]][ci] | (uint32_t)sx[sa][pix[1]][ci] << 16;
                B[nt][1] = sx[sb][pix[0]][ci] | (uint32_t)sx[sb][pix[1]][ci] << 16;
            }
#pragma unroll
            for (int mt = 0; mt < 2; mt++)
#pragma unroll
                for (int nt = 0; nt < 8; nt++) mma16816w(c[mt][nt], A[mt], B[nt]);
            acc_step(c, kn++);
        }
    }
    acc_fin(c);
    float *o = part + (long)blockIdx.x * NETH_NET_C3 * K3;
#pragma unroll
    for (int mt = 0; mt < 2; mt++)
#pragma unroll
        for (int nt = 0; nt < 8; nt++) {
            int co = (m0 + mt) * 16 + g, kc = cell * NETH_NET_C2 + nt * 8 + 2 * t;
            o[co * K3 + kc] = c[mt][nt][0], o[co * K3 + kc + 1] = c[mt][nt][1];
            o[(co + 8) * K3 + kc] = c[mt][nt][2], o[(co + 8) * K3 + kc + 1] = c[mt][nt][3];
        }
}

/* conv3's input gradient the same way (NETH_LEARN_BF16): a 3x3 kernel at stride 2, so an input pixel of parity 0 on an axis
 * is read by the kernel cell 1 alone (output iy / 2) and of parity 1 by cells 0 and 2 (outputs (iy + 1) / 2 and (iy -
 * 1) / 2, the first past the edge at iy 3); a (parity) tile is 4 samples' 4 pixels, K = (cells, co), N = ci (eight
 * n-tiles), B conv3's weights as [cell][ci][co] in shared memory, rows padded to 36 words (through L1: 157 us a
 * step). A warp a tile; the output times relu' (y2 + bias) */
#define C3X_BLK 164
#define C3X_SMEM (9 * NETH_NET_C2 * C2X_LD * 4)
__global__ void __launch_bounds__(512) k_conv3_dx_mma(int b, const bf *dx3, const bf *w3t, const bf *y2,
                                                      const float *b2, bf *dx2) {
    extern __shared__ uint32_t gw[];    /* [cell 9][ci 64][co pairs 32, padded to C2X_LD] */
    for (int i = threadIdx.x; i < NETH_LEARN_N_conv3_w / 2; i += blockDim.x)
        gw[(i >> 5) * C2X_LD + (i & 31)] = ((const uint32_t *)w3t)[i];
    __syncthreads();
    int lane = threadIdx.x & 31, g = lane >> 2, t = lane & 3;
    long warp = (long)blockIdx.x * (blockDim.x >> 5) + (threadIdx.x >> 5), nw = (long)gridDim.x * (blockDim.x >> 5);
    const uint32_t *d3 = (const uint32_t *)dx3;     /* [r][co pairs 32] */
    int q = g & 3, qy = q >> 1, qx = q & 1;     /* the pixel within the class: rows g and g + 8, samples g / 4, + 2 */
    for (long tile = warp; tile < (long)(b / 4) * 4; tile += nw) {
        long n0 = (tile >> 2) * 4;
        int py = (int)(tile >> 1) & 1, px = (int)tile & 1;
        int iy = py + 2 * qy, ix = px + 2 * qx;
        long na = n0 + (g >> 2), nb = na + 2;
        acc4 c[8] = {};
        int kn = 0;
        for (int sy = 0; sy <= py; sy++) {
            int ky = py ? 2 * sy : 1, oy = (iy + 1 - ky) >> 1;
            for (int sx = 0; sx <= px; sx++) {
                int kx = px ? 2 * sx : 1, ox = (ix + 1 - kx) >> 1;
                bool v = oy >= 0 && oy < 2 && ox >= 0 && ox < 2;
                const uint32_t *ra = d3 + (na * P3 + oy * 2 + ox) * 32, *rb = d3 + (nb * P3 + oy * 2 + ox) * 32;
                const uint32_t *wc = gw + (ky * 3 + kx) * NETH_NET_C2 * C2X_LD;
#pragma unroll
                for (int cs = 0; cs < 4; cs++) {
                    int qq = cs * 8 + t;
                    uint32_t A[4] = {v ? ra[qq] : 0u, v ? rb[qq] : 0u, v ? ra[qq + 4] : 0u, v ? rb[qq + 4] : 0u};
#pragma unroll
                    for (int nt = 0; nt < 8; nt++) {
                        const uint32_t *wr = wc + (nt * 8 + g) * C2X_LD;
                        uint32_t B[2] = {wr[qq], wr[qq + 4]};
                        mma16816(c[nt], A, B);
                    }
                    acc_step(c, kn++);
                }
            }
        }
        acc_fin(c);
        long pa = na * 16 + iy * 4 + ix, pb = nb * 16 + iy * 4 + ix;
#pragma unroll
        for (int nt = 0; nt < 8; nt++) {
            int ci = nt * 8 + 2 * t;
            float2 ya = h22f2(*(const nt162 *)(y2 + pa * NETH_NET_C2 + ci));
            float2 yb = h22f2(*(const nt162 *)(y2 + pb * NETH_NET_C2 + ci));
            float b0 = b2[ci], b1v = b2[ci + 1];
            *(uint32_t *)(dx2 + pa * NETH_NET_C2 + ci) =
                pack2(ya.x + b0 > 0.0f ? c[nt][0] : 0.0f, ya.y + b1v > 0.0f ? c[nt][1] : 0.0f);
            *(uint32_t *)(dx2 + pb * NETH_NET_C2 + ci) =
                pack2(yb.x + b0 > 0.0f ? c[nt][2] : 0.0f, yb.y + b1v > 0.0f ? c[nt][3] : 0.0f);
        }
    }
}

#endif /* NETH_LEARN_SPEC */

#if NETH_NET_STEM
/* ---- the stem on the tensor cores straight from the bytes (NETH_LEARN_BF16; lane/ntwidth): no im2col ----
 * lane/ntlearn's conv1 kernels at the stem's geometry: 64x64x3 in, 32x32x16 out, K 48 three k-steps of 16 whose step
 * is the channel (k = ci * 16 + ky * 4 + kx). A block stages one sample's bytes (12 KB) in shared memory at a time. */
#define SOB (NETH_NET_H * NETH_NET_W * NETH_NET_C)
#define P0P (OS + 2)        /* h0's side: the stem's output padded by one */
/* the stem's input at position p (32x32), channel ci, kernel cell (ky, kx): the byte / 255, 0 in the padding */
__device__ __forceinline__ float px0(const uint8_t *o, int p, int ci, int ky, int kx) {
    int iy = 2 * (p / OS) - 1 + ky, ix = 2 * (p % OS) - 1 + kx;
    return iy >= 0 && iy < NETH_NET_H && ix >= 0 && ix < NETH_NET_W ? (float)o[(iy * NETH_NET_W + ix) * NETH_NET_C + ci] * (float)(1.0 / 255)
                                                       : 0.0f;
}
__device__ __forceinline__ void stage_obs(uint8_t *so, const uint8_t *obs, const int32_t *idx, long n, int b) {
    for (int i = threadIdx.x; i < SOB / 16; i += blockDim.x)
        *(uint4 *)&so[i * 16] = n < b ? *(const uint4 *)(obs + (idx ? (long)idx[n] : n) * SOB + i * 16)
                                      : make_uint4(0, 0, 0, 0);
}

/* y0 = a0 W0^T and h0 = relu(y0 + b0) padded: a block a sample at a time, a warp 16 positions (half an output row) a
 * tile, the weights' 6 fragments in registers */
#define S0F_BLK 512
#define S0F_WARPS 8
__global__ void __launch_bounds__(S0F_WARPS * 32) k_stem_mma(int b, const int32_t *idx, const uint8_t *obs,
                                                            const bf *w0, const float *b0, bf *h0) {
    int lane = threadIdx.x & 31, g = lane >> 2, t = lane & 3, wib = threadIdx.x >> 5;
    uint32_t B[NETH_NET_C][2][2];
#pragma unroll
    for (int kk = 0; kk < NETH_NET_C; kk++)
#pragma unroll
        for (int nt = 0; nt < 2; nt++) {
            const bf *wr = w0 + (nt * 8 + g) * K0 + kk * 16 + 2 * t;
            B[kk][nt][0] = *(const uint32_t *)wr;
            B[kk][nt][1] = *(const uint32_t *)(wr + 8);
        }
    int ky = t >> 1, kx = (t & 1) * 2;
    __shared__ __align__(16) uint8_t so[SOB];
    for (long n = blockIdx.x; n < b; n += gridDim.x) {
        __syncthreads();
        stage_obs(so, obs, idx, n, b);
        __syncthreads();
#pragma unroll 1
        for (int tq = wib; tq < P0 / 16; tq += S0F_WARPS) {
            int pa = tq * 16 + g, pb = pa + 8;
            acc4 c[2] = {};
#pragma unroll
            for (int kk = 0; kk < NETH_NET_C; kk++) {
                uint32_t A[4] = {pack2(px0(so, pa, kk, ky, kx), px0(so, pa, kk, ky, kx + 1)),
                                 pack2(px0(so, pb, kk, ky, kx), px0(so, pb, kk, ky, kx + 1)),
                                 pack2(px0(so, pa, kk, ky + 2, kx), px0(so, pa, kk, ky + 2, kx + 1)),
                                 pack2(px0(so, pb, kk, ky + 2, kx), px0(so, pb, kk, ky + 2, kx + 1))};
#pragma unroll
                for (int nt = 0; nt < 2; nt++) mma16816(c[nt], A, B[kk][nt]);
            }
            acc_fin(c);
            /* h0 = relu(x0 + b0) from x0's rounding, into the padded map (x0 itself is not kept: k_c1dx's relu'
             * reads h0 > 0, which is x0 + b0 > 0) */
            bf *ha = h0 + ((n * P0P + pa / OS + 1) * P0P + pa % OS + 1) * NETH_NET_C0,
               *hb = h0 + ((n * P0P + pb / OS + 1) * P0P + pb % OS + 1) * NETH_NET_C0;
#pragma unroll
            for (int nt = 0; nt < 2; nt++) {
                uint32_t va = pack2(c[nt][0], c[nt][1]), vb = pack2(c[nt][2], c[nt][3]);
                int ch = nt * 8 + 2 * t;
                float2 fa = h22f2(*(nt162 *)&va), fb = h22f2(*(nt162 *)&vb);
                *(uint32_t *)(ha + ch) = pack2(fmaxf(fa.x + b0[ch], 0.0f), fmaxf(fa.y + b0[ch + 1], 0.0f));
                *(uint32_t *)(hb + ch) = pack2(fmaxf(fb.x + b0[ch], 0.0f), fmaxf(fb.y + b0[ch + 1], 0.0f));
            }
        }
    }
}

/* the stem's weight and bias gradients: D[co][kc] = sum over rows r of dx0[r][co] a0[r][kc], kc 0..47 a0's columns
 * (rebuilt from the bytes), kc 48 a column of ones (the bias), 49..55 zero; M = co (one m-tile), N = kc (seven
 * n-tiles), K = the rows, 16 a step. S0W_BLK blocks of 4 warps take a sample at a time (its bytes and half its dx0
 * rows at once through shared memory, rows padded to 24 against bank conflicts), a warp every fourth k-step; the
 * warps' partials summed in warp order into the block's part[blk][16][49]; k_stem_dwsum sums the blocks in order */
#define S0W_BLK 256
#define SW (K0 + 1)
#define S0W_DL 24
#define S0W_R (P0 / 2)
__global__ void __launch_bounds__(128) k_stem_dw_mma(int b, const int32_t *idx, const uint8_t *obs, const bf *dx0,
                                                     float *part) {
    __shared__ __align__(16) unsigned short sd[S0W_R][S0W_DL];
    __shared__ __align__(16) uint8_t so[SOB];
    int lane = threadIdx.x & 31, g = lane >> 2, t = lane & 3, wib = threadIdx.x >> 5;
    acc4 c[7] = {};
    int ks = 0;
    for (long n = blockIdx.x; n < b; n += gridDim.x) {
        __syncthreads();
        stage_obs(so, obs, idx, n, b);
        for (int h = 0; h < 2; h++) {
            if (h) __syncthreads();
            for (int i = threadIdx.x; i < S0W_R * NETH_NET_C0 / 8; i += blockDim.x) {
                int r = i / (NETH_NET_C0 / 8), c8 = (i % (NETH_NET_C0 / 8)) * 8;
                *(uint4 *)&sd[r][c8] = *(const uint4 *)(dx0 + (n * P0 + h * S0W_R + r) * NETH_NET_C0 + c8);
            }
            __syncthreads();
#pragma unroll 1
            for (int p0 = wib * 16; p0 < S0W_R; p0 += 64) {
                int r = p0 + 2 * t, q = h * S0W_R + r;   /* q: the position in the sample */
                uint32_t A[4], B[7][2];
                A[0] = sd[r][g] | (uint32_t)sd[r + 1][g] << 16;
                A[1] = sd[r][g + 8] | (uint32_t)sd[r + 1][g + 8] << 16;
                A[2] = sd[r + 8][g] | (uint32_t)sd[r + 9][g] << 16;
                A[3] = sd[r + 8][g + 8] | (uint32_t)sd[r + 9][g + 8] << 16;
#pragma unroll
                for (int nt = 0; nt < 6; nt++) {
                    int kc = nt * 8 + g, ci = kc >> 4, ky = (kc >> 2) & 3, kx = kc & 3;
                    B[nt][0] = pack2(px0(so, q, ci, ky, kx), px0(so, q + 1, ci, ky, kx));
                    B[nt][1] = pack2(px0(so, q + 8, ci, ky, kx), px0(so, q + 9, ci, ky, kx));
                }
                B[6][0] = B[6][1] = g == 0 ? pack2(1.0f, 1.0f) : 0u;
#pragma unroll
                for (int nt = 0; nt < 7; nt++) mma16816w(c[nt], A, B[nt]);
                acc_step(c, ks++);
            }
        }
    }
    acc_fin(c);
    __syncthreads();
    float(*red)[NETH_NET_C0 * 56] = (float(*)[NETH_NET_C0 * 56])&sd[0][0];   /* 4 x 3.5 KB of the 24 KB */
#pragma unroll
    for (int nt = 0; nt < 7; nt++) {
        float *r = red[wib] + g * 56 + nt * 8 + 2 * t;
        r[0] = c[nt][0], r[1] = c[nt][1], r[8 * 56] = c[nt][2], r[8 * 56 + 1] = c[nt][3];
    }
    __syncthreads();
    for (int i = threadIdx.x; i < NETH_NET_C0 * SW; i += blockDim.x) {
        int co = i / SW, kc = i % SW, j = co * 56 + kc;
        part[(long)blockIdx.x * NETH_NET_C0 * SW + i] = (red[0][j] + red[1][j]) + (red[2][j] + red[3][j]);
    }
}

/* the blocks' partials in block order into the stem's weight gradient [16][48] and bias gradient [16] */
__global__ void k_stem_dwsum(int nblk, const float *part, float *gw, float *gb) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= NETH_NET_C0 * SW) return;
    float s = 0.0f;
    for (int k = 0; k < nblk; k++) s += part[(long)k * NETH_NET_C0 * SW + i];
    int co = i / SW, kc = i % SW;
    if (kc == K0)
        gb[co] = s;
    else
        gw[co * K0 + kc] = s;
}
#endif /* NETH_NET_STEM */

/* ---- implicit GEMMs (NETH_LEARN_BF16; lane/ntwidth): conv1 over the stem without im2col, da1 or col2im, and conv2's input
 * gradient, forward and weight gradient without im2col, da2 or col2im (NETH_LEARN_IC2) ----
 * k_stem_mma also writes h0 = relu(x0 + b0) in bf16 (from x0's own rounding, as k_im2col reads it) into a map padded by
 * one (P0P x P0P, its border zero from neth_learn_new), so every 16 bytes of conv1's A (8 channels of one kernel cell) is one
 * cp.async: the forward (x1 = A W1^T) is k_igemm, the weight gradient k_cwgrad, the input gradient k_c1dx (an input
 * pixel of parity (qy, qx) is read by the cells ky = 1 - qy + 2 sy, kx = 1 - qx + 2 sx at the output (j + qy - sy,
 * i + qx - sx): four GEMMs, one a parity, K = (sy, sx, co)). */
__device__ __forceinline__ void cp16(void *s, const void *g, bool valid) {
    unsigned sa = (unsigned)__cvta_generic_to_shared(s);
    asm volatile("cp.async.cg.shared.global [%0], [%1], 16, %2;\n" ::"r"(sa), "l"(g), "r"(valid ? 16 : 0));
}
__device__ __forceinline__ void cp_commit() { asm volatile("cp.async.commit_group;\n" ::); }
template <int N>
__device__ __forceinline__ void cp_wait() { asm volatile("cp.async.wait_group %0;\n" ::"n"(N)); }
__device__ __forceinline__ void ldm4(uint32_t *r, const void *p) {
    unsigned a = (unsigned)__cvta_generic_to_shared(p);
    asm volatile("ldmatrix.sync.aligned.m8n8.x4.shared.b16 {%0, %1, %2, %3}, [%4];\n"
                 : "=r"(r[0]), "=r"(r[1]), "=r"(r[2]), "=r"(r[3]) : "r"(a));
}
__device__ __forceinline__ void ldm4t(uint32_t *r, const void *p) {
    unsigned a = (unsigned)__cvta_generic_to_shared(p);
    asm volatile("ldmatrix.sync.aligned.m8n8.x4.trans.shared.b16 {%0, %1, %2, %3}, [%4];\n"
                 : "=r"(r[0]), "=r"(r[1]), "=r"(r[2]), "=r"(r[3]) : "r"(a));
}

/* a stride-2, 4x4, 1-padded convolution's input gradient for parity z = (qy, qx) of the input (H = 2 O wide, CI
 * channels), from the output's gradient dy [n][O][O][CO]: row m = (n, j, i) of the parity's O x O pixels, column k =
 * (sy * 2 + sx) * CO + co: dy at the output (j + qy - sy, i + qx - sx), zero outside */
template <int O, int CO>
struct cx_a {
    const bf *dy;
    __device__ const bf *at(int m, int k, int z, bool &ok) const {
        int n = m / (O * O), j = m / O % O, i = m % O, s = k / CO, co = k % CO;
        int oy = j + (z >> 1) - (s >> 1), ox = i + (z & 1) - (s & 1);
        ok = oy >= 0 && oy < O && ox >= 0 && ox < O;
        return dy + (((long)n * O + oy) * O + ox) * CO + co;
    }
};
/* its output: dx at the pixel (2j + qy, 2i + qx), times relu' (x + bias > 0) as k_col2im's (x the layer before's GEMM
 * output, before its bias), or (h not null) h > 0, h the layer before's relu(x + bias) padded by one */
template <int O, int CI>
struct cx_o {
    const bf *x;
    const float *bias;
    bf *dx;
    const bf *h;
    __device__ void operator()(int m, int n, int z, const float *v) const {
        int b = m / (O * O), j = m / O % O, i = m % O;
        long r = ((long)b * 2 * O + 2 * j + (z >> 1)) * 2 * O + 2 * i + (z & 1);
        float f[8], s[8];
        if (h) {
            vld(h + (((long)b * (2 * O + 2) + 2 * j + (z >> 1) + 1) * (2 * O + 2) + 2 * i + (z & 1) + 1) * CI + n, f);
#pragma unroll
            for (int q = 0; q < 8; q++) s[q] = f[q] > 0.0f ? v[q] : 0.0f;
        } else {
            vld(x + r * CI + n, f);
#pragma unroll
            for (int q = 0; q < 8; q++) s[q] = f[q] + bias[n + q] > 0.0f ? v[q] : 0.0f;
        }
        vst(dx + r * CI + n, s);
    }
};

/* a stride-2, 4x4 convolution's A over h, the layer before's relu(x + bias) padded by one (HP = 2 O + 2 wide, C
 * channels): row m = (n, oy, ox) of O x O, column k = (ky * 4 + kx) * C + ci; 8 columns from k */
template <int O, int C>
struct conv_h {
    const bf *h;
    __device__ const bf *at(int m, int k, int z, bool &ok) const {
        constexpr int HP = 2 * O + 2;
        int n = m / (O * O), oy = m / O % O, ox = m % O, cell = k / C;
        ok = true;
        return h + (((long)n * HP + 2 * oy + (cell >> 2)) * HP + 2 * ox + (cell & 3)) * C + k % C;
    }
};
/* a forward's output: x [m][C] dense, no bias (the next layer's input adds it); with h (not null) also relu(x + bias)
 * from x's rounding into the next layer's padded map (O x O inside) */
template <int O, int C>
struct conv_o {
    bf *x;
    const float *bias;
    bf *h;
    __device__ void operator()(int m, int n, int z, const float *v) const {
        if (x) vst(x + (long)m * C + n, v);
        if (h) {
            float f[8];
#pragma unroll
            for (int q = 0; q < 8; q++) f[q] = fmaxf(h2f(f2h(v[q])) + bias[n + q], 0.0f);
            int b = m / (O * O), oy = m / O % O, ox = m % O;
            vst(h + (((long)b * (O + 2) + oy + 1) * (O + 2) + ox + 1) * C + n, f);
        }
    }
};
/* h = relu(x + bias) padded by one (x [n][O][O][C], the GEMM's output before its bias; h's border stays zero), 8
 * channels a thread */
template <int O, int C>
__global__ void k_actpad(int b, const bf *x, const float *bias, bf *h) {
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= (long)b * O * O * C / 8) return;
    int c = (t % (C / 8)) * 8;
    long r = t / (C / 8);
    int n = r / (O * O), oy = r / O % O, ox = r % O;
    float f[8];
    vld(x + r * C + c, f);
#pragma unroll
    for (int q = 0; q < 8; q++) f[q] = fmaxf(f[q] + bias[c + q], 0.0f);
    vst(h + (((long)n * (O + 2) + oy + 1) * (O + 2) + ox + 1) * C + c, f);
}

/* out(m, n..n+7) = sum_k A(m, k) B[k][n] over a grid of BM x BN tiles (blockIdx.z the loader's and the output's z),
 * K in chunks of KC, two chunks in flight (cp.async; A's rows and B's rows padded by 16 bytes for ldmatrix), a warp
 * FM x FN 16x16 tiles; B is [K][N] bf16 */
template <int BM, int BN, int KC, int FM, int FN, int K, class A, class O>
__global__ void __launch_bounds__((BM / (16 * FM)) * (BN / (16 * FN)) * 32)
k_igemm(A a, const bf *__restrict__ wb, O o, int M, int N) {
    constexpr int WM = 16 * FM, WN = 16 * FN, TW = (BM / WM) * (BN / WN), NT = TW * 32, K8 = KC / 8, N8 = BN / 8;
    constexpr int AS = KC + 8, WS = BN + 8, STAGE = BM * AS + KC * WS;
    constexpr int NA = BM * K8, NW = KC * N8, PA = (NA + NT - 1) / NT, PW = (NW + NT - 1) / NT;
    constexpr int SMB = 2 * STAGE * 2 > BM * (BN + 4) * 4 ? 2 * STAGE * 2 : BM * (BN + 4) * 4;
    static_assert(K % KC == 0 && KC % 16 == 0 && BM % WM == 0 && BN % WN == 0, "the tiles");
    __shared__ __align__(128) unsigned char sm[SMB];
    bf *st = (bf *)sm;
    int m0 = blockIdx.x * BM, n0 = blockIdx.y * BN, z = blockIdx.z, t = threadIdx.x, lane = t & 31, w = t >> 5;
    int wm0 = w / (BN / WN) * WM, wn0 = w % (BN / WN) * WN;
    auto issue = [&](int c) {
        bf *As = st + (c & 1) * STAGE, *Ws = As + BM * AS;
#pragma unroll
        for (int u = 0; u < PA; u++) {
            int i = t + u * NT, mm = i / K8;
            if (NA % NT == 0 || i < NA) {
                bool ok = m0 + mm < M;
                const bf *g = ok ? a.at(m0 + mm, c * KC + i % K8 * 8, z, ok) : wb;
                cp16(As + mm * AS + i % K8 * 8, ok ? g : wb, ok);
            }
        }
#pragma unroll
        for (int u = 0; u < PW; u++) {
            int i = t + u * NT;
            if (NW % NT == 0 || i < NW)
                cp16(Ws + i / N8 * WS + i % N8 * 8, wb + ((long)z * K + c * KC + i / N8) * N + n0 + i % N8 * 8, true);
        }
        cp_commit();
    };
    acc4 acc[FM][2 * FN] = {};
    constexpr int nch = K / KC;
    issue(0);
    for (int c = 0; c < nch; c++) {
        if (c + 1 < nch) {
            issue(c + 1);
            cp_wait<1>();
        } else
            cp_wait<0>();
        __syncthreads();
        const bf *As = st + (c & 1) * STAGE, *Ws = As + BM * AS;
#pragma unroll
        for (int kk = 0; kk < KC; kk += 16) {
            uint32_t af[FM][4], bfr[FN][4];
#pragma unroll
            for (int i = 0; i < FM; i++) ldm4(af[i], As + (wm0 + i * 16 + (lane & 15)) * AS + kk + (lane >> 4) * 8);
#pragma unroll
            for (int j = 0; j < FN; j++) ldm4t(bfr[j], Ws + (kk + (lane & 15)) * WS + wn0 + j * 16 + (lane >> 4) * 8);
#pragma unroll
            for (int i = 0; i < FM; i++)
#pragma unroll
                for (int j = 0; j < 2 * FN; j++) mma16816(acc[i][j], af[i], &bfr[j / 2][(j & 1) * 2]);
            acc_step(acc, c * (KC / 16) + kk / 16);
        }
        __syncthreads();
    }
    acc_fin(acc);
    /* through shared memory, then 8 columns a thread */
    float *red = (float *)sm;
    constexpr int RS = BN + 4;
#pragma unroll
    for (int i = 0; i < FM; i++)
#pragma unroll
        for (int j = 0; j < 2 * FN; j++) {
            float *r = red + (wm0 + i * 16 + (lane >> 2)) * RS + wn0 + j * 8 + (lane & 3) * 2;
            r[0] = acc[i][j][0], r[1] = acc[i][j][1], r[8 * RS] = acc[i][j][2], r[8 * RS + 1] = acc[i][j][3];
        }
    __syncthreads();
    for (int e = t; e < BM * N8; e += NT) {
        int mm = e / N8, c8 = e % N8 * 8;
        if (m0 + mm < M) o(m0 + mm, n0 + c8, z, red + mm * RS + c8);
    }
}

/* k_igemm's launch: Z GEMMs (the grid's z) of M x N; the tiles (BM, BN, KC, FM, FN) per call site, NETH_LEARN_T1F (conv1
 * over the stem's output), NETH_LEARN_T2F (conv2's forward), NETH_LEARN_T2X (conv2's input gradient) */
template <int BM, int BN, int KC, int FM, int FN, int K, class A, class O>
static void igemm(A a, const bf *wb, O o, int M, int N, int Z, cudaStream_t s) {
    k_igemm<BM, BN, KC, FM, FN, K, A, O>
        <<<dim3((M + BM - 1) / BM, N / BN, Z), (BM / (16 * FM)) * (BN / (16 * FN)) * 32, 0, s>>>(a, wb, o, M, N);
}
#ifndef NETH_LEARN_T1F
#define NETH_LEARN_T1F 128, NETH_NET_C1, 32, 2, (NETH_NET_C1 >= 32 ? 2 : 1)
#endif
#ifndef NETH_LEARN_T2F
#define NETH_LEARN_T2F 128, (NETH_NET_C2 < 64 ? NETH_NET_C2 : 64), 32, 2, 2
#endif
#ifndef NETH_LEARN_T2X
#define NETH_LEARN_T2X 128, (NETH_NET_C1 < 64 ? NETH_NET_C1 : 64), 32, 2, 2
#endif

/* a convolution's weight gradient: D[co][k] = sum over rows r of dy[r][co] A(r, k) (CO rows, KD columns), the grid's
 * x a slice of rows (rows a block, a multiple of 32, staged 32 at a time), its y 256 columns, its z MT of the CO rows:
 * 8 warps each MT rows and 32 columns (dy's tile the A operand through ldmatrix.trans, A's the B operand); each
 * slice's partial [CO][KD] summed with the others in order by k_slicesum */
#define CW_BLK 164          /* blocks over all slices and tiles, about */
#define CW_KC 32
__global__ void k_slicesum(int n, int ns, const float *part, float *c) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    float s = 0.0f;
    for (int k = 0; k < ns; k++) s += part[(long)k * n + i];
    c[i] = s;
}
template <int CO, int KD, int MT, class A>
__global__ void __launch_bounds__(256) k_cwgrad(A a, const bf *__restrict__ dy, long R, long rows, float *part) {
    constexpr int FM = MT / 16, DS = MT + 8, AS = 256 + 8, STAGE = CW_KC * DS + CW_KC * AS;
    constexpr int ND = CW_KC * MT / 8, NA = CW_KC * 256 / 8;
    static_assert(KD % 256 == 0 && CO % MT == 0 && MT % 16 == 0 && MT <= 64, "eight warps of 32 columns");
    __shared__ __align__(128) bf st[2 * STAGE];
    int t = threadIdx.x, lane = t & 31, w = t >> 5, wn0 = w * 32, k0 = blockIdx.y * 256, c0 = blockIdx.z * MT;
    long r0 = blockIdx.x * rows, r1 = r0 + rows < R ? r0 + rows : R;
    auto issue = [&](long r, int buf) {
        bf *Ds = st + buf * STAGE, *As = Ds + CW_KC * DS;
        for (int i = t; i < ND; i += 256) {
            int rr = i / (MT / 8), c8 = i % (MT / 8) * 8;
            bool ok = r + rr < r1;
            cp16(Ds + rr * DS + c8, ok ? dy + (r + rr) * CO + c0 + c8 : dy, ok);
        }
        for (int i = t; i < NA; i += 256) {
            int rr = i / (256 / 8), k8 = i % (256 / 8) * 8;
            bool ok = r + rr < r1;
            const bf *g = ok ? a.at((int)(r + rr), k0 + k8, 0, ok) : dy;
            cp16(As + rr * AS + k8, ok ? g : dy, ok);
        }
        cp_commit();
    };
    acc4 acc[FM][4] = {};
    int buf = 0, kn = 0;
    if (r0 < r1) issue(r0, 0);
    for (long r = r0; r < r1; r += CW_KC, buf ^= 1) {
        if (r + CW_KC < r1) {
            issue(r + CW_KC, buf ^ 1);
            cp_wait<1>();
        } else
            cp_wait<0>();
        __syncthreads();
        const bf *Ds = st + buf * STAGE, *As = Ds + CW_KC * DS;
#pragma unroll
        for (int kk = 0; kk < CW_KC; kk += 16) {
            uint32_t af[FM][4], bfr[2][4];
#pragma unroll
            for (int i = 0; i < FM; i++)
                ldm4t(af[i], Ds + (kk + (lane & 7) + ((lane >> 4) & 1) * 8) * DS + i * 16 + ((lane >> 3) & 1) * 8);
#pragma unroll
            for (int j = 0; j < 2; j++) ldm4t(bfr[j], As + (kk + (lane & 15)) * AS + wn0 + j * 16 + (lane >> 4) * 8);
#pragma unroll
            for (int i = 0; i < FM; i++)
#pragma unroll
                for (int j = 0; j < 4; j++) mma16816w(acc[i][j], af[i], &bfr[j / 2][(j & 1) * 2]);
            acc_step(acc, kn++);
        }
        __syncthreads();
    }
    acc_fin(acc);
    float *p = part + (long)blockIdx.x * CO * KD + (long)c0 * KD + k0;
    int g = lane >> 2, q = lane & 3;
#pragma unroll
    for (int i = 0; i < FM; i++)
#pragma unroll
        for (int j = 0; j < 4; j++) {
            float *r = p + (i * 16 + g) * KD + wn0 + j * 8 + q * 2;
            r[0] = acc[i][j][0], r[1] = acc[i][j][1], r[8 * KD] = acc[i][j][2], r[8 * KD + 1] = acc[i][j][3];
        }
}

/* the slices of the grid's x for a weight gradient of CO x KD with MT-row tiles, scaled to 2 waves of the device's SMs */
template <int CO, int KD, int MT>
static inline int cw_slices_n(int nsm) {
    constexpr int tiles = (KD / 256) * (CO / MT);
    int target = 2 * nsm > CW_BLK ? 2 * nsm : CW_BLK;
    int ns = target / tiles;
    return ns > 0 ? ns : 1;
}
/* D (fp32 [CO][KD]) = dy^T A over R rows: k_cwgrad's slices, then their sum in order */
template <int CO, int KD, class A>
static void cwgrad(neth_learn *L, A a, const bf *dy, long R, float *D) {
    constexpr int MT = CO < 64 ? CO : 64;
    int ns = cw_slices_n<CO, KD, MT>(L->nsm);
    long rows = (R + (long)ns * CW_KC - 1) / ((long)ns * CW_KC) * CW_KC;
    k_cwgrad<CO, KD, MT, A><<<dim3(ns, KD / 256, CO / MT), 256, 0, L->s>>>(a, dy, R, rows, L->cwpart);
    k_slicesum<<<(CO * KD + 255) / 256, 256, 0, L->s>>>(CO * KD, ns, L->cwpart, D);
}

#if NETH_NET_STEM
/* conv1's input gradient: a block a sample (8 warps, each the output rows j = w and w + 8), dx1's 18 x 18 map with
 * its zero border staged once in shared memory (rows padded by 16 bytes), then for each input parity z = (qy, qx) a
 * row's 16 pixels (2j + qy, 2i + qx) are a m16 tile with K = (sy, sx, co): its A rows the map's (j + qy - sy, i + qx -
 * sx) by ldmatrix, its B (w1x: [z][ci][(sy, sx, co)]) 32-bit pairs from global memory (L1), each used for the warp's
 * two tiles; the output times relu' (h0 > 0: x0 + b0 > 0, as k_col2im's) */
#define C1X_PD (NETH_NET_C1 + 8)
#define C1X_R 2             /* output rows a warp */
__global__ void __launch_bounds__(256) k_c1dx(int b, const bf *__restrict__ dx1, const bf *__restrict__ w1x,
                                              const bf *__restrict__ h0, bf *dx0) {
    constexpr int K4 = 4 * NETH_NET_C1;
    static_assert(NETH_NET_O1 == 8 * C1X_R, "8 warps of C1X_R rows");
    __shared__ __align__(16) bf P[NETH_NET_O1 + 2][NETH_NET_O1 + 2][C1X_PD];
    int n = blockIdx.x, t = threadIdx.x, lane = t & 31, w = t >> 5;
    int g = lane >> 2, q4 = lane & 3;
    for (int i = t; i < (NETH_NET_O1 + 2) * (NETH_NET_O1 + 2) * (NETH_NET_C1 / 8); i += 256) {
        int jj = i / ((NETH_NET_O1 + 2) * (NETH_NET_C1 / 8)), ii = i / (NETH_NET_C1 / 8) % (NETH_NET_O1 + 2), c8 = i % (NETH_NET_C1 / 8) * 8;
        int oy = jj - 1, ox = ii - 1;
        bool ok = oy >= 0 && oy < NETH_NET_O1 && ox >= 0 && ox < NETH_NET_O1;
        cp16(&P[jj][ii][c8], ok ? dx1 + (((long)n * NETH_NET_O1 + oy) * NETH_NET_O1 + ox) * NETH_NET_C1 + c8 : dx1, ok);
    }
    cp_commit();
    cp_wait<0>();
    __syncthreads();
#pragma unroll 1
    for (int z = 0; z < 4; z++) {
        int qy = z >> 1, qx = z & 1;
        acc4 acc[C1X_R][2] = {};
        const bf *wz = w1x + (long)z * NETH_NET_C0 * K4;
#pragma unroll
        for (int s = 0; s < 4; s++) {
#pragma unroll
            for (int kk = 0; kk < NETH_NET_C1; kk += 16) {
                uint32_t bb[2][2];
#pragma unroll
                for (int nt = 0; nt < 2; nt++) {
                    const bf *wr = wz + (nt * 8 + g) * K4 + s * NETH_NET_C1 + kk + 2 * q4;
                    bb[nt][0] = __ldg((const unsigned *)wr);
                    bb[nt][1] = __ldg((const unsigned *)(wr + 8));
                }
#pragma unroll
                for (int rr = 0; rr < C1X_R; rr++) {
                    uint32_t a[4];
                    ldm4(a, &P[w + rr * 8 + 1 + qy - (s >> 1)][(lane & 15) + qx - (s & 1) + 1][(lane >> 4) * 8 + kk]);
                    mma16816(acc[rr][0], a, bb[0]);
                    mma16816(acc[rr][1], a, bb[1]);
                }
                acc_step(acc, s * (NETH_NET_C1 / 16) + kk / 16);
            }
        }
        acc_fin(acc);
        /* rows g and g + 8 (i), columns nt * 8 + 2 q4 (+1) (ci) */
#pragma unroll
        for (int rr = 0; rr < C1X_R; rr++)
#pragma unroll
            for (int hr = 0; hr < 2; hr++) {
                int i = g + hr * 8, j = w + rr * 8;
                long r = ((long)n * OS + 2 * j + qy) * OS + 2 * i + qx;
                long rh = ((long)n * P0P + 2 * j + qy + 1) * P0P + 2 * i + qx + 1;
#pragma unroll
                for (int nt = 0; nt < 2; nt++) {
                    int c = nt * 8 + 2 * q4;
                    float2 f = h22f2(*(const nt162 *)(h0 + rh * NETH_NET_C0 + c));
                    float v0 = f.x > 0.0f ? acc[rr][nt][2 * hr] : 0.0f, v1 = f.y > 0.0f ? acc[rr][nt][2 * hr + 1] : 0.0f;
                    *(uint32_t *)(dx0 + r * NETH_NET_C0 + c) = pack2(v0, v1);
                }
            }
    }
}

#if NETH_LEARN_S1B || NETH_LEARN_S1F
/* conv1's whole backward in one kernel (lane/ntbytes, NETH_LEARN_S1B): its weight and bias gradients, its input's gradient
 * and the stem's weight and bias gradients, a sample at a time in shared memory, h0 recomputed from the bytes (the
 * stem's forward, the same arithmetic as k_stem_mma's, so the same bits) and dx0 never written: DRAM reads only the
 * bytes and dx1, where cwgrad, k_c1dx and k_stem_dw_mma moved h0 three times and dx0 twice (2.3 GB a minibatch of
 * 8192). A block of 16 warps a sample: the stem's 64 tiles of 16 positions; conv1's weight gradient D[co][(cell, ci)]
 * over the sample's 256 positions (a warp 32 columns, every other output row); its input gradient transposed (dx0^T
 * = W1^T dy^T, M = ci, N = the 16 pixels of a parity row, K = (sy, sx, co)), so its accumulator is the A operand of
 * the stem's weight gradient (M = ci, K = those pixels, N = the bytes' columns, as k_stem_dw_mma's). The next sample's
 * bytes load during this one's GEMMs and its dx1 during its stem. Each block's sums to part, k_s1bsum sums them in
 * block order. h0 in shared memory is [y][x parity][x / 2][16], the two 8-channel halves swapped where bit 2 of x / 2
 * is set (ldmatrix's eight rows on eight bank groups) */
#define S1B_W 16
/* k_s1b's and k_s1f's grids (a block an SM, by their shared memory): S1_WAVES waves of the SMs, a block's samples
 * spb = b / (S1_WAVES x SMs) rounded up (s1_spb). One wave was the fastest alone (k_s1b 2.12 ms at mb 8192 against
 * 2.39 at 16 samples a block, 6.2 waves, and 2.71 at 32, 3.1), but a block an SM for the whole kernel stretched to 5.2
 * ms beside another process's kernels */
#ifndef S1_WAVES
#define S1_WAVES 2
#endif
#define S1B_HN (P0P * 2 * (P0P / 2) * NETH_NET_C0)    /* h0's elements */
#define S1B_PN ((NETH_NET_O1 + 2) * (NETH_NET_O1 + 2) * C1X_PD)
#define S1B_SMEM (2 * SOB + 2 * S1B_HN + 2 * S1B_PN)
#define S1B_PART (NETH_NET_C1 * K1 + NETH_NET_C1 + NETH_NET_C0 * SW)
static_assert(S1B_SMEM >= 2 * NETH_NET_C1 * K1 * 4 && S1B_SMEM >= S1B_W * NETH_NET_C0 * 56 * 4, "the reductions fit");
__device__ __forceinline__ int s1h(int y, int x, int c) {
    return ((y * 2 + (x & 1)) * (P0P / 2) + (x >> 1)) * NETH_NET_C0 + ((((c >> 3) ^ (x >> 3)) & 1) << 3) + (c & 7);
}
__device__ __forceinline__ float s1hv(const bf *h, int y, int x, int c) { return h2f(h[s1h(y, x, c)]); }
/* the stem's weights as k_stem_mma's B fragments */
__device__ __forceinline__ void s1_w0(const bf *w0, uint32_t (&B0)[NETH_NET_C][2][2]) {
    int g = (threadIdx.x & 31) >> 2, q = threadIdx.x & 3;
#pragma unroll
    for (int kk = 0; kk < NETH_NET_C; kk++)
#pragma unroll
        for (int nt = 0; nt < 2; nt++) {
            const bf *wr = w0 + (nt * 8 + g) * K0 + kk * 16 + 2 * q;
            B0[kk][nt][0] = *(const uint32_t *)wr;
            B0[kk][nt][1] = *(const uint32_t *)(wr + 8);
        }
}
/* the stem's forward for the sample in so (warp w of nw): h = relu(x0 + b0) inside h0's border, k_stem_mma's tiles and
 * arithmetic (the same bits as its h0) */
__device__ __forceinline__ void s1_stem(const uint8_t *so, const uint32_t (&B0)[NETH_NET_C][2][2], const float *b0, bf *h,
                                        int w, int nw) {
    int lane = threadIdx.x & 31, g = lane >> 2, q = lane & 3, ky = q >> 1, kx = (q & 1) * 2;
#pragma unroll 1
    for (int tq = w; tq < P0 / 16; tq += nw) {
        int pa = tq * 16 + g, pb = pa + 8;
        acc4 c[2] = {};
#pragma unroll
        for (int kk = 0; kk < NETH_NET_C; kk++) {
            uint32_t A[4] = {pack2(px0(so, pa, kk, ky, kx), px0(so, pa, kk, ky, kx + 1)),
                             pack2(px0(so, pb, kk, ky, kx), px0(so, pb, kk, ky, kx + 1)),
                             pack2(px0(so, pa, kk, ky + 2, kx), px0(so, pa, kk, ky + 2, kx + 1)),
                             pack2(px0(so, pb, kk, ky + 2, kx), px0(so, pb, kk, ky + 2, kx + 1))};
#pragma unroll
            for (int nt = 0; nt < 2; nt++) mma16816(c[nt], A, B0[kk][nt]);
        }
        acc_fin(c);
#pragma unroll
        for (int nt = 0; nt < 2; nt++) {
            uint32_t va = pack2(c[nt][0], c[nt][1]), vb = pack2(c[nt][2], c[nt][3]);
            int ch = nt * 8 + 2 * q;
            float2 fa = h22f2(*(nt162 *)&va), fb = h22f2(*(nt162 *)&vb);
            *(uint32_t *)&h[s1h(pa / OS + 1, pa % OS + 1, ch)] =
                pack2(fmaxf(fa.x + b0[ch], 0.0f), fmaxf(fa.y + b0[ch + 1], 0.0f));
            *(uint32_t *)&h[s1h(pb / OS + 1, pb % OS + 1, ch)] =
                pack2(fmaxf(fb.x + b0[ch], 0.0f), fmaxf(fb.y + b0[ch + 1], 0.0f));
        }
    }
}

#if NETH_LEARN_S1F
/* the stem inside conv1's forward (NETH_LEARN_S1F): a block of 16 warps spb samples, each sample's bytes to h0 in shared
 * memory (s1_stem), then conv1 = A W1^T straight from it (a warp an output row: M its 16 positions, K (cell, ci) a
 * cell a k-step, N co; W1 [co][(cell, ci)] in shared memory, rows padded to S1F_WL), its output as conv_o's: h1 =
 * relu(x1 + b1) from x1's rounding into conv2's padded map. DRAM reads the bytes and writes h1, where k_stem_mma and
 * conv1's k_igemm wrote h0 and read it back; the next sample's bytes load during this one's GEMM */
#define S1F_WL (K1 + 8)
#define S1F_SMEM (2 * SOB + 2 * S1B_HN + 2 * NETH_NET_C1 * S1F_WL)
static_assert(!X1_KEPT, "k_s1f writes h1 only");
__global__ void __launch_bounds__(S1B_W * 32, 1) k_s1f(int b, int spb, const int32_t *idx, const uint8_t *obs,
                                                       const bf *w0, const float *b0, const bf *__restrict__ w1,
                                                       const float *b1, bf *h1) {
    extern __shared__ __align__(16) unsigned char s1sm[];
    uint8_t *so0 = s1sm;
    bf *h = (bf *)(s1sm + 2 * SOB), *sw = (bf *)(s1sm + 2 * SOB + 2 * S1B_HN);
    int t = threadIdx.x, lane = t & 31, w = t >> 5, g = lane >> 2, q = lane & 3;
    for (int i = t; i < 2 * S1B_HN / 16; i += blockDim.x) ((uint4 *)h)[i] = make_uint4(0, 0, 0, 0);
    for (int i = t; i < NETH_NET_C1 * K1 / 8; i += blockDim.x)
        *(uint4 *)&sw[(i / (K1 / 8)) * S1F_WL + i % (K1 / 8) * 8] = *(const uint4 *)(w1 + i * 8);
    uint32_t B0[NETH_NET_C][2][2];
    s1_w0(w0, B0);
    float bb[4][2];
#pragma unroll
    for (int nt = 0; nt < 4; nt++) bb[nt][0] = b1[nt * 8 + 2 * q], bb[nt][1] = b1[nt * 8 + 2 * q + 1];
    const long n0 = (long)blockIdx.x * spb, ne = n0 + spb < b ? n0 + spb : b;
    auto ld_obs = [&](long n, uint8_t *so) {
        if (n < ne)
            for (int i = t; i < SOB / 16; i += blockDim.x)
                cp16(so + i * 16, obs + (idx ? (long)idx[n] : n) * SOB + i * 16, true);
        cp_commit();
    };
    ld_obs(n0, so0);
    ld_obs(n0 + 1, so0 + SOB);
    const int oy = w;
    for (long n = n0, it = 0; n < ne; n++, it++) {
        cp_wait<1>();
        __syncthreads();
        s1_stem(so0 + (it & 1) * SOB, B0, b0, h, w, S1B_W);
        __syncthreads();
        ld_obs(n + 2, so0 + (it & 1) * SOB);
        acc4 c[4] = {};
#pragma unroll 4
        for (int cell = 0; cell < 16; cell++) {
            int ky = cell >> 2, kx = cell & 3;
            uint32_t a[4], bw[2][4];
            ldm4(a, &h[s1h(2 * oy + ky, 2 * (lane & 15) + kx, (lane >> 4) * 8)]);
#pragma unroll
            for (int np = 0; np < 2; np++)
                ldm4(bw[np], &sw[((lane & 7) + (lane >> 4) * 8 + np * 16) * S1F_WL + cell * 16 + ((lane >> 3) & 1) * 8]);
#pragma unroll
            for (int nt = 0; nt < 4; nt++) mma16816(c[nt], a, &bw[nt >> 1][(nt & 1) * 2]);
            acc_step(c, cell);
        }
        acc_fin(c);
        /* rows g, g + 8 (ox), columns nt * 8 + 2q (+1) (co): h1 at (oy + 1, ox + 1) of conv2's padded map */
        bf *ha = h1 + (((long)n * (NETH_NET_O1 + 2) + oy + 1) * (NETH_NET_O1 + 2) + g + 1) * NETH_NET_C1, *hb = ha + 8 * NETH_NET_C1;
#pragma unroll
        for (int nt = 0; nt < 4; nt++) {
            int co = nt * 8 + 2 * q;
            uint32_t va = pack2(c[nt][0], c[nt][1]), vb = pack2(c[nt][2], c[nt][3]);
            float2 fa = h22f2(*(const nt162 *)&va), fb = h22f2(*(const nt162 *)&vb);
            *(uint32_t *)(ha + co) = pack2(fmaxf(fa.x + bb[nt][0], 0.0f), fmaxf(fa.y + bb[nt][1], 0.0f));
            *(uint32_t *)(hb + co) = pack2(fmaxf(fb.x + bb[nt][0], 0.0f), fmaxf(fb.y + bb[nt][1], 0.0f));
        }
        __syncthreads();
    }
    cp_wait<0>();
}
#endif /* NETH_LEARN_S1F */

#if NETH_LEARN_S1B

__global__ void __launch_bounds__(S1B_W * 32, 1) k_s1b(int b, int spb, const int32_t *idx, const uint8_t *obs,
                                                       const bf *w0, const float *b0, const bf *__restrict__ dx1,
                                                       const bf *__restrict__ w1x, float *part) {
    extern __shared__ __align__(16) unsigned char s1sm[];
    uint8_t *so0 = s1sm;
    bf *h = (bf *)(s1sm + 2 * SOB);
    bf(*P)[NETH_NET_O1 + 2][C1X_PD] = (bf(*)[NETH_NET_O1 + 2][C1X_PD])(s1sm + 2 * SOB + 2 * S1B_HN);
    constexpr int K4 = 4 * NETH_NET_C1;
    int t = threadIdx.x, lane = t & 31, w = t >> 5, g = lane >> 2, q = lane & 3;
    for (int i = t; i < S1B_SMEM / 16; i += blockDim.x) ((uint4 *)s1sm)[i] = make_uint4(0, 0, 0, 0);
    uint32_t B0[NETH_NET_C][2][2];    /* the stem's weights, as k_stem_mma's */
    s1_w0(w0, B0);
    __syncthreads();
    const long n0 = (long)blockIdx.x * spb, ne = n0 + spb < b ? n0 + spb : b;  /* the block's samples */
    auto ld_obs = [&](long n, uint8_t *so) {
        if (n < ne)
            for (int i = t; i < SOB / 16; i += blockDim.x)
                cp16(so + i * 16, obs + (idx ? (long)idx[n] : n) * SOB + i * 16, true);
        cp_commit();
    };
    auto ld_dx1 = [&](long n) {
        if (n < ne)
            for (int i = t; i < NETH_NET_O1 * NETH_NET_O1 * (NETH_NET_C1 / 8); i += blockDim.x) {
                int p = i / (NETH_NET_C1 / 8), c8 = i % (NETH_NET_C1 / 8) * 8;
                cp16(&P[p / NETH_NET_O1 + 1][p % NETH_NET_O1 + 1][c8], dx1 + ((long)n * P1 + p) * NETH_NET_C1 + c8, true);
            }
        cp_commit();
    };
    auto stem = [&](const uint8_t *so) { s1_stem(so, B0, b0, h, w, S1B_W); };
    acc4 acc[2][4] = {};        /* conv1's weight gradient: co i * 16 + g (+ 8), columns cg * 32 + j * 8 + 2q (+1) */
    acc4 sdw[7] = {};           /* the stem's: co g (+ 8), columns nt * 8 + 2q (+1) */
    int ks = 0, ks2 = 0;        /* their k-steps (acc_step) */
    float cb = 0.0f;            /* conv1's bias: co t % 32 over rows t / 32 + 16 k */
    const int cg = w & 7, kh = w >> 3, z = w & 3, qy = z >> 1, qx = z & 1;
    const bf *wz = w1x + (long)z * NETH_NET_C0 * K4;
    long n = n0;
    ld_obs(n, so0);
    ld_dx1(n);
    ld_obs(n + 1, so0 + SOB);
    cp_wait<2>();
    __syncthreads();
    if (n < ne) stem(so0);
    cp_wait<1>();
    __syncthreads();
    for (int it = 0; n < ne; n++, it++) {
        const uint8_t *so = so0 + (it & 1) * SOB;
        for (int r = w; r < P1; r += S1B_W) cb += h2f(P[r / NETH_NET_O1 + 1][r % NETH_NET_O1 + 1][lane]);
        /* conv1's weight gradient: K the positions, a k-step an output row */
#pragma unroll 1
        for (int oy = kh; oy < NETH_NET_O1; oy += 2) {
            uint32_t af[2][4], bfr[2][4];
#pragma unroll
            for (int i = 0; i < 2; i++)
                ldm4t(af[i], &P[oy + 1][(lane & 7) + ((lane >> 4) & 1) * 8 + 1][i * 16 + ((lane >> 3) & 1) * 8]);
#pragma unroll
            for (int j = 0; j < 2; j++) {
                int cell = cg * 2 + j, ky = cell >> 2, kx = cell & 3;
                ldm4t(bfr[j], &h[s1h(2 * oy + ky, 2 * (lane & 15) + kx, (lane >> 4) * 8)]);
            }
#pragma unroll
            for (int i = 0; i < 2; i++)
#pragma unroll
                for (int j = 0; j < 4; j++) mma16816w(acc[i][j], af[i], &bfr[j / 2][(j & 1) * 2]);
            acc_step(acc, ks++);
        }
        /* conv1's input gradient (transposed), relu' (h0 > 0), and the stem's weight gradient */
#pragma unroll 1
        for (int j = w >> 2; j < NETH_NET_O1; j += S1B_W / 4) {
            acc4 c[2] = {};
#pragma unroll
            for (int s = 0; s < 4; s++)
#pragma unroll
                for (int kk = 0; kk < NETH_NET_C1; kk += 16) {
                    const bf *wr = wz + g * K4 + s * NETH_NET_C1 + kk + 2 * q;
                    uint32_t A[4] = {__ldg((const unsigned *)wr), __ldg((const unsigned *)(wr + 8 * K4)),
                                     __ldg((const unsigned *)(wr + 8)), __ldg((const unsigned *)(wr + 8 * K4 + 8))};
                    uint32_t bb[4];
                    ldm4(bb, &P[j + 1 + qy - (s >> 1)][(lane & 7) + ((lane >> 4) & 1) * 8 + qx - (s & 1) + 1]
                               [kk + ((lane >> 3) & 1) * 8]);
                    mma16816(c[0], A, bb);
                    mma16816(c[1], A, bb + 2);
                    acc_step(c, s * (NETH_NET_C1 / 16) + kk / 16);
                }
            acc_fin(c);
            int Y = 2 * j + qy;
            uint32_t A2[4];
#pragma unroll
            for (int nt = 0; nt < 2; nt++)
#pragma unroll
                for (int hr = 0; hr < 2; hr++) {
                    int ci = g + hr * 8, X = 2 * (nt * 8 + 2 * q) + qx;
                    float v0 = s1hv(h, Y + 1, X + 1, ci) > 0.0f ? c[nt][2 * hr] : 0.0f;
                    float v1 = s1hv(h, Y + 1, X + 3, ci) > 0.0f ? c[nt][2 * hr + 1] : 0.0f;
                    A2[nt * 2 + hr] = pack2(v0, v1);
                }
            int pr = Y * OS + qx;   /* the tile's pixel k at position pr + 2k */
#pragma unroll
            for (int nt = 0; nt < 6; nt++) {
                int kc = nt * 8 + g, ci = kc >> 4, ky = (kc >> 2) & 3, kx = kc & 3;
                uint32_t B2[2] = {pack2(px0(so, pr + 4 * q, ci, ky, kx), px0(so, pr + 4 * q + 2, ci, ky, kx)),
                                  pack2(px0(so, pr + 4 * q + 16, ci, ky, kx), px0(so, pr + 4 * q + 18, ci, ky, kx))};
                mma16816w(sdw[nt], A2, B2);
            }
            uint32_t B6[2];
            B6[0] = B6[1] = g == 0 ? pack2(1.0f, 1.0f) : 0u;
            mma16816w(sdw[6], A2, B6);
            acc_step(sdw, ks2++);
        }
        __syncthreads();
        ld_dx1(n + 1);
        ld_obs(n + 2, so0 + (it & 1) * SOB);
        cp_wait<2>();
        __syncthreads();
        if (n + 1 < ne) stem(so0 + ((it + 1) & 1) * SOB);
        cp_wait<1>();
        __syncthreads();
    }
    cp_wait<0>();
    __syncthreads();
    acc_fin(acc);
    acc_fin(sdw);
    /* the reductions: conv1's weight gradient (the two row parities), its bias, the stem's (16 warps), in order */
    float *red = (float *)s1sm, *o = part + (long)blockIdx.x * S1B_PART;
#pragma unroll
    for (int i = 0; i < 2; i++)
#pragma unroll
        for (int j = 0; j < 4; j++) {
            float *r = red + (long)kh * NETH_NET_C1 * K1 + (i * 16 + g) * K1 + cg * 32 + j * 8 + 2 * q;
            r[0] = acc[i][j][0], r[1] = acc[i][j][1], r[8 * K1] = acc[i][j][2], r[8 * K1 + 1] = acc[i][j][3];
        }
    __syncthreads();
    for (int i = t; i < NETH_NET_C1 * K1; i += blockDim.x) o[i] = red[i] + red[NETH_NET_C1 * K1 + i];
    __syncthreads();
    red[t] = cb;
#pragma unroll
    for (int nt = 0; nt < 7; nt++) {
        float *r = red + 512 + w * NETH_NET_C0 * 56 + g * 56 + nt * 8 + 2 * q;
        r[0] = sdw[nt][0], r[1] = sdw[nt][1], r[8 * 56] = sdw[nt][2], r[8 * 56 + 1] = sdw[nt][3];
    }
    __syncthreads();
    if (t < NETH_NET_C1) {
        float s = 0.0f;
        for (int k = 0; k < S1B_W; k++) s += red[k * 32 + t];
        o[NETH_NET_C1 * K1 + t] = s;
    }
    for (int i = t; i < NETH_NET_C0 * SW; i += blockDim.x) {
        int co = i / SW, kc = i % SW;
        float s = 0.0f;
        for (int k = 0; k < S1B_W; k++) s += red[512 + k * NETH_NET_C0 * 56 + co * 56 + kc];
        o[NETH_NET_C1 * K1 + NETH_NET_C1 + i] = s;
    }
}

/* the blocks' sums in block order: conv1's weight gradient ((ky, kx, ci) order, gw's), its bias, the stem's */
__global__ void k_s1bsum(int nblk, const float *part, float *gw1, float *gb1, float *gws, float *gbs) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= S1B_PART) return;
    float s = 0.0f;
    for (int k = 0; k < nblk; k++) s += part[(long)k * S1B_PART + i];
    if (i < NETH_NET_C1 * K1)
        gw1[i] = s;
    else if ((i -= NETH_NET_C1 * K1) < NETH_NET_C1)
        gb1[i] = s;
    else {
        i -= NETH_NET_C1;
        int co = i / SW, kc = i % SW;
        if (kc == K0)
            gbs[co] = s;
        else
            gws[co * K0 + kc] = s;
    }
}
#endif /* NETH_LEARN_S1B */
#endif /* NETH_LEARN_S1B || NETH_LEARN_S1F */

#endif /* NETH_NET_STEM */

/* a 2-stride, 1-padded convolution's im2col over the layer before's GEMM output y [n][H][H][C] (channels last, its
 * bias not yet added): x = relu(y + bias), columns (ky, kx, ci): a[n * Ho * Ho + oy * Ho + ox][(ky * KK + kx) * C +
 * ci], a thread VN channels of one cell (the padding 0, as relu's output is padded) */
template <int H, int C, int KK, class T>
__global__ void k_im2col(int b, const T *y, const float *bias, T *a) {
    constexpr int Ho = H / 2, K = C * KK * KK, VN = vec<T>::n;
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= (long)b * Ho * Ho * K / VN) return;
    int k0 = (t % (K / VN)) * VN;
    long r = t / (K / VN);
    int n = r / (Ho * Ho), p = r % (Ho * Ho), cell = k0 / C, ci = k0 % C;
    int iy = 2 * (p / Ho) - 1 + cell / KK, ix = 2 * (p % Ho) - 1 + cell % KK;
    float f[VN];
    if (iy >= 0 && iy < H && ix >= 0 && ix < H) {
        vld(y + ((long)(n * H + iy) * H + ix) * C + ci, f);
#pragma unroll
        for (int j = 0; j < VN; j += 4) {
            float4 bb = __ldg((const float4 *)(bias + ci + j));
            f[j] = fmaxf(f[j] + bb.x, 0.0f), f[j + 1] = fmaxf(f[j + 1] + bb.y, 0.0f);
            f[j + 2] = fmaxf(f[j + 2] + bb.z, 0.0f), f[j + 3] = fmaxf(f[j + 3] + bb.w, 0.0f);
        }
    } else
#pragma unroll
        for (int j = 0; j < VN; j++) f[j] = 0.0f;
    vst(a + r * K + k0, f);
}

/* its backward: dx[n][iy][ix][ci] = the sum of da's terms that read it, times relu' (y + bias > 0), VN channels a
 * thread */
template <int H, int C, int KK, class T>
__global__ void k_col2im(int b, const T *da, const T *x, const float *bias, T *dx) {
    constexpr int Ho = H / 2, K = C * KK * KK, VN = vec<T>::n;
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= (long)b * H * H * C / VN) return;
    int ci = (t % (C / VN)) * VN;
    long r = t / (C / VN);
    int n = r / (H * H), iy = (r / H) % H, ix = r % H;
    float s[VN] = {}, f[VN];
    for (int ky = (iy + 1) & 1; ky < KK; ky += 2) {
        int oy = (iy + 1 - ky) / 2;
        if (oy < 0 || oy >= Ho) continue;
        for (int kx = (ix + 1) & 1; kx < KK; kx += 2) {
            int ox = (ix + 1 - kx) / 2;
            if (ox < 0 || ox >= Ho) continue;
            vld(da + (long)(n * Ho * Ho + oy * Ho + ox) * K + (ky * KK + kx) * C + ci, f);
#pragma unroll
            for (int j = 0; j < VN; j++) s[j] += f[j];
        }
    }
    vld(x + r * C + ci, f);
#pragma unroll
    for (int j = 0; j < VN; j += 4) {
        float4 bb = __ldg((const float4 *)(bias + ci + j));
        s[j] = f[j] + bb.x > 0.0f ? s[j] : 0.0f, s[j + 1] = f[j + 1] + bb.y > 0.0f ? s[j + 1] : 0.0f;
        s[j + 2] = f[j + 2] + bb.z > 0.0f ? s[j + 2] : 0.0f, s[j + 3] = f[j + 3] + bb.w > 0.0f ? s[j + 3] : 0.0f;
    }
    vst(dx + r * C + ci, s);
}

/* y[r][c] += bias[c] for c < C (row stride ld), then relu */
template <class T>
__global__ void k_bias_relu(long rows, int C, int ld, T *y, const float *bias, int relu) {
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= rows * C) return;
    long r = t / C;
    int c = t % C;
    float v = ldf(y[r * ld + c]) + bias[c];
    y[r * ld + c] = cvt<T>(relu ? fmaxf(v, 0.0f) : v);
}

/* the same on a dense [rows][C] (C a multiple of VN), VN a thread */
template <class T>
__global__ void k_bias_relu_v(long rows, int C, T *y, const float *bias) {
    constexpr int VN = vec<T>::n;
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= rows * C / VN) return;
    int c0 = (t % (C / VN)) * VN;
    float f[VN];
    vld(y + t * VN, f);
#pragma unroll
    for (int j = 0; j < VN; j++) f[j] = fmaxf(f[j] + bias[c0 + j], 0.0f);
    vst(y + t * VN, f);
}

template <class T>
__global__ void k_relu_bwd(long n, T *d, const T *y) {
    constexpr int VN = vec<T>::n;
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= n / VN) return;
    float f[VN], g[VN];
    vld(y + t * VN, f);
    vld(d + t * VN, g);
#pragma unroll
    for (int j = 0; j < VN; j++) g[j] = f[j] > 0.0f ? g[j] : 0.0f;
    vst(d + t * VN, g);
}

/* z0[n] = relu(y3 + bias) channel-major (c * 4 + p), then the scalars, then zeros to the row's Z0LD */
template <class T>
__global__ void k_flat(int b, const int32_t *idx, const T *y3, const float *bias, const neth_sc_t *sc, T *z0) {
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= (long)b * Z0LD) return;
    int n = t / Z0LD, k = t % Z0LD;
    if (k < NETH_NET_C3 * P3)
        z0[t] = cvt<T>(fmaxf(ldf(y3[(n * P3 + k % P3) * NETH_NET_C3 + k / P3]) + bias[k / P3], 0.0f));
    else if (k < NETH_NET_FLAT)
        z0[t] = cvt<T>(neth_sc_f32(sc[(long)(idx ? idx[n] : n) * NETH_NET_NS + k - NETH_NET_C3 * P3]));
    else
        z0[t] = cvt<T>(0.0f);
}

/* its backward to y3's layout, times relu' (y3 + bias > 0) */
template <class T>
__global__ void k_unflat(int b, const T *dz0, const T *y3, const float *bias, T *dx3) {
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= (long)b * P3 * NETH_NET_C3) return;
    int c = t % NETH_NET_C3, r = t / NETH_NET_C3, n = r / P3, p = r % P3;
    dx3[t] = ldf(y3[t]) + bias[c] > 0.0f ? dz0[(long)n * Z0LD + c * P3 + p] : cvt<T>(0.0f);
}

template <class T>
__global__ void k_fill(long n, T *p, float v) {
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t < n) p[t] = cvt<T>(v);
}


/* the GEMMs' weights (T), one thread an element: the first layer's (the stem's, or conv1's from the bytes) as it is,
 * conv1 after the stem, conv2 and conv3 into (ky, kx, ci) order, fc1's rows padded to Z0LD (zeros), fc2, the heads'
 * [97][D] padded to HS rows (zeros): every GEMM's dimensions a multiple of 8, so cuBLAS takes its aligned kernels
 * (fc1's weight gradient at 411 columns took 151 us a step, align1) */
#define NS0 (NETH_NET_STEM ? NETH_NET_C0 * K0 : 0)  /* the stem's weights */
#define WT_S0 0
#define WT_C1 (WT_S0 + NS0)
#define WT_C2 (WT_C1 + NETH_LEARN_N_conv1_w)
#define WT_C3 (WT_C2 + NETH_LEARN_N_conv2_w)
#define WT_F1 (WT_C3 + NETH_LEARN_N_conv3_w)
#define WT_F2 (WT_F1 + NETH_NET_D * Z0LD)
#define WT_H (WT_F2 + NETH_LEARN_N_fc2_w)
#define WT_C2T (WT_H + HS * NETH_NET_D)   /* conv2's again as [cell][ci][co], k_conv2_dx_mma's B */
#define WT_C3T (WT_C2T + NETH_LEARN_N_conv2_w)     /* conv3's as [cell][ci][co], k_conv3_dx_mma's B */
#define NS1 (NETH_NET_STEM ? NETH_LEARN_N_conv1_w : 0)
#define WT_C1F (WT_C3T + NETH_LEARN_N_conv3_w)     /* conv1's after the stem as [(ky * 4 + kx) * 16 + ci][co]: its k_igemm's B */
#define WT_C1X (WT_C1F + NS1)               /* and as [qy, qx][ci][sy, sx][co]: k_c1dx's B */
#define NS2X (NETH_LEARN_IC2X ? NETH_LEARN_N_conv2_w : 0)
#define NS2 (NETH_LEARN_IC2 ? NETH_LEARN_N_conv2_w : 0)
#define WT_C2X (WT_C1X + NS1)               /* conv2's as [qy, qx][sy, sx][co][ci]: its input gradient's k_igemm's B */
#define WT_C2F (WT_C2X + NS2X)             /* and as [(ky * 4 + kx) * C1 + ci][co]: its forward's k_igemm's B */
#define WT_N (WT_C2F + NS2)
template <class T>
__global__ void k_wcvt(const float *w, T *dst) {
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= WT_N) return;
    float v;
    if (t < WT_C1) {
#if NETH_NET_STEM
        v = w[NETH_LEARN_OFF(stem_w) + t];
#else
        v = 0.0f;
#endif
    } else if (t < WT_C2) {     /* after the stem: [co][(ky * 4 + kx) * 16 + ci] from [co][ci][ky][kx] */
        long i = t - WT_C1;
        int co = i / K1, cell = (i % K1) / NETH_NET_CI, ci = i % NETH_NET_CI;
        v = NETH_NET_STEM ? w[NETH_LEARN_OFF(conv1_w) + co * K1 + ci * 16 + cell] : w[NETH_LEARN_OFF(conv1_w) + i];
    } else if (t < WT_C3) {   /* [co][(ky * 4 + kx) * C1 + ci] from [co][ci][ky][kx] */
        long i = t - WT_C2;
        int co = i / K2, cell = (i % K2) / NETH_NET_C1, ci = i % NETH_NET_C1;
        v = w[NETH_LEARN_OFF(conv2_w) + (long)co * K2 + ci * 16 + cell];
    } else if (t < WT_F1) {
        long i = t - WT_C3;
        int co = i / K3, cell = (i % K3) / NETH_NET_C2, ci = i % NETH_NET_C2;
        v = w[NETH_LEARN_OFF(conv3_w) + (long)co * K3 + ci * 9 + cell];
    } else if (t < WT_F2) {
        long i = t - WT_F1;
        int r = i / Z0LD, c = i % Z0LD;
        v = c < NETH_NET_FLAT ? w[NETH_LEARN_OFF(fc1_w) + (long)r * NETH_NET_FLAT + c] : 0.0f;
    } else if (t < WT_H)
        v = w[NETH_LEARN_OFF(fc2_w) + t - WT_F2];
    else if (t < WT_C2T)
        v = t - WT_H < NETH_LEARN_HEADS * NETH_NET_D ? w[NETH_LEARN_OFF(cam_w) + t - WT_H] : 0.0f;     /* rows 97..103 zero */
    else if (t < WT_C3T) {
        long i = t - WT_C2T;
        int cell = i / (NETH_NET_C1 * NETH_NET_C2), ci = (i / NETH_NET_C2) % NETH_NET_C1, co = i % NETH_NET_C2;
        v = w[NETH_LEARN_OFF(conv2_w) + (long)co * K2 + ci * 16 + cell];
    } else if (t < WT_C1F) {
        long i = t - WT_C3T;
        int cell = i / (NETH_NET_C2 * NETH_NET_C3), ci = (i / NETH_NET_C3) % NETH_NET_C2, co = i % NETH_NET_C3;
        v = w[NETH_LEARN_OFF(conv3_w) + (long)co * K3 + ci * 9 + cell];
    } else if (t < WT_C1X) {    /* (NETH_NET_STEM) [co][ci][ky][kx] as [(ky * 4 + kx) * 16 + ci][co] */
        long i = t - WT_C1F;
        int k = i / NETH_NET_C1, co = i % NETH_NET_C1;
        v = w[NETH_LEARN_OFF(conv1_w) + co * K1 + (k % NETH_NET_CI) * 16 + k / NETH_NET_CI];
    } else if (t >= WT_C2F) {   /* (NETH_LEARN_IC2) conv2's as [(ky * 4 + kx) * C1 + ci][co] */
        long i = t - WT_C2F;
        int k = i / NETH_NET_C2, co = i % NETH_NET_C2;
        v = w[NETH_LEARN_OFF(conv2_w) + (long)co * K2 + (k % NETH_NET_C1) * 16 + k / NETH_NET_C1];
    } else if (t >= WT_C2X) {   /* (NETH_LEARN_IC2X) conv2's as [q][s][co][ci]: ky = 1 - qy + 2 sy, kx = 1 - qx + 2 sx */
        long i = t - WT_C2X;
        int q = i / (4 * NETH_NET_C2 * NETH_NET_C1), s = i / (NETH_NET_C2 * NETH_NET_C1) % 4, co = i / NETH_NET_C1 % NETH_NET_C2, ci = i % NETH_NET_C1;
        int ky = 1 - (q >> 1) + 2 * (s >> 1), kx = 1 - (q & 1) + 2 * (s & 1);
        v = w[NETH_LEARN_OFF(conv2_w) + (long)co * K2 + ci * 16 + ky * 4 + kx];
    } else {                    /* (NETH_NET_STEM) as [q][ci][s][co]: ky = 1 - qy + 2 sy, kx = 1 - qx + 2 sx */
        long i = t - WT_C1X;
        int q = i / (4 * NETH_NET_C1 * NETH_NET_CI), ci = i / (4 * NETH_NET_C1) % NETH_NET_CI, s = i / NETH_NET_C1 % 4, co = i % NETH_NET_C1;
        int ky = 1 - (q >> 1) + 2 * (s >> 1), kx = 1 - (q & 1) + 2 * (s & 1);
        v = w[NETH_LEARN_OFF(conv1_w) + co * K1 + ci * 16 + ky * 4 + kx];
    }
    dst[t] = cvt<T>(v);
}

/* conv1's (after the stem), conv2's and conv3's weight gradients from (ky, kx, ci) order (gw: conv1's [C1][K1], conv2's
 * [C2][K2], conv3's [C3][K3], then fc1's [D][Z0LD]) back into the layout's [co][ci][ky][kx], and fc1's without its
 * padding */
#define NG1 (NETH_NET_STEM ? NETH_LEARN_N_conv1_w : 0)
#define GW_C1 0
#define GW_C2 (GW_C1 + NG1)
#define GW_C3 (GW_C2 + NETH_LEARN_N_conv2_w)
#define GW_F1 (GW_C3 + NETH_LEARN_N_conv3_w)
#define GW_N (GW_F1 + NETH_NET_D * Z0LD)
__global__ void k_gunperm(const float *gw, float *grad, int zp = 0) {   /* zp: fc1's columns channels last (fc.cuh) */
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t < NG1) {
        int co = t / K1, ci = (t % K1) / 16, cell = t % 16;
        grad[NETH_LEARN_OFF(conv1_w) + t] = gw[GW_C1 + co * K1 + cell * NETH_NET_CI + ci];
    } else if ((t -= NG1) < NETH_LEARN_N_conv2_w) {
        int co = t / K2, ci = (t % K2) / 16, cell = t % 16;
        grad[NETH_LEARN_OFF(conv2_w) + t] = gw[GW_C2 + (long)co * K2 + cell * NETH_NET_C1 + ci];
    } else if ((t -= NETH_LEARN_N_conv2_w) < NETH_LEARN_N_conv3_w) {
        int co = t / K3, ci = (t % K3) / 9, cell = t % 9;
        grad[NETH_LEARN_OFF(conv3_w) + t] = gw[GW_C3 + (long)co * K3 + cell * NETH_NET_C2 + ci];
    } else if ((t -= NETH_LEARN_N_conv3_w) < NETH_LEARN_NFC1) {
        int k = t % NETH_NET_FLAT;
        if (zp && k < NETH_NET_C3 * P3) k = (k % P3) * NETH_NET_C3 + k / P3;
        grad[NETH_LEARN_OFF(fc1_w) + t] = gw[GW_F1 + (t / NETH_NET_FLAT) * Z0LD + k];
    }
}

/* ---- the loss ---- */

/* the masked policy steps of the minibatch: nm = max(sum mask[idx], 1) (one block) */
__global__ void k_nm(int b, const int32_t *idx, const float *mask, float *nm) {
    __shared__ float sh[1024];
    float s = 0.0f;
    for (int i = threadIdx.x; i < b; i += blockDim.x) s += mask[idx ? idx[i] : i];
    sh[threadIdx.x] = s;
    __syncthreads();
    for (int w = blockDim.x / 2; w > 0; w >>= 1) {
        if (threadIdx.x < w) sh[threadIdx.x] += sh[threadIdx.x + w];
        __syncthreads();
    }
    if (threadIdx.x == 0) *nm = fmaxf(sh[0], 1.0f);
}

#define NB 6    /* the loss kernel's partial sums: pg, (v - ret)^2, entropy, kl, clipped, ref_kl (lane/ntppo) */
#define LW 16   /* its samples (warps) a block */

__device__ __forceinline__ float wsum(float v) {
#pragma unroll
    for (int o = 16; o; o >>= 1) v += __shfl_xor_sync(0xffffffffu, v, o);
    return v;
}
__device__ __forceinline__ float wmax(float v) {
#pragma unroll
    for (int o = 16; o; o >>= 1) v = fmaxf(v, __shfl_xor_sync(0xffffffffu, v, o));
    return v;
}

/* one warp a sample (a lane bins j and j + 32 of each camera head, key j): the heads' outputs hd[n][HS] in, d loss /
 * d outputs out (in place; and in bf16 to dhb when it is not NULL); fastppo's _part: lp = S.logp, ratio = exp(lp -
 * lp0), pg = -sum(min(ratio adv, clamp(ratio) adv) m) / nm, vl = 0.5 sum((v - ret)^2) / ns, ent = sum(S.entropy m) /
 * nm, loss = pg + vf vl - entc ent; each gradient in torch's own order of terms. lane/ntppo: rh (NULL none) the
 * reference's logits by segment sample [.][NETH_LEARN_RSTRIDE], loss += rkc sum(policy_kl m) / nm (fastppo's policy_kl: the
 * camera heads' KL(pi || ref) of their log_softmax, the keys' Bernoulli KL of their logsigmoid); pol 0 (the value
 * warm-up) the policy's terms give no gradient (the books as always) */
template <class RT>
__global__ void k_loss(int b, const int32_t *idx, float *hd, bf *dhb, const int32_t *cam, const float *keys,
                       const int32_t *hot, const float *logp0, const float *adv, const float *ret, const float *mask,
                       const float *nmp, float clip, float vf, float entc, float *part, const RT *rh, float rkc,
                       int pol, const float *val0 = nullptr, float vclip = 0.0f) {
    __shared__ float sh[NB][LW];
    int lane = threadIdx.x & 31, wib = threadIdx.x >> 5, n = blockIdx.x * LW + wib;
    float acc[NB] = {};
    if (n < b) {
        long s = idx ? idx[n] : n;
        float *h = hd + (long)n * HS;
        float nm = *nmp, mj = mask[s], A = adv[s];
        int hi = lane + 32 < NETH_NET_BINS;
        float la[NETH_NET_CAM], lb[NETH_NET_CAM], ra[NETH_NET_CAM] = {}, rb[NETH_NET_CAM] = {};
        float lp = 0.0f, E = 0.0f, KL = 0.0f;
        const RT *r = rh ? rh + s * NETH_LEARN_RSTRIDE : NULL;
#pragma unroll
        for (int c = 0; c < NETH_NET_CAM; c++) {
            const float *l = h + c * NETH_NET_BINS;
            float va = l[lane], vb = hi ? l[lane + 32] : -INFINITY;
            float mx = wmax(fmaxf(va, vb));
            float se = wsum(expf(va - mx) + (hi ? expf(vb - mx) : 0.0f));
            float lse = logf(se);
            la[c] = (va - mx) - lse;
            lb[c] = hi ? (vb - mx) - lse : 0.0f;
            float e = wsum(expf(la[c]) * la[c] + (hi ? expf(lb[c]) * lb[c] : 0.0f));
            int k = cam[s * NETH_NET_CAM + c];
            lp += __shfl_sync(0xffffffffu, k < 32 ? la[c] : lb[c], k & 31);
            E += -e;
            if (r) {    /* the reference's log_softmax, then sum p (lc - rc) */
                const RT *q = r + c * NETH_NET_BINS;
                float qa = ldf(q[lane]), qb = hi ? ldf(q[lane + 32]) : -INFINITY;
                float qm = wmax(fmaxf(qa, qb));
                float ql = logf(wsum(expf(qa - qm) + (hi ? expf(qb - qm) : 0.0f)));
                ra[c] = (qa - qm) - ql;
                rb[c] = hi ? (qb - qm) - ql : 0.0f;
                KL += wsum(expf(la[c]) * (la[c] - ra[c]) + (hi ? expf(lb[c]) * (lb[c] - rb[c]) : 0.0f));
            }
        }
        int kl = lane < NETH_NET_KEYS;
        float x = kl ? h[NETH_NET_CAM * NETH_NET_BINS + lane] : 0.0f, kj = kl ? keys[s * NETH_NET_KEYS + lane] : 0.0f;
        /* logsigmoid(x) = min(x, 0) - log1p(exp(-|x|)) */
        float lsp = fminf(x, 0.0f) - log1pf(expf(-fabsf(x))), lsn = fminf(-x, 0.0f) - log1pf(expf(-fabsf(x)));
        float p = 1.0f / (1.0f + expf(-x));
        lp += wsum(kl ? kj * lsp + (1.0f - kj) * lsn : 0.0f);
        E += -wsum(kl ? p * logf(p + 1e-8f) + (1.0f - p) * logf(1.0f - p + 1e-8f) : 0.0f);
        float rx = 0.0f;
        if (r) {
            rx = kl ? ldf(r[NETH_NET_CAM * NETH_NET_BINS + lane]) : 0.0f;
            float qp = fminf(rx, 0.0f) - log1pf(expf(-fabsf(rx))), qn = fminf(-rx, 0.0f) - log1pf(expf(-fabsf(rx)));
            KL += wsum(kl ? p * (lsp - qp) + (1.0f - p) * (lsn - qn) : 0.0f);
        }
#if NETH_NET_HOTBAR
        /* the hotbar: a categorical of NETH_NET_HOT over lanes 0 .. 9, after the keys in logp and the entropy */
        int hl = lane < NETH_NET_HOT, kh = hot[s];
        kh = kh < 0 ? 0 : kh >= NETH_NET_HOT ? NETH_NET_HOT - 1 : kh;
        float xh = hl ? h[NETH_LEARN_HOFF + lane] : -INFINITY, mh = wmax(xh);
        float lseh = logf(wsum(hl ? expf(xh - mh) : 0.0f));     /* every lane in the shuffles */
        float lh = hl ? (xh - mh) - lseh : 0.0f, ph = hl ? expf(lh) : 0.0f;
        lp += __shfl_sync(0xffffffffu, lh, kh);
        E += -wsum(ph * lh);   /* (--ref-kl anchors the camera and the keys; the hotbar head is not in the KL) */
#endif
        float lp0 = logp0[s], ratio = expf(lp - lp0);
        float rc = fminf(fmaxf(ratio, 1.0f - clip), 1.0f + clip);
        float s1 = ratio * A, s2 = rc * A;
        float v = h[NETH_LEARN_HEADS - 1], dv = v - ret[s];
        acc[0] = -fminf(s1, s2) * mj;
        acc[1] = dv * dv;
        if (val0) {     /* lane/acrl: hyper vclip, PPO2's clipped value term (the larger loss, its gradient) */
            float v0 = val0[s], dc = fminf(fmaxf(v - v0, -vclip), vclip), ec = v0 + dc - ret[s];
            if (ec * ec > dv * dv) {
                acc[1] = ec * ec;
                dv = fabsf(v - v0) < vclip ? ec : 0.0f;
            }
        }
        acc[2] = E * mj;
        acc[3] = (lp0 - lp) * mj;
        acc[4] = fabsf(ratio - 1.0f) > clip ? mj : 0.0f;
        acc[5] = KL * mj;
        /* d loss: through min (a tie splits), clamp (inside the range), exp */
        float gm = -mj / nm;
        float w1 = s1 < s2 ? 1.0f : s1 == s2 ? 0.5f : 0.0f, w2 = 1.0f - w1;
        float inr = (ratio >= 1.0f - clip && ratio <= 1.0f + clip) ? 1.0f : 0.0f;
        float glp = pol ? (gm * w1 * A + gm * w2 * A * inr) * ratio : 0.0f;
        float gE = pol ? -entc * mj / nm : 0.0f;
        float gK = pol && r ? rkc * mj / nm : 0.0f;     /* d loss / d KL */
        float out[4];   /* this lane's: camera head 0's bins lane and lane + 32, head 1's the same */
#pragma unroll
        for (int c = 0; c < NETH_NET_CAM; c++) {
            /* lc's gradient (the gather's, the entropy's and the KL's: d/dlc sum p (lc - rc) = p (lc - rc + 1)), then
             * log_softmax's backward: g - p sum(g) */
            int k = cam[s * NETH_NET_CAM + c];
            float pa = expf(la[c]), pb = hi ? expf(lb[c]) : 0.0f;
            float ga = gE * -(pa * la[c] + pa) + (lane == k ? glp : 0.0f);
            float gb = hi ? gE * -(pb * lb[c] + pb) + (lane + 32 == k ? glp : 0.0f) : 0.0f;
            if (r) {
                ga += gK * (pa * (la[c] - ra[c]) + pa);
                if (hi) gb += gK * (pb * (lb[c] - rb[c]) + pb);
            }
            float sg = wsum(ga + gb);
            out[2 * c] = ga - pa * sg;
            out[2 * c + 1] = gb - pb * sg;
        }
        float dE = -(logf(p + 1e-8f) + p / (p + 1e-8f)) + (logf(1.0f - p + 1e-8f) + (1.0f - p) / (1.0f - p + 1e-8f));
        float gk = glp * (kj - p) + gE * dE * p * (1.0f - p);
        if (r) gk += gK * p * (1.0f - p) * (x - rx);     /* d/dx of the Bernoulli KL: sigma'(x) (x - rx) */
#if NETH_NET_HOTBAR
        float gh = hl ? gE * -(ph * lh + ph) + (lane == kh ? glp : 0.0f) : 0.0f;    /* gE, glp 0 when !pol */
        float oh = gh - ph * wsum(gh);
#endif
        __syncwarp();
#pragma unroll
        for (int c = 0; c < NETH_NET_CAM; c++) {
            h[c * NETH_NET_BINS + lane] = out[2 * c];
            if (hi) h[c * NETH_NET_BINS + lane + 32] = out[2 * c + 1];
        }
        if (kl) h[NETH_NET_CAM * NETH_NET_BINS + lane] = gk;
#if NETH_NET_HOTBAR
        if (hl) h[NETH_LEARN_HOFF + lane] = oh;
#endif
        if (lane == 0) h[NETH_LEARN_HEADS - 1] = vf * dv / (float)b;
        if (lane < HS - NETH_LEARN_HEADS) h[NETH_LEARN_HEADS + lane] = 0.0f;
        if (dhb) {
            __syncwarp();
            bf *o = dhb + (long)n * HS;
            for (int j = lane; j < HS; j += 32) o[j] = f2h(h[j] * NETH_NET_LSCALE);    /* 1 at bf16 (prec.cuh) */
        }
    }
    if (lane == 0)
        for (int k = 0; k < NB; k++) sh[k][wib] = acc[k];
    __syncthreads();
    if (threadIdx.x < NB) {
        float t = 0.0f;
        for (int w = 0; w < LW; w++) t += sh[threadIdx.x][w];
        part[blockIdx.x * NB + threadIdx.x] = t;
    }
}

/* the books from the partials, in block order: loss pg vl ent kl clipfrac ref_kl */
#define BT 256  /* k_books' threads: each sums a fixed stride of the partials, then a tree (one thread took 37 us) */
__global__ void k_books(int nblk, int b, const float *part, const float *nmp, float vf, float entc, float *books,
                        int add, float rkc = 0.0f) {
    __shared__ float sh[NB][BT];
    float t[NB] = {};
    for (int i = threadIdx.x; i < nblk; i += BT)
        for (int k = 0; k < NB; k++) t[k] += part[i * NB + k];
    for (int k = 0; k < NB; k++) sh[k][threadIdx.x] = t[k];
    __syncthreads();
    for (int w = BT / 2; w > 0; w >>= 1) {
        if (threadIdx.x < w)
            for (int k = 0; k < NB; k++) sh[k][threadIdx.x] += sh[k][threadIdx.x + w];
        __syncthreads();
    }
    if (threadIdx.x) return;
    for (int k = 0; k < NB; k++) t[k] = sh[k][0];
    float nm = *nmp, pg = t[0] / nm, vl = 0.5f * t[1] / (float)b, ent = t[2] / nm;
    float rk = t[5] / nm, loss = pg + vf * vl - entc * ent;
    if (rkc != 0.0f) loss = loss + rkc * rk;    /* lane/ntppo: --ref-kl's term */
    float o[NETH_LEARN_NBOOK] = {loss, pg, vl, ent, t[3] / nm, t[4] / nm, rk};
    for (int k = 0; k < NETH_LEARN_NBOOK; k++) books[k] = add ? books[k] + o[k] : o[k];
}

#if NETH_LEARN_FC
#include "fc.cuh"   /* lane/ntfc: the MLP, the heads and the loss as one kernel (the 16-bit path's neth_learn_grad) */
#endif

/* ---- GAE ---- */

/* one thread an env: fastppo's _gae loop, then ret = adv + val and the mask; each operation rounded on its own (no
 * fused multiply-add), as torch's one kernel an operation rounds them */
/* GE envs a block: their columns of rew, val and done staged in shared memory by the whole block first (coalesced),
 * then one thread an env walks back over them (the walk's loads 64 dependent global reads before, 180 us) */
/* lane/audit-fixes: done[t] says the episode ended with row t's action (row t + 1, or the next segment's first row,
 * is the new episode's first observation); term[t] that the task ended it (vecenv.h terminations). Every end cuts
 * the trace. A terminated row bootstraps from zero, as before; a timed-out one (done, not term) from its own value
 * sv[i]. That is an approximation: the critic never sees the timed-out episode's true last observation (the next row
 * is already the new episode's), so the value it bootstraps from is one step stale. term = done (ntloop --trunc-boot
 * off, the fixture checks) is the learner as it was, bit for bit */
#define GE 32
__global__ void k_gae(int T, int gs, float scale, float gamma, float gl, const float *rew, const uint8_t *done,
                      const uint8_t *term, const uint8_t *drv, const float *val, const float *boot, float *adv, float *ret,
                      float *mask) {
    extern __shared__ float sg[];   /* rew [T][GE], val [T][GE], the end [T][GE]: 0 none, 1 terminated, 2 timed out */
    float *sr = sg, *sv = sg + T * GE, *sd = sg + 2 * T * GE;
    int e0 = blockIdx.x * GE;
    for (int i = threadIdx.x; i < T * GE; i += blockDim.x) {
        int t = i / GE, e = e0 + i % GE;
        if (e < gs) {
            sr[i] = rew[t * gs + e];
            sv[i] = val[t * gs + e];
            sd[i] = done[t * gs + e] ? (term[t * gs + e] ? 1.0f : 2.0f) : 0.0f;
            mask[t * gs + e] = drv[t * gs + e] == 0 ? 1.0f : 0.0f;
        }
    }
    __syncthreads();
    int el = threadIdx.x, e = e0 + el;
    if (el >= GE || e >= gs) return;
    float last = 0.0f;
    for (int t = T - 1; t >= 0; t--) {
        int i = t * GE + el;
        float nv = sd[i] == 2.0f ? sv[i] : t == T - 1 ? boot[e] : sv[i + GE];
        float nd = sd[i] == 0.0f ? 1.0f : 0.0f;     /* the trace goes on */
        float nb = sd[i] == 1.0f ? 0.0f : 1.0f;     /* the bootstrap kept */
        float delta = __fsub_rn(__fadd_rn(sr[i] / scale, __fmul_rn(__fmul_rn(gamma, nv), nb)), sv[i]);
        last = __fadd_rn(delta, __fmul_rn(__fmul_rn(gl, nd), last));
        adv[t * gs + e] = last;
        ret[t * gs + e] = __fadd_rn(last, sv[i]);
    }
}

/* the advantages normalised over the masked steps (one block): (adv - mean) / (std + 1e-8) when more than one */
__global__ void k_advnorm(int N, float *adv, const float *mask) {
    __shared__ float sa[1024], sb[1024];
    float k = 0.0f, s = 0.0f;
    for (int i = threadIdx.x; i < N; i += blockDim.x) {
        k += mask[i];
        s += __fmul_rn(adv[i], mask[i]);
    }
    sa[threadIdx.x] = k;
    sb[threadIdx.x] = s;
    __syncthreads();
    for (int w = blockDim.x / 2; w > 0; w >>= 1) {
        if (threadIdx.x < w) {
            sa[threadIdx.x] += sa[threadIdx.x + w];
            sb[threadIdx.x] += sb[threadIdx.x + w];
        }
        __syncthreads();
    }
    float K = sa[0], mean = sb[0] / fmaxf(K, 1.0f);
    __syncthreads();
    float q = 0.0f;
    for (int i = threadIdx.x; i < N; i += blockDim.x) {
        float d = adv[i] - mean;
        q += __fmul_rn(__fmul_rn(d, d), mask[i]);
    }
    sb[threadIdx.x] = q;
    __syncthreads();
    for (int w = blockDim.x / 2; w > 0; w >>= 1) {
        if (threadIdx.x < w) sb[threadIdx.x] += sb[threadIdx.x + w];
        __syncthreads();
    }
    if (!(K > 1.0f)) return;
    float sd = sqrtf(sb[0] / fmaxf(K - 1.0f, 1.0f));
    for (int i = threadIdx.x; i < N; i += blockDim.x) adv[i] = (adv[i] - mean) / (sd + 1e-8f);
}

/* explained variance of the value predictions over the segment: 1 - Var(ret - val) / Var(ret) */
__global__ void k_ev(int N, const float *val, const float *ret, float *tstat) {
    __shared__ float sr[256], se[256];
    float s_r = 0.0f, s_e = 0.0f;
    for (int i = threadIdx.x; i < N; i += 256) {
        float r = ret[i], e = r - val[i];
        s_r += r;
        s_e += e;
    }
    sr[threadIdx.x] = s_r;
    se[threadIdx.x] = s_e;
    __syncthreads();
    for (int w = 128; w > 0; w >>= 1) {
        if (threadIdx.x < w) sr[threadIdx.x] += sr[threadIdx.x + w], se[threadIdx.x] += se[threadIdx.x + w];
        __syncthreads();
    }
    float mr = sr[0] / (float)N, me = se[0] / (float)N;
    __syncthreads();
    float vr = 0.0f, ve = 0.0f;
    for (int i = threadIdx.x; i < N; i += 256) {
        float dr = ret[i] - mr, de = (ret[i] - val[i]) - me;
        vr += dr * dr;
        ve += de * de;
    }
    sr[threadIdx.x] = vr;
    se[threadIdx.x] = ve;
    __syncthreads();
    for (int w = 128; w > 0; w >>= 1) {
        if (threadIdx.x < w) sr[threadIdx.x] += sr[threadIdx.x + w], se[threadIdx.x] += se[threadIdx.x + w];
        __syncthreads();
    }
    if (threadIdx.x == 0 && tstat) {
        float ev = sr[0] > 1e-8f ? 1.0f - se[0] / sr[0] : 0.0f;
        tstat[4] += ev;
        tstat[5] += 1.0f;
    }
}

/* ---- the clip and Adam ---- */

#define AT 256
__global__ void k_sumsq(int n, const float *g, float *part) {
    __shared__ float sh[AT];
    float s = 0.0f;
    for (int i = blockIdx.x * AT + threadIdx.x; i < n; i += gridDim.x * AT) s += g[i] * g[i];
    sh[threadIdx.x] = s;
    __syncthreads();
    for (int w = AT / 2; w > 0; w >>= 1) {
        if (threadIdx.x < w) sh[threadIdx.x] += sh[threadIdx.x + w];
        __syncthreads();
    }
    if (!threadIdx.x) part[blockIdx.x] = sh[0];
}

/* every block sums the partials in the same order (the norm), then torch's fused Adam on its elements with the
 * clipped gradient: clip_coef = max_norm / (norm + 1e-6) at most 1 */
__global__ void k_adam(int n, int npart, const float *part, float *w, float *g, float *m, float *v, float step, float lr,
                       float b1, float b2, float eps, float max_norm, float *gnorm, const int *stop, float *tstat = NULL) {
    __shared__ float coef;
    if (stop && *stop) return;  /* lane/acrl: hyper kl_stop's flag, this update's epochs stopped */
    if (!threadIdx.x) {
        float s = 0.0f;
        for (int i = 0; i < npart; i++) s += part[i];
        float norm = sqrtf(s);
        coef = fminf(max_norm / (norm + 1e-6f), 1.0f);
        if (!blockIdx.x && gnorm) *gnorm = norm;
        if (!blockIdx.x && tstat) {
            if (isfinite(norm)) {
                tstat[0] += norm;
                tstat[1] = fmaxf(tstat[1], norm);
                tstat[2] += 1.0f;
            } else {
                tstat[3] += 1.0f;
            }
        }
#if NETH_NET_MMA
        if (!isfinite(norm)) coef = 0.0f / 0.0f;    /* an fp16 overflow under the loss scale: the update skipped */
#endif
    }
    __syncthreads();
#if NETH_NET_MMA
    if (isnan(coef)) return;    /* the weights and moments as they were; the gnorm book says inf or nan */
#endif
    float bc1 = 1.0f - powf(b1, step), bc2s = sqrtf(1.0f - powf(b2, step)), ss = lr / bc1;
    for (int i = blockIdx.x * AT + threadIdx.x; i < n; i += gridDim.x * AT) {
        float gi = g[i] * coef;
        g[i] = gi;
        float mi = b1 * m[i] + (1.0f - b1) * gi;
        float vi = b2 * v[i] + (1.0f - b2) * gi * gi;
        m[i] = mi;
        v[i] = vi;
        w[i] -= ss * mi / (sqrtf(vi) / bc2s + eps);
    }
}

#include "rnn.cuh"     /* lane/ntgru: the minGRU (NETH_NET_RNN), its buffers and its dispatch */
#include "critic.cuh"  /* lane/acrl: the privileged critic */

/* ---- the API ---- */

static int grid(long n, int t) { return (int)((n + t - 1) / t); }
#if NETH_LEARN_S1B || NETH_LEARN_S1F
/* k_s1b's and k_s1f's samples a block for b: S1_WAVES waves of the SMs (at most S1_WAVES x nsm blocks) */
static int s1_spb(const neth_learn *L, int b) { return b > 0 ? grid(b, S1_WAVES * L->nsm) : 1; }
#endif

__global__ void k_scale(int n, float *g, float f) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) g[i] *= f;
}

neth_learn *neth_learn_new(int nmax, int mb, int prec, void *stream) {
    neth_net_dsync();   /* lane/audit-feat: this file's kernels read the run's scalar row */
    neth_learn *L = (neth_learn *)calloc(1, sizeof *L);
    L->s = (cudaStream_t)stream;
    L->nmax = nmax;
    L->mb = mb;
    L->prec = prec;
    L->ab = prec == NETH_LEARN_BF16 ? NT16_CUDA : CUDA_R_32F;     /* NETH_LEARN_BF16 is the 16-bit path: fp16 at NETH_NET_MMA 1 to 3 */
    L->es = prec == NETH_LEARN_BF16 ? 2 : 4;
    L->ct = prec == NETH_LEARN_FP32 ? CUBLAS_COMPUTE_32F_PEDANTIC : prec == NETH_LEARN_TF32 ? CUBLAS_COMPUTE_32F_FAST_TF32
                                                                               : CUBLAS_COMPUTE_32F;
    if (cublasCreate(&L->cb) != CUBLAS_STATUS_SUCCESS) {
        fprintf(stderr, "neth_learn_new: cublasCreate failed\n");
        free(L);
        return NULL;
    }
    CB(cublasSetStream(L->cb, L->s));
    {
        int dev = 0;
        CK(cudaGetDevice(&dev));
        CK(cudaDeviceGetAttribute(&L->nsm, cudaDevAttrMultiProcessorCount, dev));
    }
#if NETH_LEARN_SPEC
    CK(cudaFuncSetAttribute(k_conv2_dx_mma, cudaFuncAttributeMaxDynamicSharedMemorySize, C2X_SMEM));
    CK(cudaFuncSetAttribute(k_conv2_mma, cudaFuncAttributeMaxDynamicSharedMemorySize, C2F_SMEM));
    CK(cudaFuncSetAttribute(k_conv3_dx_mma, cudaFuncAttributeMaxDynamicSharedMemorySize, C3X_SMEM));
    CK(cudaFuncSetAttribute(k_conv3_mma, cudaFuncAttributeMaxDynamicSharedMemorySize, C3F_SMEM));
    const long ncp = C1W_BLK * NETH_NET_C1 * CW, nc2p = (long)C2W_BLK * NETH_NET_C2 * K2, nc3p = (long)C3W_BLK * NETH_NET_C3 * K3;
#elif NETH_NET_STEM
#if NETH_LEARN_S1B
    CK(cudaFuncSetAttribute(k_s1b, cudaFuncAttributeMaxDynamicSharedMemorySize, S1B_SMEM));
#if NETH_LEARN_S1F
    CK(cudaFuncSetAttribute(k_s1f, cudaFuncAttributeMaxDynamicSharedMemorySize, S1F_SMEM));
#endif
    const long ncp = (long)S1_WAVES * L->nsm * S1B_PART, nc2p = 1, nc3p = 1;  /* cpart: k_s1b's blocks' (bf16) */
#else
    const long ncp = (long)S0W_BLK * NETH_NET_C0 * SW, nc2p = 1, nc3p = 1;   /* cpart: the stem's (bf16) */
#endif
#else
    const long ncp = 1, nc2p = 1, nc3p = 1;
#endif
    /* k_cwgrad's partials: conv1's after the stem, conv2's (not NETH_LEARN_SPEC), scaled to the device's SMs */
#if NETH_NET_STEM
    const long ncw1 = (long)cw_slices_n<NETH_NET_C1, K1, (NETH_NET_C1 < 64 ? NETH_NET_C1 : 64)>(L->nsm) * NETH_NET_C1 * K1;
#else
    const long ncw1 = 1;
#endif
#if NETH_LEARN_IC2
    const long ncw2 = (long)cw_slices_n<NETH_NET_C2, K2, (NETH_NET_C2 < 64 ? NETH_NET_C2 : 64)>(L->nsm) * NETH_NET_C2 * K2;
#else
    const long ncw2 = 1;
#endif
    /* the stem's buffers: none without it; in bf16 no im2col of the bytes (k_stem_mma) or of the stem's output (conv1's
     * implicit GEMMs over h0), and none of conv1's output but at lane/ntlearn's net (conv2's over h1) */
    long b = mb, s0 = NETH_NET_STEM, s0b = NETH_NET_STEM && prec == NETH_LEARN_BF16, i2 = NETH_LEARN_IC2 && prec == NETH_LEARN_BF16;
    struct {
        void **p;
        long n;
    } al[] = {{&L->a0, s0 * (prec != NETH_LEARN_BF16) * b * P0 * K0},{&L->x0, s0 * (prec != NETH_LEARN_BF16) * b * P0 * NETH_NET_C0},
              {&L->dx0, s0 * (prec != NETH_LEARN_BF16 || !NETH_LEARN_S1B) * b * P0 * NETH_NET_C0},     /* NETH_LEARN_S1B: none in bf16 */
              {&L->h0, s0b * !NETH_LEARN_S1F * b * (OS + 2) * (OS + 2) * NETH_NET_C0},         /* NETH_LEARN_S1F: none */ {&L->h1, i2 * b * (NETH_NET_O1 + 2) * (NETH_NET_O1 + 2) * NETH_NET_C1},
              {&L->a1, (1 - s0b) * b * P1 * K1}, {&L->x1, (s0b && !X1_KEPT ? 0 : 1) * b * P1 * NETH_NET_C1}, {&L->a2, (1 - i2) * b * P2 * K2},
              {&L->x2, b * P2 * NETH_NET_C2},
              {&L->a3, b * P3 * K3}, {&L->x3, b * P3 * NETH_NET_C3}, {&L->z0, b * Z0LD},     {&L->z1, b * NETH_NET_D},
              {&L->z2, b * NETH_NET_D},    {&L->d1, b * NETH_NET_D},       {&L->d2, b * NETH_NET_D},     {&L->dx1, b * P1 * NETH_NET_C1},
              {&L->dx2, b * P2 * NETH_NET_C2}, {&L->dx3, b * P3 * NETH_NET_C3}, {&L->dh, b * HS},   {&L->ones, b * P0},
              {&L->wt, WT_N}};
    struct {
        float **p;
        long n;
    } af[] = {{&L->hd, b * HS},     {&L->gw, GW_N}, {&L->part, (b / LW + 1 + NPART) * NB}, {&L->cpart, ncp},
              {&L->c2part, nc2p},   {&L->c3part, nc3p},              {&L->cwpart, ncw1 > ncw2 ? ncw1 : ncw2},
              {&L->nm, 1},         {&L->books, NETH_LEARN_NBOOK},                  {&L->gn, 1},
              {&L->adv, nmax},      {&L->ret, nmax},                         {&L->mask, nmax},
              {&L->tstat, 6}};
    int bad = 0;
    for (size_t i = 0; i < sizeof al / sizeof *al; i++)
        bad |= al[i].n && cudaMalloc(al[i].p, al[i].n * L->es) != cudaSuccess;
    for (size_t i = 0; i < sizeof af / sizeof *af; i++)
        bad |= cudaMalloc((void **)af[i].p, af[i].n * sizeof(float)) != cudaSuccess;
#if NETH_NET_ACC16
    if (prec == NETH_LEARN_BF16 && NETH_NET_MMA != 4) {
        L->hparts = gemm16_parts(b);
        bad |= cudaMalloc(&L->hpart, L->hparts * 2) != cudaSuccess;
    }
#endif
    bad |= NETH_NET_RNN && !(L->rnn = rnn_new(b, L->es));
#if NETH_LEARN_FC
    if (prec == NETH_LEARN_BF16) {
        bad |= cudaMalloc(&L->fcpk, PK_N * 16) != cudaSuccess;
        bad |= cudaMalloc((void **)&L->fcpart, (b / FC_BM + 1) * FC_WR * FC_BP * sizeof(float)) != cudaSuccess;
#define FC_ATTR(NP) CK(cudaFuncSetAttribute(k_fcrow<NETH_NET_FLAT0 + NP>, cudaFuncAttributeMaxDynamicSharedMemorySize, FC_SMEM));
        NETH_NET_NSP_CASES(FC_ATTR)
#undef FC_ATTR
    }
#endif
    if (bad) {
        fprintf(stderr, "neth_learn_new: cudaMalloc failed (minibatch %d)\n", mb);
        neth_learn_free(L);
        return NULL;
    }
    if (prec == NETH_LEARN_BF16)
        k_fill<bf><<<grid(b * P0, 256), 256, 0, L->s>>>(b * P0, (bf *)L->ones, 1.0f);
    else
        k_fill<float><<<grid(b * P0, 256), 256, 0, L->s>>>(b * P0, (float *)L->ones, 1.0f);
    CK(cudaMemsetAsync(L->tstat, 0, 6 * sizeof(float), L->s));
    if (L->h0)  /* the borders stay zero: k_stem_mma, conv_o and k_actpad write the inside */
        CK(cudaMemsetAsync(L->h0, 0, b * (OS + 2) * (OS + 2) * NETH_NET_C0 * L->es, L->s));
    if (L->h1)
        CK(cudaMemsetAsync(L->h1, 0, b * (NETH_NET_O1 + 2) * (NETH_NET_O1 + 2) * NETH_NET_C1 * L->es, L->s));
    CK(cudaStreamSynchronize(L->s));
    return L;
}

void neth_learn_free(neth_learn *L) {
    if (!L) return;
    void *p[] = {L->a0,  L->x0,  L->dx0, L->h0, L->h1, L->cwpart, L->a1, L->x1,  L->a2,  L->x2, L->a3,   L->x3, L->z0,    L->z1, L->z2, L->d1,
                 L->d2,  L->dx1, L->dx2, L->dx3, L->dh,  L->ones, L->wt,  L->hd, L->gw, L->part, L->cpart, L->c2part, L->c3part,
                 L->nm,  L->books, L->gn, L->adv, L->ret, L->mask, L->hpart, L->fcpk, L->fcpart, L->rhd, L->wt_ref, L->kstop, L->tstat};
    for (size_t i = 0; i < sizeof p / sizeof *p; i++) cudaFree(p[i]);
    rnn_free(L->rnn);
    cublasDestroy(L->cb);
    free(L);
}

void neth_learn_gae(neth_learn *L, const neth_learn_hyper *h, int T, int gs, float scale, const float *rew, const uint8_t *done,
             const uint8_t *term, const uint8_t *drv, const float *val, const float *boot, float *adv, float *ret,
             float *mask) {
    float gl = (float)(h->gamma * h->lam);
    k_gae<<<grid(gs, GE), 256, 3 * T * GE * sizeof(float), L->s>>>(T, gs, scale, (float)h->gamma, gl, rew, done, term,
                                                                    drv, val, boot, adv, ret, mask);
    k_advnorm<<<1, 1024, 0, L->s>>>(T * gs, adv, mask);
}

/* the weights' sub-buffers */
#define W_(w, n) ((w) + NETH_LEARN_OFF(n))

/* the GEMMs' weights, made now from the layout's */
template <class T>
static const T *weights(neth_learn *L, const float *w) {
    k_wcvt<T><<<grid(WT_N, 256), 256, 0, L->s>>>(w, (T *)L->wt);
    return (const T *)L->wt;
}

template <class T>
static void forward(neth_learn *L, const float *w, const T *wt, int b, const int32_t *idx, const uint8_t *obs,
                    const neth_sc_t *sc, bool fc = true) {  /* fc false: the convolutions only (NETH_LEARN_FC's k_fcrow next) */
    constexpr int VN = vec<T>::n;
    cudaStream_t s = L->s;
    cudaDataType_t ab = L->ab;
    T *a1 = (T *)L->a1, *x1 = (T *)L->x1, *a2 = (T *)L->a2, *x2 = (T *)L->x2, *a3 = (T *)L->a3, *x3 = (T *)L->x3;
    T *z0 = (T *)L->z0, *z1 = (T *)L->z1, *z2 = (T *)L->z2;
    /* x0 to x3 hold the convolutions' GEMM outputs before their bias: the next im2col (or flat) adds it and takes
     * relu, col2im's (or unflat's) relu' reads it the same way */
#if NETH_NET_STEM
    /* the stem from the bytes, then conv1 over its output */
    T *a0 = (T *)L->a0, *x0 = (T *)L->x0;
#if NETH_LEARN_S1F
    if constexpr (std::is_same<T, bf>::value)   /* the stem inside conv1, no h0: h1 only */
        k_s1f<<<grid(b, s1_spb(L, b)), S1B_W * 32, S1F_SMEM, s>>>(b, s1_spb(L, b), idx, obs, wt + WT_S0,
                                                                  W_(w, stem_b), wt + WT_C1, W_(w, conv1_b),
                                                                  (bf *)L->h1);
    else
#endif
    if constexpr (std::is_same<T, bf>::value) {
        k_stem_mma<<<S0F_BLK, S0F_WARPS * 32, 0, s>>>(b, idx, obs, wt + WT_S0, W_(w, stem_b), (bf *)L->h0);
        /* conv1 over h0, its output x1 and h1 = relu(x1 + b1) padded (conv2's input) */
        igemm<NETH_LEARN_T1F, K1>(conv_h<NETH_NET_O1, NETH_NET_C0>{(const bf *)L->h0}, wt + WT_C1F,
                           conv_o<NETH_NET_O1, NETH_NET_C1>{X1_KEPT ? x1 : nullptr, W_(w, conv1_b), (bf *)L->h1}, b * P1, NETH_NET_C1, 1,
                           s);
    } else {
        k_im2col1<T><<<grid((long)b * P0 * K0 / VN, 256), 256, 0, s>>>(b, idx, obs, a0);
        gemm(L, 0, 1, b * P0, NETH_NET_C0, K0, a0, K0, wt + WT_S0, K0, x0, ab, NETH_NET_C0);
        k_im2col<OS, NETH_NET_C0, 4, T><<<grid((long)b * P1 * K1 / VN, 256), 256, 0, s>>>(b, x0, W_(w, stem_b), a1);
        gemm(L, 0, 1, b * P1, NETH_NET_C1, K1, a1, K1, wt + WT_C1, K1, x1, ab, NETH_NET_C1);
    }
#else
#if NETH_LEARN_SPEC
    if constexpr (std::is_same<T, bf>::value)
        k_conv1_mma<<<C1F_BLK, C1F_WARPS * 32, 0, s>>>(b, idx, obs, wt + WT_C1, x1);
    else
#endif
    {
        k_im2col1<T><<<grid((long)b * P1 * K1 / VN, 256), 256, 0, s>>>(b, idx, obs, a1);
        gemm(L, 0, 1, b * P1, NETH_NET_C1, K1, a1, K1, wt + WT_C1, K1, x1, ab, NETH_NET_C1);
        if constexpr (std::is_same<T, bf>::value && NETH_LEARN_IC2)    /* h1 = relu(x1 + b1) padded: conv2's input */
            k_actpad<NETH_NET_O1, NETH_NET_C1><<<grid((long)b * P1 * NETH_NET_C1 / 8, 256), 256, 0, s>>>(b, x1, W_(w, conv1_b),
                                                                                     (bf *)L->h1);
    }
#endif
#if NETH_LEARN_SPEC
    if constexpr (std::is_same<T, bf>::value)
        k_conv2_mma<<<C2F_BLK, 512, C2F_SMEM, s>>>(b, x1, W_(w, conv1_b), wt + WT_C2, x2);
    else
#endif
    if constexpr (std::is_same<T, bf>::value && NETH_LEARN_IC2) {
        /* conv2 over h1 (weights [k][co]: WT_C2F) */
        igemm<NETH_LEARN_T2F, K2>(conv_h<NETH_NET_O2, NETH_NET_C1>{(const bf *)L->h1}, wt + WT_C2F,
                           conv_o<NETH_NET_O2, NETH_NET_C2>{x2, nullptr, nullptr}, b * P2, NETH_NET_C2, 1, s);
    } else {
        k_im2col<NETH_NET_O1, NETH_NET_C1, 4, T><<<grid((long)b * P2 * K2 / VN, 256), 256, 0, s>>>(b, x1, W_(w, conv1_b), a2);
        gemm(L, 0, 1, b * P2, NETH_NET_C2, K2, a2, K2, wt + WT_C2, K2, x2, ab, NETH_NET_C2);
    }
#if NETH_LEARN_SPEC
    if constexpr (std::is_same<T, bf>::value)
        k_conv3_mma<<<C3F_BLK, 512, C3F_SMEM, s>>>(b, x2, W_(w, conv2_b), wt + WT_C3, x3);
    else
#endif
    {
        k_im2col<NETH_NET_O2, NETH_NET_C2, 3, T><<<grid((long)b * P3 * K3 / VN, 256), 256, 0, s>>>(b, x2, W_(w, conv2_b), a3);
        gemm(L, 0, 1, b * P3, NETH_NET_C3, K3, a3, K3, wt + WT_C3, K3, x3, ab, NETH_NET_C3);
    }
    if (!fc) return;
    k_flat<T><<<grid((long)b * Z0LD, 256), 256, 0, s>>>(b, idx, x3, W_(w, conv3_b), sc, z0);
    gemm(L, 0, 1, b, NETH_NET_D, Z0LD, z0, Z0LD, wt + WT_F1, Z0LD, z1, ab, NETH_NET_D);
    k_bias_relu_v<T><<<grid((long)b * NETH_NET_D / VN, 256), 256, 0, s>>>(b, NETH_NET_D, z1, W_(w, fc1_b));
    gemm(L, 0, 1, b, NETH_NET_D, NETH_NET_D, z1, NETH_NET_D, wt + WT_F2, NETH_NET_D, z2, ab, NETH_NET_D);
    k_bias_relu_v<T><<<grid((long)b * NETH_NET_D / VN, 256), 256, 0, s>>>(b, NETH_NET_D, z2, W_(w, fc2_b));
    gemm(L, 0, 1, b, HS, NETH_NET_D, rnn_forward<T>(L, w, b, idx, z2), NETH_NET_D, wt + WT_H, NETH_NET_D, L->hd, CUDA_R_32F, HS);
    k_bias_relu<float><<<grid((long)b * NETH_LEARN_HEADS, 256), 256, 0, s>>>(b, NETH_LEARN_HEADS, HS, L->hd, W_(w, cam_b), 0);
}

template <class T>
static void backward(neth_learn *L, const float *w, const T *wt, int b, const int32_t *idx, const uint8_t *obs, float *grad,
                     bool fc = true) {  /* fc false: k_fcrow made dx3, z0 (channels last), z1, z2, dH, d2 and d1 */
    constexpr int VN = vec<T>::n;
    constexpr bool spec = NETH_LEARN_SPEC && std::is_same<T, bf>::value;  /* lane/ntlearn's tensor-core convolutions */
    cudaStream_t s = L->s;
    cudaDataType_t ab = L->ab, f32 = CUDA_R_32F;
    T *a1 = (T *)L->a1, *x1 = (T *)L->x1, *a2 = (T *)L->a2, *x2 = (T *)L->x2, *a3 = (T *)L->a3, *x3 = (T *)L->x3;
    T *z0 = (T *)L->z0, *z1 = (T *)L->z1, *z2 = (T *)L->z2, *d1 = (T *)L->d1, *d2 = (T *)L->d2;
    T *dx1 = (T *)L->dx1, *dx2 = (T *)L->dx2, *dx3 = (T *)L->dx3;
    const void *dH = L->prec == NETH_LEARN_BF16 ? L->dh : (const void *)L->hd;  /* k_loss wrote the bf16 copy */
#if NETH_LEARN_FC
    if (!fc) {  /* the weights' gradients over k_fcrow's outputs, its biases' block partials summed */
        gemm(L, 1, 0, NETH_LEARN_HEADS, NETH_NET_D, b, dH, HS, z2, NETH_NET_D, W_(grad, cam_w), f32, NETH_NET_D);
        gemm(L, 1, 0, NETH_NET_D, NETH_NET_D, b, d2, NETH_NET_D, z1, NETH_NET_D, W_(grad, fc2_w), f32, NETH_NET_D);
        gemm(L, 1, 0, NETH_NET_D, Z0LD, b, d1, NETH_NET_D, z0, Z0LD, L->gw + GW_F1, f32, Z0LD);
        k_fcfin<<<grid(NETH_LEARN_HEADS + 2 * NETH_NET_D, 256), 256, 0, s>>>(grid(b, FC_BM), L->fcpart, grad);
    } else
#endif
    {
        /* the heads: [97][D] and [97] side by side in the layout (lane/ntgru: over the minGRU's output when NETH_NET_RNN) */
        gemm(L, 1, 0, NETH_LEARN_HEADS, NETH_NET_D, b, dH, HS, rnn_y<T>(L, z2), NETH_NET_D, W_(grad, cam_w), f32, NETH_NET_D);
        colsum(L, b, NETH_LEARN_HEADS, dH, HS, W_(grad, cam_b));
        gemm(L, 0, 0, b, NETH_NET_D, HS, dH, HS, wt + WT_H, NETH_NET_D, d2, ab, NETH_NET_D);     /* dH's padding 0 (k_loss) */
        rnn_backward<T>(L, w, b, idx, z2, d2, grad);   /* NETH_NET_RNN: the minGRU's gradients, d2 += its share */
        k_relu_bwd<T><<<grid((long)b * NETH_NET_D / VN, 256), 256, 0, s>>>((long)b * NETH_NET_D, d2, z2);
        /* fc2, fc1 */
        gemm(L, 1, 0, NETH_NET_D, NETH_NET_D, b, d2, NETH_NET_D, z1, NETH_NET_D, W_(grad, fc2_w), f32, NETH_NET_D);
        colsum(L, b, NETH_NET_D, d2, NETH_NET_D, W_(grad, fc2_b));
        gemm(L, 0, 0, b, NETH_NET_D, NETH_NET_D, d2, NETH_NET_D, wt + WT_F2, NETH_NET_D, d1, ab, NETH_NET_D);
        k_relu_bwd<T><<<grid((long)b * NETH_NET_D / VN, 256), 256, 0, s>>>((long)b * NETH_NET_D, d1, z1);
        /* fc1 at Z0LD columns (z0's and the weight copy's padding 0): its weight gradient into gw, unpadded below */
        gemm(L, 1, 0, NETH_NET_D, Z0LD, b, d1, NETH_NET_D, z0, Z0LD, L->gw + GW_F1, f32, Z0LD);
        colsum(L, b, NETH_NET_D, d1, NETH_NET_D, W_(grad, fc1_b));
        gemm(L, 0, 0, b, Z0LD, NETH_NET_D, d1, NETH_NET_D, wt + WT_F1, Z0LD, z0, ab, Z0LD);       /* dz0 */
        k_unflat<T><<<grid((long)b * P3 * NETH_NET_C3, 256), 256, 0, s>>>(b, z0, x3, W_(w, conv3_b), dx3);
    }
    /* conv3, conv2: the weight gradients in (ky, kx, ci) order into gw */
#if NETH_LEARN_SPEC
    if constexpr (spec) {
        k_conv3_dw_mma<<<C3W_BLK, 576, 0, s>>>(b, dx3, x2, W_(w, conv2_b), L->c3part);
        k_dwsum<<<grid(NETH_NET_C3 * K3, 256), 256, 0, s>>>(NETH_NET_C3 * K3, C3W_BLK, L->c3part, L->gw + GW_C3);
    } else
#endif
        gemm(L, 1, 0, NETH_NET_C3, K3, b * P3, dx3, NETH_NET_C3, a3, K3, L->gw + GW_C3, f32, K3);
    colsum(L, b * P3, NETH_NET_C3, dx3, NETH_NET_C3, W_(grad, conv3_b));
#if NETH_LEARN_SPEC
    if constexpr (spec) {
        if (b % 4 == 0)     /* its tiles are 4 samples */
            k_conv3_dx_mma<<<C3X_BLK, 512, C3X_SMEM, s>>>(b, dx3, wt + WT_C3T, x2, W_(w, conv2_b), dx2);
    }
#endif
    if (!spec || b % 4) {
        gemm(L, 0, 0, b * P3, K3, NETH_NET_C3, dx3, NETH_NET_C3, wt + WT_C3, K3, a3, ab, K3);     /* da3 */
        k_col2im<NETH_NET_O2, NETH_NET_C2, 3, T><<<grid((long)b * P2 * NETH_NET_C2 / VN, 256), 256, 0, s>>>(b, a3, x2, W_(w, conv2_b), dx2);
    }
#if NETH_LEARN_SPEC
    if constexpr (spec) {
        k_conv2_dw_mma<<<C2W_BLK, C2W_T, 0, s>>>(b, dx2, x1, W_(w, conv1_b), L->c2part);
        k_dwsum<<<grid(NETH_NET_C2 * K2, 256), 256, 0, s>>>(NETH_NET_C2 * K2, C2W_BLK, L->c2part, L->gw + GW_C2);
    } else
#endif
    if constexpr (std::is_same<T, bf>::value && NETH_LEARN_IC2)
        cwgrad<NETH_NET_C2, K2>(L, conv_h<NETH_NET_O2, NETH_NET_C1>{(const bf *)L->h1}, dx2, (long)b * P2, L->gw + GW_C2);
    else
        gemm(L, 1, 0, NETH_NET_C2, K2, b * P2, dx2, NETH_NET_C2, a2, K2, L->gw + GW_C2, f32, K2);
    colsum(L, b * P2, NETH_NET_C2, dx2, NETH_NET_C2, W_(grad, conv2_b));
#if NETH_LEARN_SPEC
    if constexpr (spec)
        k_conv2_dx_mma<<<C2X_BLK, 512, C2X_SMEM, s>>>(b, dx2, wt + WT_C2T, x1, W_(w, conv1_b), dx1);
    else
#endif
    if constexpr (std::is_same<T, bf>::value && NETH_LEARN_IC2X) {
        /* four GEMMs, one an input parity, K = (sy, sx, co), N = C1 */
        igemm<NETH_LEARN_T2X, 4 * NETH_NET_C2>(cx_a<NETH_NET_O2, NETH_NET_C2>{dx2}, wt + WT_C2X,
                                  cx_o<NETH_NET_O2, NETH_NET_C1>{x1, W_(w, conv1_b), dx1, NETH_LEARN_IC2 ? (const bf *)L->h1 : nullptr},
                                  b * P2, NETH_NET_C1, 4, s);
    } else {
        gemm(L, 0, 0, b * P2, K2, NETH_NET_C2, dx2, NETH_NET_C2, wt + WT_C2, K2, a2, ab, K2);     /* da2 */
        k_col2im<NETH_NET_O1, NETH_NET_C1, 4, T><<<grid((long)b * P1 * NETH_NET_C1 / VN, 256), 256, 0, s>>>(b, a2, x1, W_(w, conv1_b), dx1);
    }
#if NETH_NET_STEM
    /* conv1 over the stem's output ((ky, kx, ci) order, into gw), its input's gradient, then the stem's */
    T *a0 = (T *)L->a0, *x0 = (T *)L->x0, *dx0 = (T *)L->dx0;
#if NETH_LEARN_S1B
    if constexpr (std::is_same<T, bf>::value) {     /* all of it in one kernel, h0 from the bytes, no dx0 */
        int spb = s1_spb(L, b), nblk = grid(b, spb);
        k_s1b<<<nblk, S1B_W * 32, S1B_SMEM, s>>>(b, spb, idx, obs, wt + WT_S0, W_(w, stem_b), dx1, wt + WT_C1X,
                                                 L->cpart);
        k_s1bsum<<<grid(S1B_PART, 256), 256, 0, s>>>(nblk, L->cpart, L->gw + GW_C1, W_(grad, conv1_b),
                                                     W_(grad, stem_w), W_(grad, stem_b));
        k_gunperm<<<grid(GW_F1 + NETH_LEARN_NFC1, 256), 256, 0, s>>>(L->gw, grad);
        return;
    }
#endif
    if constexpr (std::is_same<T, bf>::value) {
        cwgrad<NETH_NET_C1, K1>(L, conv_h<NETH_NET_O1, NETH_NET_C0>{(const bf *)L->h0}, dx1, (long)b * P1, L->gw + GW_C1);
        colsum(L, b * P1, NETH_NET_C1, dx1, NETH_NET_C1, W_(grad, conv1_b));
        k_c1dx<<<b, 256, 0, s>>>(b, dx1, wt + WT_C1X, (const bf *)L->h0, dx0);
    } else {
        gemm(L, 1, 0, NETH_NET_C1, K1, b * P1, dx1, NETH_NET_C1, a1, K1, L->gw + GW_C1, f32, K1);
        colsum(L, b * P1, NETH_NET_C1, dx1, NETH_NET_C1, W_(grad, conv1_b));
        gemm(L, 0, 0, b * P1, K1, NETH_NET_C1, dx1, NETH_NET_C1, wt + WT_C1, K1, a1, ab, K1);     /* da1 */
        k_col2im<OS, NETH_NET_C0, 4, T><<<grid((long)b * P0 * NETH_NET_C0 / VN, 256), 256, 0, s>>>(b, a1, x0, W_(w, stem_b), dx0);
    }
    k_gunperm<<<grid(GW_F1 + NETH_LEARN_NFC1, 256), 256, 0, s>>>(L->gw, grad, !fc);
    if constexpr (std::is_same<T, bf>::value) {
        k_stem_dw_mma<<<S0W_BLK, 128, 0, s>>>(b, idx, obs, dx0, L->cpart);
        k_stem_dwsum<<<grid(NETH_NET_C0 * SW, 256), 256, 0, s>>>(S0W_BLK, L->cpart, W_(grad, stem_w), W_(grad, stem_b));
    } else {
        gemm(L, 1, 0, NETH_NET_C0, K0, b * P0, dx0, NETH_NET_C0, a0, K0, W_(grad, stem_w), f32, K0);
        colsum(L, b * P0, NETH_NET_C0, dx0, NETH_NET_C0, W_(grad, stem_b));
    }
#else
    k_gunperm<<<grid(GW_F1 + NETH_LEARN_NFC1, 256), 256, 0, s>>>(L->gw, grad, !fc);
    /* conv1 */
#if NETH_LEARN_SPEC
    if constexpr (spec) {
        k_conv1_dw_mma<<<C1W_BLK, 128, 0, s>>>(b, idx, obs, dx1, L->cpart);
        k_conv1_dwsum<<<grid(NETH_NET_C1 * CW, 256), 256, 0, s>>>(C1W_BLK, L->cpart, W_(grad, conv1_w), W_(grad, conv1_b));
    } else
#endif
    {
        gemm(L, 1, 0, NETH_NET_C1, K1, b * P1, dx1, NETH_NET_C1, a1, K1, W_(grad, conv1_w), f32, K1);
        colsum(L, b * P1, NETH_NET_C1, dx1, NETH_NET_C1, W_(grad, conv1_b));
    }
#endif
}

const float *neth_learn_forward(neth_learn *L, const float *w, int b, const int32_t *idx, const uint8_t *obs, const neth_sc_t *sc) {
    if (L->prec == NETH_LEARN_BF16)
        forward<bf>(L, w, weights<bf>(L, w), b, idx, obs, sc);
    else
        forward<float>(L, w, weights<float>(L, w), b, idx, obs, sc);
    return L->hd;
}

void neth_learn_grad(neth_learn *L, const neth_learn_hyper *h, float entc, const float *w, int b, const int32_t *idx, const uint8_t *obs,
              const neth_sc_t *sc, const int32_t *cam, const float *keys, const int32_t *hot, const float *logp0,
              const float *adv, const float *ret, const float *mask, float *grad, float *books) {
    cudaStream_t s = L->s;
    const void *rh = h->ref_kl > 0.0f && L->wref && !h->vonly ? L->rhd : NULL;   /* lane/ntppo */
#if NETH_LEARN_FC
    if (L->prec == NETH_LEARN_BF16 && !rh && !h->vonly && !L->C && !L->val0) {  /* lane/ntfc: the convolutions, then k_fcrow from x3 to dx3, then the rest */
        const bf *wt = weights<bf>(L, w);
        forward<bf>(L, w, wt, b, idx, obs, sc, false);
        k_nm<<<1, 1024, 0, s>>>(b, idx, mask, L->nm);
        k_fcpack<<<grid(PK_N, 256), 256, 0, s>>>(w, (uint4 *)L->fcpk);
        fc_args a = {b, idx, (const bf *)L->x3, w, sc, (const uint4 *)L->fcpk, cam, keys, logp0, adv, ret, mask, L->nm,
                     h->clip, h->vf, entc, (bf *)L->z0, (bf *)L->z1, (bf *)L->z2, (bf *)L->dh, (bf *)L->d2,
                     (bf *)L->d1, (bf *)L->dx3, L->part, L->fcpart};
        switch (NETH_NET_NSP(NETH_NET_NS)) {   /* lane/audit-feat: fc1's K, the run's (net.h NETH_NET_NSP_CASES) */
#define FC_ROW(NP)                                                                       \
        case NP:                                                                         \
            k_fcrow<NETH_NET_FLAT0 + NP><<<grid(b, FC_BM), FC_T, FC_SMEM, s>>>(a);      \
            break;
        NETH_NET_NSP_CASES(FC_ROW)
#undef FC_ROW
        }
        k_books<<<1, BT, 0, s>>>(grid(b, FC_BM), b, L->part, L->nm, h->vf, entc, books, 0);
        backward<bf>(L, w, wt, b, idx, obs, grad, false);
        if (NETH_NET_LSCALE != 1.0f)
            k_scale<<<grid(NETH_LEARN_NPARAM, 256), 256, 0, s>>>(NETH_LEARN_NPARAM, grad, 1.0f / NETH_NET_LSCALE);
        return;
    }
#endif
    neth_learn_forward(L, w, b, idx, obs, sc);
    /* lane/acrl: the privileged critic's value into the value column (before the loss reads it) */
    if (L->C && !h->loss) neth_critic_fwd(L, L->C, b, idx, L->psc, L->hd + (NETH_LEARN_HEADS - 1), HS);
    if (h->loss) /* lane/ntbc: distillation's loss (train/distill.cu) */
        h->loss(h->lctx, b, idx, L->hd, L->prec == NETH_LEARN_BF16 ? L->dh : NULL, s);
    else {
        k_nm<<<1, 1024, 0, s>>>(b, idx, mask, L->nm);
        int nblk = grid(b, LW);
        if (L->prec == NETH_LEARN_BF16)
            k_loss<bf><<<nblk, LW * 32, 0, s>>>(b, idx, L->hd, (bf *)L->dh, cam, keys, hot,
                                                logp0, adv, ret, mask, L->nm, h->clip, h->vf, entc, L->part, (const bf *)rh,
                                                rh ? h->ref_kl : 0.0f, !h->vonly, L->val0, h->vclip);
        else
            k_loss<float><<<nblk, LW * 32, 0, s>>>(b, idx, L->hd, NULL, cam, keys, hot,
                                                   logp0, adv, ret, mask, L->nm, h->clip, h->vf, entc, L->part, (const float *)rh,
                                                   rh ? h->ref_kl : 0.0f, !h->vonly, L->val0, h->vclip);
        k_books<<<1, BT, 0, s>>>(nblk, b, L->part, L->nm, h->vf, entc, books, 0, rh ? h->ref_kl : 0.0f);
        /* lane/acrl: the critic's gradient from the loss's d value (k_loss wrote it in place, fp32) */
        if (L->C) neth_critic_bwd(L, L->C, b, idx, L->psc, L->hd + (NETH_LEARN_HEADS - 1), HS);
    }
    if (L->prec == NETH_LEARN_BF16) {
        backward<bf>(L, w, (const bf *)L->wt, b, idx, obs, grad);
        if (NETH_NET_LSCALE != 1.0f)  /* the loss scale off the gradients (a power of two: exact) */
            k_scale<<<grid(NETH_LEARN_NPARAM, 256), 256, 0, s>>>(NETH_LEARN_NPARAM, grad, 1.0f / NETH_NET_LSCALE);
    } else
        backward<float>(L, w, (const float *)L->wt, b, idx, obs, grad);
}

#if NETH_NET_RNN
void neth_learn_seq(neth_learn *L, int T, const uint8_t *done, const float *h0) {
    L->rnn->T = T;
    L->rnn->done = done;
    L->rnn->h0 = h0;
}
#endif

void neth_learn_adam(neth_learn *L, const neth_learn_hyper *h, int step, float *w, float *grad, float *m, float *v, float *gnorm) {
    int np = 264;   /* about two blocks an SM */
    k_sumsq<<<np, AT, 0, L->s>>>(NETH_LEARN_NPARAM, grad, L->part);
    k_adam<<<np, AT, 0, L->s>>>(NETH_LEARN_NPARAM, np, L->part, w, grad, m, v, (float)step, h->lr, h->beta1, h->beta2, h->eps,
                                h->max_norm, gnorm, h->kl_stop > 0.0f ? L->kstop : NULL, L->tstat);
}

__global__ void k_addbooks(float *acc, const float *b) {
    if (threadIdx.x < NETH_LEARN_NBOOK) acc[threadIdx.x] += b[threadIdx.x];
}

/* ---- lane/ntppo: the reference policy and the value warm-up ---- */

int neth_learn_set_ref(neth_learn *L, const float *wref) {
    L->wref = wref;
    if (wref && !L->rhd && cudaMalloc(&L->rhd, (size_t)L->nmax * NETH_LEARN_RSTRIDE * L->es) != cudaSuccess) {
        fprintf(stderr, "neth_learn_set_ref: cudaMalloc failed (%d samples)\n", L->nmax);
        L->wref = NULL;
        return -1;
    }
    if (wref && !L->wt_ref && cudaMalloc(&L->wt_ref, (size_t)WT_N * L->es) != cudaSuccess) {
        fprintf(stderr, "neth_learn_set_ref: cudaMalloc wt_ref failed\n");
        L->wref = NULL;
        return -1;
    }
    if (wref) {
        if (L->prec == NETH_LEARN_BF16)
            k_wcvt<bf><<<grid(WT_N, 256), 256, 0, L->s>>>(wref, (bf *)L->wt_ref);
        else
            k_wcvt<float><<<grid(WT_N, 256), 256, 0, L->s>>>(wref, (float *)L->wt_ref);
    }
    return 0;
}

/* the forward's heads rows [n][HS] to the reference's rows from i0 [.][NETH_LEARN_RSTRIDE] */
template <class RefT>
__global__ void k_refrows(int n, const float *hd, RefT *rhd) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)n * NETH_LEARN_RSTRIDE) return;
    long r = i / NETH_LEARN_RSTRIDE, c = i % NETH_LEARN_RSTRIDE;
    rhd[i] = cvt<RefT>(hd[r * HS + c]);
}

#if NETH_NET_RNN
/* scatter forward's heads rows [n][HS] into rhd[idx[r]][NETH_LEARN_RSTRIDE] for chunked minGRU ref_kl */
template <class RefT>
__global__ void k_refrows_idx(int n, const int32_t *idx, const float *hd, RefT *rhd) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)n * NETH_LEARN_RSTRIDE) return;
    long r = i / NETH_LEARN_RSTRIDE, c = i % NETH_LEARN_RSTRIDE;
    rhd[(long)idx[r] * NETH_LEARN_RSTRIDE + c] = cvt<RefT>(hd[r * HS + c]);
}
#endif

void neth_learn_ref_forward(neth_learn *L, int i0, int n, const uint8_t *obs, const neth_sc_t *sc) {
    if (!L->wref) return;
#if NETH_NET_RNN
    if (n > L->mb && L->rnn && L->rnn->T > 0 && n % L->rnn->T == 0) {
        int T = L->rnn->T, gs = n / T, be = L->mb / T > 0 ? L->mb / T : 1, step = be * T;
        if (L->rnn->ref_n != n || L->rnn->ref_T != T || L->rnn->ref_mb != step) {
            cudaFree(L->rnn->ref_idx);
            CK(cudaMalloc((void **)&L->rnn->ref_idx, (size_t)n * sizeof(int32_t)));
            int32_t *ep = (int32_t *)malloc((size_t)gs * sizeof(int32_t));
            int32_t *hi = (int32_t *)malloc((size_t)n * sizeof(int32_t));
            for (int e = 0; e < gs; ++e) ep[e] = e;
            neth_learn_cols(ep, T, gs, be, hi);
            CK(cudaMemcpyAsync(L->rnn->ref_idx, hi, (size_t)n * sizeof(int32_t), cudaMemcpyHostToDevice, L->s));
            CK(cudaStreamSynchronize(L->s));
            free(ep), free(hi);
            L->rnn->ref_n = n, L->rnn->ref_T = T, L->rnn->ref_mb = step;
        }
        const uint8_t *ob0 = obs + (long)i0 * NETH_NET_H * NETH_NET_W * NETH_NET_C;
        const neth_sc_t *sc0 = sc + (long)i0 * NETH_NET_NS;
        for (int i = 0; i < n; i += step) {
            int b = n - i < step ? n - i : step;
            const int32_t *ix = L->rnn->ref_idx + i;
            if (L->prec == NETH_LEARN_BF16) {
                forward<bf>(L, L->wref, (const bf *)L->wt_ref, b, ix, ob0, sc0);
                k_refrows_idx<bf><<<grid((long)b * NETH_LEARN_RSTRIDE, 256), 256, 0, L->s>>>(
                    b, ix, L->hd, (bf *)L->rhd + (long)i0 * NETH_LEARN_RSTRIDE);
            } else {
                forward<float>(L, L->wref, (const float *)L->wt_ref, b, ix, ob0, sc0);
                k_refrows_idx<float><<<grid((long)b * NETH_LEARN_RSTRIDE, 256), 256, 0, L->s>>>(
                    b, ix, L->hd, (float *)L->rhd + (long)i0 * NETH_LEARN_RSTRIDE);
            }
        }
        return;
    }
#endif
    for (int i = 0; i < n; i += L->mb) {
        int b = n - i < L->mb ? n - i : L->mb;
        long o = (long)i0 + i;
        const uint8_t *ob = obs + o * NETH_NET_H * NETH_NET_W * NETH_NET_C;
        const neth_sc_t *scb = sc + o * NETH_NET_NS;
        if (L->prec == NETH_LEARN_BF16) {
            forward<bf>(L, L->wref, (const bf *)L->wt_ref, b, NULL, ob, scb);
            k_refrows<bf><<<grid((long)b * NETH_LEARN_RSTRIDE, 256), 256, 0, L->s>>>(b, L->hd, (bf *)L->rhd + o * NETH_LEARN_RSTRIDE);
        } else {
            forward<float>(L, L->wref, (const float *)L->wt_ref, b, NULL, ob, scb);
            k_refrows<float><<<grid((long)b * NETH_LEARN_RSTRIDE, 256), 256, 0, L->s>>>(b, L->hd, (float *)L->rhd + o * NETH_LEARN_RSTRIDE);
        }
    }
}

void neth_learn_adam_value(neth_learn *L, const neth_learn_hyper *h, int step, float *w, float *grad, float *m, float *v, float *gnorm) {
    /* v.weight, then cam.bias and keys.bias (no gradient with the policy's terms off: k_loss's pol 0), then v.bias:
     * one span for the norm, the two value tensors' Adam steps */
    const int o0 = NETH_LEARN_OFF(v_w), span = NETH_LEARN_OFF(v_b) + 1 - o0, np = 8;
    k_sumsq<<<np, AT, 0, L->s>>>(span, grad + o0, L->part);
    const int t[2] = {NETH_LEARN_OFF(v_w), NETH_LEARN_OFF(v_b)}, k[2] = {NETH_LEARN_N_v_w, NETH_LEARN_N_v_b};
    for (int j = 0; j < 2; j++)
        k_adam<<<grid(k[j], AT), AT, 0, L->s>>>(k[j], np, L->part, w + t[j], grad + t[j], m + t[j], v + t[j],
                                                 (float)step, h->lr, h->beta1, h->beta2, h->eps, h->max_norm,
                                                 j ? NULL : gnorm, h->kl_stop > 0.0f ? L->kstop : NULL, j ? NULL : L->tstat);
}

__global__ void k_klstop(const float *books, float target, int *ks);    /* lane/acrl, below */
static void neth_critic_adam(neth_learn *L, const neth_learn_hyper *h);

void neth_learn_update(neth_learn *L, const neth_learn_hyper *h, float entc, int T, int gs, float scale, int epochs, int mb,
                const int32_t *perm, const uint8_t *obs, const neth_sc_t *sc, const int32_t *cam, const float *keys,
                const int32_t *hot, const float *logp, const float *val, const float *rew, const uint8_t *done,
                const uint8_t *term, const uint8_t *drv, const float *boot, float *w, float *grad, float *m, float *v,
                int *step, float *books) {
    neth_learn_update_k(L, h, entc, T, gs, 1, &scale, epochs, mb, perm, obs, sc, cam, keys, hot, logp, val, rew, done, term,
                        drv, boot, w, grad, m, v, step, books);
}

void neth_learn_update_k(neth_learn *L, const neth_learn_hyper *h, float entc, int T, int gs, int K, const float *scale,
                int epochs, int mb, const int32_t *perm, const uint8_t *obs, const neth_sc_t *sc, const int32_t *cam,
                const float *keys, const int32_t *hot, const float *logp, const float *val, const float *rew,
                const uint8_t *done, const uint8_t *term, const uint8_t *drv, const float *boot, float *w, float *grad,
                float *m, float *v, int *step, float *books) {
    int N1 = T * gs, N = K * N1;
#if NETH_NET_RNN
    neth_learn_seq(L, T, done, L->rnn->h0);    /* the scan's length and the segment's ends; its start states neth_learn_seq's */
#endif
    if (K == 1)
        neth_learn_gae(L, h, T, gs, scale[0], rew, done, term, drv, val, boot, L->adv, L->ret, L->mask);
    else {
        /* each segment's GAE with its own reward scale and bootstrap, then the advantages normalised over all K */
        float gl = (float)(h->gamma * h->lam);
        for (int k = 0; k < K; k++) {
            size_t o = (size_t)k * N1;
            k_gae<<<grid(gs, GE), 256, 3 * T * GE * sizeof(float), L->s>>>(T, gs, scale[k], (float)h->gamma, gl, rew + o,
                                                                            done + o, term + o, drv + o, val + o,
                                                                            boot + (size_t)k * gs,
                                                                            L->adv + o, L->ret + o, L->mask + o);
        }
        k_advnorm<<<1, 1024, 0, L->s>>>(N, L->adv, L->mask);
    }
    k_ev<<<1, 256, 0, L->s>>>(N, val, L->ret, L->tstat);
    if (h->ref_kl > 0.0f && L->wref && !h->vonly) neth_learn_ref_forward(L, 0, N, obs, sc);  /* lane/ntppo */
    L->val0 = h->vclip > 0.0f ? val : NULL;     /* lane/acrl */
    int ks = h->kl_stop > 0.0f && !h->vonly;
    if (ks) {
        if (!L->kstop) {
            CK(cudaMalloc((void **)&L->kstop, 3 * sizeof(int)));
            CK(cudaMemsetAsync(L->kstop, 0, 3 * sizeof(int), L->s));
        }
        CK(cudaMemsetAsync(L->kstop, 0, sizeof(int), L->s));
    }
    neth_learn_hyper hk = *h;
    if (!ks) hk.kl_stop = 0.0f;
    for (int e = 0; e < epochs; e++)
        for (int i = 0; i < N; i += mb) {
            int b = N - i < mb ? N - i : mb;
            neth_learn_grad(L, &hk, entc, w, b, perm + (long)e * N + i, obs, sc, cam, keys, hot, logp, L->adv, L->ret, L->mask,
                     grad, L->books);
            if (ks) k_klstop<<<1, 1, 0, L->s>>>(L->books, hk.kl_stop, L->kstop);
            if (h->vonly)
                neth_learn_adam_value(L, &hk, ++*step, w, grad, m, v, NULL);
            else
                neth_learn_adam(L, &hk, ++*step, w, grad, m, v, NULL);
            if (L->C) neth_critic_adam(L, &hk);
            if (books) k_addbooks<<<1, 32, 0, L->s>>>(books, L->books);
        }
    L->val0 = NULL;
}

/* ---- lane/acrl ---- */

/* hyper kl_stop: the flag set at the first minibatch whose approx KL (book 4) passes the target; a step counted where
 * it is not */
__global__ void k_klstop(const float *books, float target, int *ks) {
    if (!ks[0] && books[4] > target) {
        ks[0] = 1;
        ks[2]++;
    }
    if (!ks[0]) ks[1]++;
}

void neth_learn_set_critic(neth_learn *L, neth_critic *C, const neth_sc_t *psc) {
    L->C = C;
    L->psc = psc;
}

/* the critic's Adam: its own norm (clip at max_norm), torch's fused step; skipped with the trunk's under kl_stop */
static void neth_critic_adam(neth_learn *L, const neth_learn_hyper *h) {
    neth_critic *C = L->C;
    const int np = 16;
    ++C->step;
    k_sumsq<<<np, AT, 0, L->s>>>(C->P, C->g, C->part);
    k_adam<<<grid(C->P, AT), AT, 0, L->s>>>(C->P, np, C->part, C->w, C->g, C->m, C->v, (float)C->step, h->lr,
                                            h->beta1, h->beta2, h->eps, h->max_norm, NULL,
                                            h->kl_stop > 0.0f ? L->kstop : NULL);
}

void neth_learn_kstat(neth_learn *L, long long *taken, long long *stopped) {
    int k[3] = {0, 0, 0};
    if (L->kstop) {
        CK(cudaMemcpyAsync(k, L->kstop, sizeof k, cudaMemcpyDeviceToHost, L->s));
        CK(cudaStreamSynchronize(L->s));
        CK(cudaMemsetAsync(L->kstop + 1, 0, 2 * sizeof(int), L->s));
    }
    *taken = k[1];
    *stopped = k[2];
}

void neth_learn_tstat(neth_learn *L, double out[4]) {
    float s[6] = {0};
    if (L->tstat) {
        CK(cudaMemcpyAsync(s, L->tstat, sizeof s, cudaMemcpyDeviceToHost, L->s));
        CK(cudaStreamSynchronize(L->s));
        CK(cudaMemsetAsync(L->tstat, 0, sizeof s, L->s));
    }
    out[0] = s[2] > 0.0f ? (double)s[0] / (double)s[2] : 0.0;
    out[1] = (double)s[1];
    out[2] = (double)s[3];
    out[3] = s[5] > 0.0f ? (double)s[4] / (double)s[5] : 0.0;
}
