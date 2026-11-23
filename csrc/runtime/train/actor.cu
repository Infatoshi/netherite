/* The actor's forward and sampler (actor.h; net.h has the net). Each layer is a GEMM over the group, out[m][n] =
 * relu(bias[n] + sum_k a(m, k) w[k][n]), its rows the group's samples (times the output positions for a
 * convolution: an implicit im2col over channels-last activations) and its weights transposed to [k][n] at load.
 * The heads, the sampler and logp are one kernel. Activations stay channels last ([B][y][x][c]: the cast's own
 * layout), so fc1's weights for the flattened convolution are permuted at load from net.h's channel-major order. */
#include "actor.h"
#include "learn.h"
#include "net.h"

#include "prec.cuh"

#include <cuda_runtime.h>
#include <mma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NETH_NET_HOFF (NETH_NET_CAM * NETH_NET_BINS + NETH_NET_KEYS)        /* the hotbar's logits (NETH_NET_HOTBAR), after the keys' */
#define NETH_NET_HEADS (NETH_NET_HOFF + NETH_NET_HOTBAR * NETH_NET_HOT + 1) /* 97: the camera's 82, the keys' 14, (the hotbar's 10,) the value */
#define NETH_NET_HP 112                                  /* the heads padded to a whole number of 16-wide tiles */
static_assert(NETH_NET_HEADS <= NETH_NET_HP, "the heads' padded row");

struct neth_actor {
    float *w0, *b0;                      /* the stem (NETH_NET_STEM), [k][n] as the convolutions' */
    float *w1, *b1, *w2, *b2, *w3, *b3;  /* convolutions, [k][n] with k = (kh * KW + kw) * Cin + ci */
    float *f1, *fb1, *f2, *fb2;          /* fc1 [F1K][D] (its first rows in the channels-last flatten), fc2 [D][D] */
    float *hw, *hb;                      /* the heads [D][NETH_NET_HP] and their biases (zero past NETH_NET_HEADS) */
    float *rw, *rb;                      /* (NETH_NET_RNN) the minGRU's [D][2D] and [2D] (rnn.cuh's actor step) */
    nt16 *rwh;                           /* its bf16 copy */
    float *mem;
    nt16 *w0h, *w1h, *w2h, *w3h, *f1h, *f2h, *hwh, *mem16;   /* the weights in bf16, the same layouts */
    uint4 *wf1c, *wf2, *wf3, *wf1, *wff2, *memf;   /* conv1's (after the stem), conv2's, conv3's, fc1's and fc2's bf16
                                                    * weights in gemm_mmr's fragment order */
    int32_t *map;                        /* neth_actor_reload's: each slot of mem (and mem16), then of memf, the
                                          * learner's index of the weight it holds (learn.h's layout), or -1: zero */
    size_t tot, nfr;                     /* floats in mem (bf16s in mem16), bf16s in memf */
    int mode;
};

struct neth_actor_ws {
    neth_actor *a;
    int max_b;
    float *x0, *x1, *x2, *z0, *z1, *z2;  /* z0 [B][F1K]: fc1's input; x0 the stem's output (NETH_NET_STEM) */
    float *y;                            /* (NETH_NET_RNN) [B][D] z2 + h, the heads' input */
    nt16 *x0h, *x1h, *x2h, *z0h, *z1h;   /* the bf16 path's (its z2 is fp32: heads_draw's input) */
    unsigned long long *rng;             /* [0] the seed, [1] the counter (advanced by each call) */
    float *mem;
};

/* ---- the layers ---- */

/* The activations, channels last; a convolution's input keeps a zero border of one and fc1's row its zero tail
 * (never written after the workspace's memset), so a column group of every layer's input is one aligned float4 load
 * at row(m) + col(k), and col is affine across chunks (col(k + KC) = col(k) + col(KC): a chunk is whole kernel rows):
 * a thread's load addresses are computed once and stepped. At wm 1, 16x16: x1 [B][10][10][32] (conv1's 8x8
 * outputs, padded), x2 [B][6][6][64] (conv2's 4x4), z0 [B][448] (conv3's flattened 2x2x64, the 155 scalars, zeros:
 * fc1's k, two chunks of 224). */
#define OS (NETH_NET_SIDE / 2)    /* the first layer's output side (conv1's, or the stem's) */
#define P0 (OS + 2)         /* x0 [B][P0][P0][16]: the stem's output, padded (NETH_NET_STEM) */
#define P1 (NETH_NET_O1 + 2)      /* x1 [B][P1][P1][C1]: conv1's, padded */
#define P2 (NETH_NET_O2 + 2)      /* x2 [B][P2][P2][C2]: conv2's, padded */
#define Q3 (NETH_NET_O3 * NETH_NET_O3)  /* conv3's positions */
#define F1K ((NETH_NET_FLAT + 63) / 64 * 64)   /* z0's row: fc1's k padded with zeros to a multiple of 64 */
#define K1 (16 * NETH_NET_CI)     /* conv1's k: 48 from the bytes, 256 after the stem */

/* the first layer's (conv1's, or the stem's) rows m = (b, oy, ox) over OS x OS, columns k = (kh, kw, ci) of 48: the
 * cast's bytes times 1 / 255 (Small.planes: x.float() * (1 / 255)), zero outside the cast (the caller's buffer: not
 * padded) */
struct conv1_a {
    static constexpr bool AFFINE = false;
    static constexpr int ROW = 1;
    const uint8_t *x;
    __device__ float4 operator()(int m, int k4) const {
        unsigned u = m;   /* unsigned: a power of two divides as a shift */
        int b = u / (OS * OS), oy = u / OS % OS, ox = u % OS;
        float v[4];
#pragma unroll
        for (int j = 0; j < 4; j++) {
            int k = k4 * 4 + j, ci = k % NETH_NET_C, t = k / NETH_NET_C, iy = oy * 2 - 1 + (t >> 2), ix = ox * 2 - 1 + (t & 3);
            v[j] = iy >= 0 && iy < NETH_NET_H && ix >= 0 && ix < NETH_NET_W ? (float)x[((b * NETH_NET_H + iy) * NETH_NET_W + ix) * NETH_NET_C + ci] * (1.0f / 255)
                                                                 : 0.f;
        }
        return make_float4(v[0], v[1], v[2], v[3]);
    }
};

/* a stride-2 convolution over a padded input (HP wide, C channels): m = (b, oy, ox), k = (kh, kw, ci) */
template <int HP, int C, int OH, int OW, int KW, typename T = float>
struct conv_a {
    static constexpr bool AFFINE = true;
    static constexpr int ROW = KW * C;   /* col is affine across chunks of whole kernel rows */
    const T *x;
    __device__ const T *row(int m) const { return x + ((m / (OH * OW) * HP + m / OW % OH * 2) * HP + m % OW * 2) * C; }
    __device__ int col(int k) const { return (k / (KW * C) * HP + k / C % KW) * C + k % C; }
};

/* a dense input, rows of LD */
template <int LD, typename T = float>
struct dense_a {
    static constexpr bool AFFINE = true;
    static constexpr int ROW = 1;
    const T *x;
    __device__ const T *row(int m) const { return x + m * LD; }
    __device__ int col(int k) const { return k; }
};

/* where output row m goes (as float or bf16): a dense row of N, or the inside of a padded map */
__device__ inline void put(float *p, float v) { *p = v; }
__device__ inline void put(nt16 *p, float v) { *p = f2h(v); }
template <typename T = float>
struct dense_o {
    T *y;
    int n;
    __device__ T *operator()(int m) const { return y + m * n; }
};
template <int OH, int OW, int C, typename T = float>
struct pad_o {
    T *y;
    __device__ T *operator()(int m) const {
        int b = m / (OH * OW), oy = m / OW % OH, ox = m % OW;
        return y + ((b * (OH + 2) + oy + 1) * (OW + 2) + ox + 1) * C;
    }
};

/* what a layer's blocks do first (the blocks of column 0, rows m0 .. m0 + bm - 1 of M): nothing, or the first
 * layer's (OS x OS rows a sample): advance the device RNG's counter (the sampler reads it, after this kernel) and copy
 * the scalars of each sample that starts in the block into fc1's row */
struct pre_none {
    __device__ void operator()(int m0, int bm, int M, int t, int nt) const {}
};
template <typename T = float>
struct pre_conv1 {
    unsigned long long *rng;
    const neth_sc_t *sc;
    T *z0;
    __device__ void operator()(int m0, int bm, int M, int t, int nt) const {
        if (rng && !m0 && !t)
            rng[1]++;
        constexpr int R = OS * OS;
        for (int s = (m0 + R - 1) / R; s * R < m0 + bm && s * R < M; s++)   /* the samples whose row 0 is the block's */
            for (int j = t; j < NETH_NET_NS; j += nt)
                put(z0 + s * F1K + NETH_NET_C3 * Q3 + j, neth_sc_f32(sc[s * NETH_NET_NS + j]));
    }
};

/* an affine loader's chunks of KC are stepped (col(k + KC) = col(k) + col(KC)) when KC is whole kernel rows; else
 * (a wider net's conv2 under fp32's chunks) each chunk's column is computed */
template <class A, int KC>
constexpr bool stepped = A::AFFINE && KC % A::ROW == 0;

/* a tile's input loads for one chunk: PA float4s of A (pa: their addresses in chunk 0, or their rows when not
 * stepped; null past M), stepped by dA */
template <int PA, int NT, int NA, int K4, class A>
__device__ inline void load_a(const A &a, float4 (&ra)[PA], const float *const (&pa)[PA], int c, int dA, int m0, int M,
                              int k0, int t)
{
#pragma unroll
    for (int u = 0; u < PA; u++) {
        int i = t + u * NT;
        if (NA % NT == 0 || i < NA) {
            if constexpr (stepped<A, K4 * 4>)
                ra[u] = pa[u] ? *(const float4 *)(pa[u] + c * dA) : make_float4(0.f, 0.f, 0.f, 0.f);
            else if constexpr (A::AFFINE)
                ra[u] = pa[u] ? *(const float4 *)(pa[u] + a.col(k0 + i % K4 * 4)) : make_float4(0.f, 0.f, 0.f, 0.f);
            else
                ra[u] = m0 + i / K4 < M ? a(m0 + i / K4, k0 / 4 + i % K4) : make_float4(0.f, 0.f, 0.f, 0.f);
        }
    }
}

template <int PA, int NT, int NA, int K4, class A>
__device__ inline void addr_a(const A &a, const float *(&pa)[PA], int m0, int M, int t)
{
#pragma unroll
    for (int u = 0; u < PA; u++) {
        int i = t + u * NT, mm = i / K4;
        pa[u] = nullptr;
        if constexpr (A::AFFINE)
            if ((NA % NT == 0 || i < NA) && m0 + mm < M)
                pa[u] = a.row(m0 + mm) + (stepped<A, K4 * 4> ? a.col(i % K4 * 4) : 0);
    }
}

template <class A, int KC>
__device__ inline int step_a(const A &a)
{
    if constexpr (stepped<A, KC>)
        return a.col(KC);
    else
        return 0;
}

/* o(m)[n] = act(bias[n] + sum_k a(m, k) wt[k][n]) over a grid of BM x BN tiles (N a multiple of BN, K of KC: the
 * weights' rows padded with zeros; act relu or none). Every layer is small and waits on the last, so a block's
 * loads for a chunk of KC columns are all issued at once (one memory round trip a chunk) as float4s; its KS slices
 * of threads split the chunk's column groups (q = ks, ks + KS, ...) over TM rows and 4 TV columns each, reading
 * shared memory as float4s, then sum their partials in order through shared memory. */
template <int BM, int BN, int TM, int TV, int KS, int KC, bool RELU, class A, class O, class P>
__global__ void __launch_bounds__((BM / TM) * (BN / (4 * TV)) * KS)
gemm_tile(A a, O o, P pre, const float *__restrict__ wt, const float *__restrict__ bias, int M, int N, int K)
{
    constexpr int RM = BM / TM, RN = BN / (4 * TV), NT = RM * RN * KS, K4 = KC / 4, N4 = BN / 4;
    constexpr int AS = KC + 4;   /* As's rows padded: TM rows' float4s at one column fall in distinct banks */
    constexpr int NA = BM * K4, NW = KC * N4, PA = (NA + NT - 1) / NT, PW = (NW + NT - 1) / NT;
    constexpr int SMN = BM * AS + KC * BN > KS * BM * BN ? BM * AS + KC * BN : KS * BM * BN;
    static_assert(KC % 4 == 0 && K4 % KS == 0 && BN % (4 * TV) == 0 && BM % TM == 0, "a tile's shape");
    __shared__ __align__(16) float sm[SMN];
    float *As = sm, *Ws = sm + BM * AS;   /* As[m][k], Ws[k][n] */
    int m0 = blockIdx.x * BM, n0 = blockIdx.y * BN;
    int t = threadIdx.x, tn = t % RN, tm = t / RN % RM, ks = t / (RN * RM);
    if (!blockIdx.y)
        pre(m0, BM, M, t, NT);
    const float *pa[PA];
    addr_a<PA, NT, NA, K4>(a, pa, m0, M, t);
    const int dA = step_a<A, KC>(a);
    const float *pw = wt + t / N4 * N + n0 + t % N4 * 4;   /* thread t's u-th weight float4: pw + u (NT / N4) N */
    float acc[TM][4 * TV];
#pragma unroll
    for (int i = 0; i < TM; i++)
#pragma unroll
        for (int j = 0; j < 4 * TV; j++)
            acc[i][j] = 0.f;
    /* chunk c + 1's loads are in flight while chunk c is summed */
    float4 ra[PA], rw[PW];
    load_a<PA, NT, NA, K4>(a, ra, pa, 0, dA, m0, M, 0, t);
#pragma unroll
    for (int u = 0; u < PW; u++)
        if (NW % NT == 0 || t + u * NT < NW)
            rw[u] = *(const float4 *)(pw + u * (NT / N4) * N);
    for (int c = 0, k0 = 0; k0 < K; c++, k0 += KC) {
        if (k0)
            __syncthreads();
#pragma unroll
        for (int u = 0; u < PA; u++) {
            int i = t + u * NT;
            if (NA % NT == 0 || i < NA)
                *(float4 *)(As + i / K4 * AS + i % K4 * 4) = ra[u];
        }
#pragma unroll
        for (int u = 0; u < PW; u++) {
            int i = t + u * NT;
            if (NW % NT == 0 || i < NW)
                *(float4 *)(Ws + i * 4) = rw[u];
        }
        __syncthreads();
        if (k0 + KC < K) {
            load_a<PA, NT, NA, K4>(a, ra, pa, c + 1, dA, m0, M, k0 + KC, t);
#pragma unroll
            for (int u = 0; u < PW; u++)
                if (NW % NT == 0 || t + u * NT < NW)
                    rw[u] = *(const float4 *)(pw + (k0 + KC + u * (NT / N4)) * N);
        }
#pragma unroll 2
        for (int q = ks; q < K4; q += KS) {
            float4 av[TM];
#pragma unroll
            for (int i = 0; i < TM; i++)
                av[i] = *(const float4 *)(As + (tm + i * RM) * AS + q * 4);
#pragma unroll
            for (int c4 = 0; c4 < 4; c4++) {
#pragma unroll
                for (int v = 0; v < TV; v++) {
                    float4 w = *(const float4 *)(Ws + (q * 4 + c4) * BN + (tn + v * RN) * 4);
#pragma unroll
                    for (int i = 0; i < TM; i++) {
                        float x = c4 == 0 ? av[i].x : c4 == 1 ? av[i].y : c4 == 2 ? av[i].z : av[i].w;
                        acc[i][v * 4] = fmaf(x, w.x, acc[i][v * 4]);
                        acc[i][v * 4 + 1] = fmaf(x, w.y, acc[i][v * 4 + 1]);
                        acc[i][v * 4 + 2] = fmaf(x, w.z, acc[i][v * 4 + 2]);
                        acc[i][v * 4 + 3] = fmaf(x, w.w, acc[i][v * 4 + 3]);
                    }
                }
            }
        }
    }
    __syncthreads();
#pragma unroll
    for (int i = 0; i < TM; i++)
#pragma unroll
        for (int v = 0; v < TV; v++)
            *(float4 *)(sm + (ks * BM + tm + i * RM) * BN + (tn + v * RN) * 4) =
                make_float4(acc[i][v * 4], acc[i][v * 4 + 1], acc[i][v * 4 + 2], acc[i][v * 4 + 3]);
    __syncthreads();
    for (int e = t; e < BM * BN; e += NT) {
        int m = e / BN, n = e % BN;
        if (m0 + m >= M)
            continue;
        float v = sm[e];
#pragma unroll
        for (int s = 1; s < KS; s++)
            v += sm[s * BM * BN + e];
        v += bias[n0 + n];
        put(o(m0 + m) + n0 + n, RELU ? fmaxf(v, 0.f) : v);
    }
}

template <int BM, int BN, int TM, int TV, int KS, int KC, bool RELU = true, class A, class O, class P = pre_none>
static void gemm(A a, O o, const float *wt, const float *bias, int M, int N, int K, cudaStream_t s, P pre = P{})
{
    static_assert((BM / TM) * (BN / (4 * TV)) * KS % (BN / 4) == 0, "a thread's weight float4s keep their column");
    dim3 grid((M + BM - 1) / BM, N / BN);
    gemm_tile<BM, BN, TM, TV, KS, KC, RELU, A, O, P><<<grid, (BM / TM) * (BN / (4 * TV)) * KS, 0, s>>>(a, o, pre, wt, bias, M, N, K);
}

/* The bf16 path (NETH_NET_ACTOR_BF16): the same tiles on tensor cores, wmma's 16x16x16 bf16 products with fp32 sums
 * (the PyTorch actor's autocast: bf16 inputs and weights, fp32 accumulation). Each warp keeps one 16x16 output tile
 * over its slice of the chunk's 16-wide k steps (ks, ks + KS, ...); the KS partial tiles are summed in order through
 * shared memory as gemm_tile's are. wb: the weights in bf16, [k][n] as wt. */
template <int BM, int BN, int KS, int KC, bool RELU, class A, class O, class P>
__global__ void __launch_bounds__((BM / 16) * (BN / 16) * KS * 32)
gemm_bf16(A a, O o, P pre, const nt16 *__restrict__ wb, const float *__restrict__ bias, int M, int N, int K)
{
    namespace wm = nvcuda::wmma;
    constexpr int TW = (BM / 16) * (BN / 16), NT = TW * KS * 32, K4 = KC / 4, N4 = BN / 4;
    constexpr int AS = KC + 8, WS = BN + 8;   /* rows padded by 16 bytes (wmma's ldm, fewer bank conflicts) */
    constexpr int NA = BM * K4, NW = KC * N4, PA = (NA + NT - 1) / NT, PW = (NW + NT - 1) / NT;
    constexpr int AB = BM * AS * 2, WB = KC * WS * 2, RB = KS * BM * BN * 4;
    static_assert(KC % 16 == 0 && BM % 16 == 0 && BN % 16 == 0 && AB % 32 == 0 && NT % N4 == 0, "a tile's shape");
    __shared__ __align__(32) unsigned char sm[AB + WB > RB ? AB + WB : RB];
    nt16 *As = (nt16 *)sm, *Ws = (nt16 *)(sm + AB);
    float *red = (float *)sm;
    int m0 = blockIdx.x * BM, n0 = blockIdx.y * BN, t = threadIdx.x, w = t >> 5;
    int ks = w / TW, tw = w % TW, wm0 = tw / (BN / 16) * 16, wn0 = tw % (BN / 16) * 16;
    if (!blockIdx.y)
        pre(m0, BM, M, t, NT);
    const float *pa[PA];
    addr_a<PA, NT, NA, K4>(a, pa, m0, M, t);
    const int dA = step_a<A, KC>(a);
    const nt16 *pw = wb + t / N4 * N + n0 + t % N4 * 4;
    wm::fragment<wm::accumulator, 16, 16, 16, float> acc;
    wm::fill_fragment(acc, 0.f);
#if NETH_NET_ACC16
    /* (prec.cuh) the products accumulate in fp16, added into acc every NETH_NET_PROMOTE k-steps (NETH_NET_MMA 3) and at the end;
     * the two accumulator fragments hold their elements in the same order (sm_80's m16n8k16 C layout), which
     * actor_check holds */
    wm::fragment<wm::accumulator, 16, 16, 16, __half> acch;
    wm::fill_fragment(acch, __float2half(0.f));
    int kn = 0;
    auto promote = [&]() {
#pragma unroll
        for (int i = 0; i < acc.num_elements; i++)
            acc.x[i] += __half2float(acch.x[i]);
        wm::fill_fragment(acch, __float2half(0.f));
    };
#endif
    /* chunk c + 1's loads are in flight while chunk c is summed */
    float4 ra[PA];
    uint2 rw[PW];
    load_a<PA, NT, NA, K4>(a, ra, pa, 0, dA, m0, M, 0, t);
#pragma unroll
    for (int u = 0; u < PW; u++)
        if (NW % NT == 0 || t + u * NT < NW)
            rw[u] = *(const uint2 *)(pw + u * (NT / N4) * N);
    for (int c = 0, k0 = 0; k0 < K; c++, k0 += KC) {
        if (k0)
            __syncthreads();
#pragma unroll
        for (int u = 0; u < PA; u++) {
            int i = t + u * NT;
            if (NA % NT == 0 || i < NA) {
                nt162 lo = f22h2(ra[u].x, ra[u].y), hi = f22h2(ra[u].z, ra[u].w);
                uint2 v;
                v.x = *(unsigned *)&lo;
                v.y = *(unsigned *)&hi;
                *(uint2 *)(As + i / K4 * AS + i % K4 * 4) = v;
            }
        }
#pragma unroll
        for (int u = 0; u < PW; u++) {
            int i = t + u * NT;
            if (NW % NT == 0 || i < NW)
                *(uint2 *)(Ws + i / N4 * WS + i % N4 * 4) = rw[u];
        }
        __syncthreads();
        if (k0 + KC < K) {
            load_a<PA, NT, NA, K4>(a, ra, pa, c + 1, dA, m0, M, k0 + KC, t);
#pragma unroll
            for (int u = 0; u < PW; u++)
                if (NW % NT == 0 || t + u * NT < NW)
                    rw[u] = *(const uint2 *)(pw + (k0 + KC + u * (NT / N4)) * N);
        }
        for (int kk = ks * 16; kk < KC; kk += KS * 16) {
            wm::fragment<wm::matrix_a, 16, 16, 16, nt16, wm::row_major> fa;
            wm::fragment<wm::matrix_b, 16, 16, 16, nt16, wm::row_major> fb;
            wm::load_matrix_sync(fa, As + wm0 * AS + kk, AS);
            wm::load_matrix_sync(fb, Ws + kk * WS + wn0, WS);
#if NETH_NET_ACC16
            wm::mma_sync(acch, fa, fb, acch);
            if (NETH_NET_MMA == 3 && ++kn % NETH_NET_PROMOTE == 0)
                promote();
#else
            wm::mma_sync(acc, fa, fb, acc);
#endif
        }
    }
#if NETH_NET_ACC16
    promote();
#endif
    __syncthreads();
    wm::store_matrix_sync(red + (ks * BM + wm0) * BN + wn0, acc, BN, wm::mem_row_major);
    __syncthreads();
    for (int e = t; e < BM * BN; e += NT) {
        int m = e / BN, n = e % BN;
        if (m0 + m >= M)
            continue;
        float v = red[e];
#pragma unroll
        for (int s = 1; s < KS; s++)
            v += red[s * BM * BN + e];
        v += bias[n0 + n];
        put(o(m0 + m) + n0 + n, RELU ? fmaxf(v, 0.f) : v);
    }
}

template <int BM, int BN, int KS, int KC, bool RELU = true, class A, class O, class P = pre_none>
static void gemm16(A a, O o, const nt16 *wb, const float *bias, int M, int N, int K, cudaStream_t s,
                   P pre = P{})
{
    dim3 grid((M + BM - 1) / BM, N / BN);
    gemm_bf16<BM, BN, KS, KC, RELU, A, O, P><<<grid, (BM / 16) * (BN / 16) * KS * 32, 0, s>>>(a, o, pre, wb, bias, M, N, K);
}

/* The bf16 path's layers after conv1: bf16 activations (each producer's epilogue rounds them, relu then rounding the
 * same as rounding then relu) copied global to shared by cp.async, two chunks in flight, and summed by mma.sync
 * m16n8k16 (bf16 products, fp32 sums) from ldmatrix fragments. A warp keeps a 16x16 output tile (two m16n8 tiles)
 * over its slice of the chunk's 16-wide k steps (ks, ks + KS, ...); the KS partial tiles are summed in order through
 * shared memory as gemm_tile's are. a: an affine bf16 loader (row(m) + col(k), chunks whole kernel rows). */
__device__ inline void cp16(void *s, const void *g, bool valid)
{
    unsigned sa = (unsigned)__cvta_generic_to_shared(s);
    asm volatile("cp.async.cg.shared.global [%0], [%1], 16, %2;\n" ::"r"(sa), "l"(g), "r"(valid ? 16 : 0));
}
__device__ inline void cp_commit() { asm volatile("cp.async.commit_group;\n" ::); }
template <int N>
__device__ inline void cp_wait() { asm volatile("cp.async.wait_group %0;\n" ::"n"(N)); }

template <int BM, int BN, int KS, int KC, bool RELU, class A, class O, class P, int FM, int FN>
__global__ void __launch_bounds__((BM / (16 * FM)) * (BN / (16 * FN)) * KS * 32)
gemm_mma(A a, O o, P pre, const nt16 *__restrict__ wb, const float *__restrict__ bias, int M, int N, int K)
{
    /* a warp's tile is FM x FN 16x16 tiles (lane/ntwidth: a wider net's layers reuse each fragment FN or FM times) */
    constexpr int WM = 16 * FM, WN = 16 * FN;
    constexpr int TW = (BM / WM) * (BN / WN), NT = TW * KS * 32, K8 = KC / 8, N8 = BN / 8;
    static_assert(BM % WM == 0 && BN % WN == 0, "whole warp tiles");
    constexpr int AS = KC + 8, WS = BN + 8;   /* rows padded by 16 bytes: ldmatrix's eight rows in distinct banks */
    constexpr int STAGE = BM * AS + KC * WS;  /* bf16s a stage */
    constexpr int NA = BM * K8, NW = KC * N8, PA = (NA + NT - 1) / NT, PW = (NW + NT - 1) / NT;
    constexpr int SMB = 2 * STAGE * 2 > KS * BM * BN * 4 ? 2 * STAGE * 2 : KS * BM * BN * 4;
    static_assert(KC % 16 == 0 && BM % 16 == 0 && BN % 16 == 0, "a tile's shape");
    __shared__ __align__(128) unsigned char sm[SMB];
    nt16 *st = (nt16 *)sm;
    float *red = (float *)sm;
    int m0 = blockIdx.x * BM, n0 = blockIdx.y * BN, t = threadIdx.x, lane = t & 31, w = t >> 5;
    int ks = w / TW, tw = w % TW, wm0 = tw / (BN / WN) * WM, wn0 = tw % (BN / WN) * WN;
    if (!blockIdx.y)
        pre(m0, BM, M, t, NT);
    /* thread t's copies: A's u-th 16 bytes from ga[u] + a.col(c KC + its column) (any chunk: a chunk need not be whole
     * kernel rows, only 8 columns one cell's), W's from gw + u (NT / N8) N (+ c KC N) */
    const nt16 *ga[PA];
    int sa[PA];
#pragma unroll
    for (int u = 0; u < PA; u++) {
        int i = t + u * NT, mm = i / K8;
        ga[u] = (NA % NT == 0 || i < NA) && m0 + mm < M ? a.row(m0 + mm) : nullptr;
        sa[u] = mm * AS + i % K8 * 8;
    }
    const nt16 *gw = wb + t / N8 * N + n0 + t % N8 * 8;
    static_assert(NT % N8 == 0, "a thread's weight copies keep their column");
    auto issue = [&](int c) {
        nt16 *As = st + (c & 1) * STAGE, *Ws = As + BM * AS;
#pragma unroll
        for (int u = 0; u < PA; u++)
            if (NA % NT == 0 || t + u * NT < NA)
                cp16(As + sa[u], ga[u] ? ga[u] + a.col(c * KC + (t + u * NT) % K8 * 8) : wb, ga[u] != nullptr);
#pragma unroll
        for (int u = 0; u < PW; u++) {
            int i = t + u * NT;
            if (NW % NT == 0 || i < NW)
                cp16(Ws + i / N8 * WS + i % N8 * 8, gw + (c * KC + u * (NT / N8)) * N, true);
        }
        cp_commit();
    };
    acc4 acc[FM][2 * FN] = {};   /* prec.cuh's tiles */
    int kn = 0;
    const int nch = K / KC;
    issue(0);
    for (int c = 0; c < nch; c++) {
        if (c + 1 < nch) {
            issue(c + 1);
            cp_wait<1>();
        } else
            cp_wait<0>();
        __syncthreads();
        const nt16 *As = st + (c & 1) * STAGE, *Ws = As + BM * AS;
        for (int kk = ks * 16; kk < KC; kk += KS * 16) {
            unsigned af[FM][4], bfr[FN][4];
#pragma unroll
            for (int i = 0; i < FM; i++) {
                unsigned pa = (unsigned)__cvta_generic_to_shared(As + (wm0 + i * 16 + (lane & 15)) * AS + kk +
                                                                 (lane >> 4) * 8);
                asm volatile("ldmatrix.sync.aligned.m8n8.x4.shared.b16 {%0, %1, %2, %3}, [%4];\n"
                             : "=r"(af[i][0]), "=r"(af[i][1]), "=r"(af[i][2]), "=r"(af[i][3]) : "r"(pa));
            }
#pragma unroll
            for (int j = 0; j < FN; j++) {
                unsigned pb = (unsigned)__cvta_generic_to_shared(Ws + (kk + (lane & 15)) * WS + wn0 + j * 16 +
                                                                 (lane >> 4) * 8);
                asm volatile("ldmatrix.sync.aligned.m8n8.x4.trans.shared.b16 {%0, %1, %2, %3}, [%4];\n"
                             : "=r"(bfr[j][0]), "=r"(bfr[j][1]), "=r"(bfr[j][2]), "=r"(bfr[j][3]) : "r"(pb));
            }
#pragma unroll
            for (int i = 0; i < FM; i++)
#pragma unroll
                for (int j = 0; j < 2 * FN; j++)
                    mma16816(acc[i][j], af[i], &bfr[j / 2][(j & 1) * 2]);
            acc_step(acc, kn++);
        }
        __syncthreads();   /* the stage is free for chunk c + 2 */
    }
    acc_fin(acc);
    /* the m16n8 accumulators: rows lane / 4 (+ 8), columns lane % 4 * 2 (+ 1), each next n8 tile 8 columns on */
#pragma unroll
    for (int i = 0; i < FM; i++)
#pragma unroll
        for (int j = 0; j < 2 * FN; j++) {
            float *r = red + (ks * BM + wm0 + i * 16 + (lane >> 2)) * BN + wn0 + j * 8 + (lane & 3) * 2;
            r[0] = acc[i][j][0];
            r[1] = acc[i][j][1];
            r[8 * BN] = acc[i][j][2];
            r[8 * BN + 1] = acc[i][j][3];
        }
    __syncthreads();
    for (int e = t; e < BM * BN; e += NT) {
        int m = e / BN, n = e % BN;
        if (m0 + m >= M)
            continue;
        float v = red[e];
#pragma unroll
        for (int s = 1; s < KS; s++)
            v += red[s * BM * BN + e];
        v += bias[n0 + n];
        put(o(m0 + m) + n0 + n, RELU ? fmaxf(v, 0.f) : v);
    }
}

template <int BM, int BN, int KS, int KC, int FM = 1, int FN = 1, bool RELU = true, class A, class O,
          class P = pre_none>
static void gemmm(A a, O o, const nt16 *wb, const float *bias, int M, int N, int K, cudaStream_t s,
                  P pre = P{})
{
    dim3 grid((M + BM - 1) / BM, N / BN);
    gemm_mma<BM, BN, KS, KC, RELU, A, O, P, FM, FN>
        <<<grid, (BM / (16 * FM)) * (BN / (16 * FN)) * KS * 32, 0, s>>>(a, o, pre, wb, bias, M, N, K);
}

/* The same layers with the weights in registers: at load each layer's bf16 weights are reordered into mma.sync's B
 * fragment order (wf: for each 16-column tile and 16-row k step, lane l's four 32-bit registers, two m16n8 tiles,
 * contiguous), so a warp fetches every B fragment of its k slice with one 16-byte load a step, all issued at the
 * kernel's start beside one cp.async round for the whole K of the activation tile: one memory round trip a layer
 * (a round trip costs about 1 us at this size on the 3090, out/perf/ntactor.tsv). */
template <int BM, int BN, int KS, int K, bool RELU, class A, class O, class P>
__global__ void __launch_bounds__((BM / 16) * (BN / 16) * KS * 32)
gemm_mmr(A a, O o, P pre, const uint4 *__restrict__ wf, const float *__restrict__ bias, int M, int N)
{
    constexpr int TW = (BM / 16) * (BN / 16), NT = TW * KS * 32, K8 = K / 8, KST = K / 16, SPW = (KST + KS - 1) / KS;
    constexpr int AS = K + 8;
    constexpr int NA = BM * K8, PA = (NA + NT - 1) / NT;
    constexpr int SMB = BM * AS * 2 > KS * BM * BN * 4 ? BM * AS * 2 : KS * BM * BN * 4;
    static_assert(K % 16 == 0 && BM % 16 == 0 && BN % 16 == 0, "a tile's shape");
    __shared__ __align__(128) unsigned char sm[SMB];
    nt16 *As = (nt16 *)sm;
    float *red = (float *)sm;
    int m0 = blockIdx.x * BM, n0 = blockIdx.y * BN, t = threadIdx.x, lane = t & 31, w = t >> 5;
    int ks = w / TW, tw = w % TW, wm0 = tw / (BN / 16) * 16, wn0 = tw % (BN / 16) * 16;
    if (!blockIdx.y)
        pre(m0, BM, M, t, NT);
#pragma unroll
    for (int u = 0; u < PA; u++) {
        int i = t + u * NT, mm = i / K8;
        if (NA % NT == 0 || i < NA) {
            bool valid = m0 + mm < M;
            cp16(As + mm * AS + i % K8 * 8, valid ? a.row(m0 + mm) + a.col(i % K8 * 8) : a.row(0), valid);
        }
    }
    cp_commit();
    uint4 bfr[SPW];
    const uint4 *wp = wf + (size_t)((n0 + wn0) >> 4) * KST * 32 + lane;
#pragma unroll
    for (int i = 0; i < SPW; i++)
        if (ks + i * KS < KST)
            bfr[i] = wp[(ks + i * KS) * 32];
    cp_wait<0>();
    __syncthreads();
    acc4 acc[2] = {};   /* prec.cuh's tiles */
#pragma unroll
    for (int i = 0; i < SPW; i++) {
        int s = ks + i * KS;
        if (s < KST) {
            unsigned a[4];
            unsigned pa = (unsigned)__cvta_generic_to_shared(As + (wm0 + (lane & 15)) * AS + s * 16 + (lane >> 4) * 8);
            asm volatile("ldmatrix.sync.aligned.m8n8.x4.shared.b16 {%0, %1, %2, %3}, [%4];\n"
                         : "=r"(a[0]), "=r"(a[1]), "=r"(a[2]), "=r"(a[3]) : "r"(pa));
            const unsigned b0[2] = {bfr[i].x, bfr[i].y}, b1[2] = {bfr[i].z, bfr[i].w};
            mma16816(acc[0], a, b0);
            mma16816(acc[1], a, b1);
            acc_step(acc, i);
        }
    }
    acc_fin(acc);
    __syncthreads();
#pragma unroll
    for (int j = 0; j < 2; j++) {
        float *r = red + (ks * BM + wm0 + (lane >> 2)) * BN + wn0 + j * 8 + (lane & 3) * 2;
        r[0] = acc[j][0];
        r[1] = acc[j][1];
        r[8 * BN] = acc[j][2];
        r[8 * BN + 1] = acc[j][3];
    }
    __syncthreads();
    for (int e = t; e < BM * BN; e += NT) {
        int m = e / BN, n = e % BN;
        if (m0 + m >= M)
            continue;
        float v = red[e];
#pragma unroll
        for (int s = 1; s < KS; s++)
            v += red[s * BM * BN + e];
        v += bias[n0 + n];
        put(o(m0 + m) + n0 + n, RELU ? fmaxf(v, 0.f) : v);
    }
}

template <int BM, int BN, int KS, int K, bool RELU = true, class A, class O, class P = pre_none>
static void gemmr(A a, O o, const uint4 *wf, const float *bias, int M, int N, cudaStream_t s, P pre = P{})
{
    dim3 grid((M + BM - 1) / BM, N / BN);
    gemm_mmr<BM, BN, KS, K, RELU, A, O, P><<<grid, (BM / 16) * (BN / 16) * KS * 32, 0, s>>>(a, o, pre, wf, bias, M, N);
}

/* A bf16 layer after the first (lane/ntwidth): gemm_mmr when its K fits it (at most 16 B fragments a warp in
 * registers, the tile's whole K in 48 KB of shared memory), else gemm_mma over chunks of KC (the largest multiple of
 * 16 to 256 that divides K and whose two stages fit 48 KB): a wider net's conv2, conv3 and fc layers. FM x FN above
 * 1 x 1 (a warp's tile in 16x16 tiles) always takes gemm_mma: the wide variants' larger tiles. */
template <int BM, int BN, int KS, int K>
constexpr bool mmr_fits() { return (K / 16 + KS - 1) / KS <= 16 && BM * (K + 8) * 2 <= 48 * 1024; }
template <int BM, int BN, int K>
constexpr int mma_kc()
{
    for (int kc = 256; kc >= 16; kc -= 16)
        if (K % kc == 0 && 2 * (BM * (kc + 8) + kc * (BN + 8)) * 2 <= 48 * 1024)
            return kc;
    return 0;
}
template <int BM, int BN, int KS, int K, int FM = 1, int FN = 1, bool RELU = true, class A, class O,
          class P = pre_none>
static void gemmb(A a, O o, const uint4 *wf, const nt16 *wb, const float *bias, int M, int N, cudaStream_t s,
                  P pre = P{})
{
    if constexpr (FM * FN == 1 && mmr_fits<BM, BN, KS, K>())
        gemmr<BM, BN, KS, K, RELU>(a, o, wf, bias, M, N, s, pre);
    else {
        static_assert(mma_kc<BM, BN, K>() > 0, "no chunk of K fits");
        gemmm<BM, BN, KS, mma_kc<BM, BN, K>(), FM, FN, RELU>(a, o, wb, bias, M, N, K, s, pre);
    }
}

/* W [k][n] bf16 (rows of N) into gemm_mmr's fragment order: for each 16-column tile j and 16-row step s, lane l's
 * registers {W[k][na], W[k+1][na]}, {W[k+8][na], W[k+9][na]} and the same for nb = na + 8, k = 16 s + 2 (l % 4),
 * na = 16 j + l / 4 (the first element in the low half) */
template <class E>   /* an element a bf16 slot: the weights (bf16), or neth_actor_reload's map (int32_t) */
static void frag(E *dst, const E *W, int K, int N)
{
    for (int j = 0; j < N / 16; j++)
        for (int s = 0; s < K / 16; s++)
            for (int l = 0; l < 32; l++) {
                int k = s * 16 + (l & 3) * 2, na = j * 16 + (l >> 2), nb = na + 8;
                E *d = dst + ((size_t)(j * (K / 16) + s) * 32 + l) * 8;
                const int kk[4] = {k, k + 1, k + 8, k + 9};
                for (int q = 0; q < 4; q++) {
                    d[q] = W[(size_t)kk[q] * N + na];
                    d[4 + q] = W[(size_t)kk[q] * N + nb];
                }
            }
}

/* ---- the sampler ---- */

/* Philox4x32-10 */
__device__ static uint4 philox(uint4 c, uint2 k)
{
#pragma unroll
    for (int i = 0; i < 10; i++) {
        unsigned lo0 = 0xD2511F53u * c.x, hi0 = __umulhi(0xD2511F53u, c.x);
        unsigned lo1 = 0xCD9E8D57u * c.z, hi1 = __umulhi(0xCD9E8D57u, c.z);
        c = make_uint4(hi1 ^ c.y ^ k.x, lo1, hi0 ^ c.w ^ k.y, lo0);
        k.x += 0x9E3779B9u;
        k.y += 0xBB67AE85u;
    }
    return c;
}

/* the device RNG's uniform in (0, 1) for sample b's noise element e (the camera's 0..81, then the keys') */
__device__ static float uniform(const unsigned long long *rng, int b, int e)
{
    unsigned long long seed = rng[0], ctr = rng[1];
    uint4 r = philox(make_uint4((unsigned)e >> 2, (unsigned)b, (unsigned)ctr, (unsigned)(ctr >> 32)),
                     make_uint2((unsigned)seed, (unsigned)(seed >> 32)));
    unsigned v = (e & 3) == 0 ? r.x : (e & 3) == 1 ? r.y : (e & 3) == 2 ? r.z : r.w;
    return ((v >> 8) + 0.5f) * (1.0f / 16777216);
}

/* torch's log_sigmoid: min(x, 0) - log1p(exp(-|x|)) */
__device__ static float logsig(float x) { return fminf(x, 0.f) - log1pf(expf(-fabsf(x))); }

#define HB 4                /* samples a block */
#define HK 16               /* slices of k */
#define HQ (NETH_NET_HP / 4)      /* the heads' column groups of four */
#define HTH (HK * HQ)       /* threads a block */
#define HC 256              /* k a chunk (z2 staged and the weights loaded a chunk at a time: NETH_NET_D / HC chunks) */

/* the heads over z2 and each sample's draw, a block for HB samples. Thread (ks, q) sums columns 4q..4q+3 for the
 * block's samples over k = ks, ks + HK, ... (a chunk's 16 weight float4s loaded at once; bf16: four bf16s, with z2 rounded
 * to bf16 as a tensor core's inputs are) and the HK partials are summed in order. Then three warps a sample draw:
 * camera head 0 and 1 by Gumbel-max (argmax of logit - log(-log u), the first on a tie, as torch's argmax) with
 * their log-softmax terms, and the keys as u < sigmoid(logit) with each one's log-sigmoid of its sign; logp is
 * (camera 0 + camera 1) + keys (semteach.logp). The device RNG's uniforms are drawn first, beside the loads. */
template <bool BF>
__global__ void __launch_bounds__(HTH)
heads_draw(const float *__restrict__ z2, const void *__restrict__ hw, const float *__restrict__ hb, int B,
           neth_actor_io io, const unsigned long long *__restrict__ rng)
{
    __shared__ __align__(16) float zs[HB][HC];
    __shared__ __align__(16) float part[HK][HB][NETH_NET_HP];
    __shared__ float lpp[HB][3 + NETH_NET_HOTBAR];
    int b0 = blockIdx.x * HB, ns = min(HB, B - b0), t = threadIdx.x, q = t % HQ, ks = t / HQ;
    int lane = t & 31, w = t >> 5, s = w / 3, h = w % 3, b = b0 + s;   /* the drawing warps: w < 3 ns */
    bool drawer = w < 3 * ns;
    /* NETH_NET_HOTBAR: the keys' warp's lanes 16 .. 25 draw the hotbar (its noise elements after the keys') */
    bool hlane = NETH_NET_HOTBAR && h == NETH_NET_CAM && lane >= 16 && lane < 16 + NETH_NET_HOT;
    float u0 = 0.5f, u1 = 0.5f;
    if (drawer) {
        int e0 = h < NETH_NET_CAM ? h * NETH_NET_BINS + lane : hlane ? NETH_NET_HOFF + lane - 16 : NETH_NET_CAM * NETH_NET_BINS + lane,
            e1 = h * NETH_NET_BINS + lane + 32;
        bool has0 = h < NETH_NET_CAM || lane < NETH_NET_KEYS || hlane, has1 = h < NETH_NET_CAM && lane + 32 < NETH_NET_BINS;
        if (io.ucam) {
            if (has0)
                u0 = h < NETH_NET_CAM ? io.ucam[b * NETH_NET_CAM * NETH_NET_BINS + e0]
                     : hlane    ? (io.uhot ? io.uhot[b * NETH_NET_HOT + lane - 16] : uniform(rng, b, e0))
                                : io.ukey[b * NETH_NET_KEYS + lane];
            if (has1)
                u1 = io.ucam[b * NETH_NET_CAM * NETH_NET_BINS + e1];
        } else {
            if (has0)
                u0 = uniform(rng, b, e0);
            if (has1)
                u1 = uniform(rng, b, e1);
        }
        /* NETH_NET_HOTBAR: uniform's top value rounds to 1.0f (2^-24 an element), whose -log(-log u) is +inf: that category
         * then wins whatever its logit (a teacher whose slots sit at -1e4 switched twice in 4M steps); the hotbar's
         * kept under 1 (the camera's as they were: changing them moves every default trajectory) */
        if (hlane)
            u0 = fminf(u0, 0x1.fffffep-1f);
#if NETH_ACTOR_GREEDY
        /* lane/ladder2 (the .gk and .gc builds, a frozen policy's eval only: ntloop refuses learning with them): one
         * uniform for every key (u 0.5: a key is held when its logit is over 0) and every camera bin (one Gumbel
         * offset for all bins: the argmax logit); the hotbar stays sampled */
        if ((NETH_ACTOR_GREEDY & 1) && h == NETH_NET_CAM && !hlane)
            u0 = 0.5f;
        if ((NETH_ACTOR_GREEDY & 2) && h < NETH_NET_CAM)
            u0 = u1 = 0.5f;
#endif
    }
    float acc[HB][4];
#pragma unroll
    for (int j = 0; j < HB; j++)
        acc[j][0] = acc[j][1] = acc[j][2] = acc[j][3] = 0.f;
    /* k in chunks of HC (one at wm 1): z2's chunk staged, each thread's HC / HK weight float4s loaded at once */
    for (int k0 = 0; k0 < NETH_NET_D; k0 += HC) {
        if (k0)
            __syncthreads();
        for (int i = t; i < HB * HC; i += HTH) {
            float v = i / HC < ns ? z2[(b0 + i / HC) * NETH_NET_D + k0 + i % HC] : 0.f;
            zs[i / HC][i % HC] = BF ? h2f(f2h(v)) : v;
        }
        float4 wv[HC / HK];
#pragma unroll
        for (int i = 0; i < HC / HK; i++) {
            int k = k0 + ks + i * HK;
            if constexpr (BF) {
                uint2 r = *(const uint2 *)((const nt16 *)hw + k * NETH_NET_HP + q * 4);
                float2 lo = h22f2(*(nt162 *)&r.x), hi = h22f2(*(nt162 *)&r.y);
                wv[i] = make_float4(lo.x, lo.y, hi.x, hi.y);
            } else
                wv[i] = *(const float4 *)((const float *)hw + k * NETH_NET_HP + q * 4);
        }
        __syncthreads();
#pragma unroll
        for (int i = 0; i < HC / HK; i++) {
#pragma unroll
            for (int j = 0; j < HB; j++) {
                float z = zs[j][ks + i * HK];
                acc[j][0] = fmaf(z, wv[i].x, acc[j][0]);
                acc[j][1] = fmaf(z, wv[i].y, acc[j][1]);
                acc[j][2] = fmaf(z, wv[i].z, acc[j][2]);
                acc[j][3] = fmaf(z, wv[i].w, acc[j][3]);
            }
        }
    }
#pragma unroll
    for (int j = 0; j < HB; j++)
        *(float4 *)&part[ks][j][q * 4] = make_float4(acc[j][0], acc[j][1], acc[j][2], acc[j][3]);
    __syncthreads();
    for (int i = t; i < HB * NETH_NET_HP; i += HTH) {
        int j = i / NETH_NET_HP, c = i % NETH_NET_HP;
        float v = part[0][j][c];
#pragma unroll
        for (int k = 1; k < HK; k++)
            v += part[k][j][c];
        part[0][j][c] = v + hb[c];
    }
    __syncthreads();
    if (drawer) {
        const float *l = part[0][s];
        float lp;
        if (h < NETH_NET_CAM) {
            const float *lh = l + h * NETH_NET_BINS;
            bool has1 = lane + 32 < NETH_NET_BINS;
            float x0 = lh[lane], x1 = has1 ? lh[lane + 32] : -INFINITY;
#if NETH_ACTOR_TEMP
            /* lane/rawpix: the draw at temperature NETH_ACTOR_TEMP / 10 (the log-softmax term below stays the logits') */
            const float itemp = 10.0f / (float)NETH_ACTOR_TEMP;
#else
            const float itemp = 1.0f;
#endif
            float g0 = (NETH_ACTOR_TEMP ? x0 * itemp : x0) - logf(-logf(u0)), gb = g0, mx = fmaxf(x0, x1);
            int gi = lane;
            if (has1) {
                float g1 = (NETH_ACTOR_TEMP ? x1 * itemp : x1) - logf(-logf(u1));
                if (g1 > gb) {
                    gb = g1;
                    gi = lane + 32;
                }
            }
#pragma unroll
            for (int o = 16; o > 0; o >>= 1) {
                float og = __shfl_xor_sync(0xffffffffu, gb, o);
                int oi = __shfl_xor_sync(0xffffffffu, gi, o);
                if (og > gb || (og == gb && oi < gi)) {
                    gb = og;
                    gi = oi;
                }
                mx = fmaxf(mx, __shfl_xor_sync(0xffffffffu, mx, o));
            }
            float se = expf(x0 - mx) + (has1 ? expf(x1 - mx) : 0.f);
#pragma unroll
            for (int o = 16; o > 0; o >>= 1)
                se += __shfl_xor_sync(0xffffffffu, se, o);
            lp = (lh[gi] - mx) - logf(se);
            if (lane == 0)
                io.act[b * NETH_NET_ACTION + h] = gi;
            if (io.cam)
                for (int j = lane; j < NETH_NET_BINS; j += 32)
                    io.cam[b * NETH_NET_CAM * NETH_NET_BINS + h * NETH_NET_BINS + j] = lh[j];
        } else {
            float lk = 0.f;
            if (lane < NETH_NET_KEYS) {
                float x = l[NETH_NET_CAM * NETH_NET_BINS + lane];
                int key = u0 < 1.f / (1.f + expf(NETH_ACTOR_TEMP ? -x * (10.0f / (float)(NETH_ACTOR_TEMP + !NETH_ACTOR_TEMP)) : -x));
                lk = key ? logsig(x) : logsig(-x);
                io.act[b * NETH_NET_ACTION + NETH_NET_CAM + lane] = key;
                if (io.keys)
                    io.keys[b * NETH_NET_KEYS + lane] = x;
            }
#pragma unroll
            for (int o = 16; o > 0; o >>= 1)
                lk += __shfl_xor_sync(0xffffffffu, lk, o);
            lp = lk;
            if (lane == 0)
                io.value[b] = l[NETH_NET_HEADS - 1];
#if NETH_NET_HOTBAR
            /* the hotbar as a camera head over lanes 16 .. 25 (the half warp's xor tree: lanes 0 .. 15 reduce among
             * themselves, unused): Gumbel-max, first on a tie, and its log-softmax term */
            float xh = hlane ? l[NETH_NET_HOFF + lane - 16] : -INFINITY, gh = hlane ? xh - logf(-logf(u0)) : -INFINITY,
                  mh = xh;
            int hi = lane - 16;
#pragma unroll
            for (int o = 8; o > 0; o >>= 1) {
                float og = __shfl_xor_sync(0xffffffffu, gh, o);
                int oi = __shfl_xor_sync(0xffffffffu, hi, o);
                if (og > gh || (og == gh && oi < hi)) {
                    gh = og;
                    hi = oi;
                }
                mh = fmaxf(mh, __shfl_xor_sync(0xffffffffu, mh, o));
            }
            float sh = hlane ? expf(xh - mh) : 0.f;
#pragma unroll
            for (int o = 8; o > 0; o >>= 1)
                sh += __shfl_xor_sync(0xffffffffu, sh, o);
            if (lane == 16) {
                lpp[s][3] = (l[NETH_NET_HOFF + hi] - mh) - logf(sh);
                io.act[b * NETH_NET_ACTION + NETH_NET_CAM + NETH_NET_KEYS] = hi;
            }
            if (io.hot && hlane)
                io.hot[b * NETH_NET_HOT + lane - 16] = xh;
#endif
        }
        if (lane == 0)
            lpp[s][h] = lp;
    }
    __syncthreads();
    if (t < ns)
#if NETH_NET_HOTBAR
        io.logp[b0 + t] = ((lpp[t][0] + lpp[t][1]) + lpp[t][2]) + lpp[t][3];
#else
        io.logp[b0 + t] = (lpp[t][0] + lpp[t][1]) + lpp[t][2];
#endif
}

/* NETH_ACTOR_FUSE (lane/ntfc, actorfused.cuh): 1 the bf16 step as one kernel, at 16x16 wm 1 (the other variants keep a
 * kernel a layer); 0 a kernel a layer */
#ifndef NETH_ACTOR_FUSE
#define NETH_ACTOR_FUSE 0
#endif
#if NETH_ACTOR_FUSE && (NETH_NET_STEM || NETH_NET_WM != 1 || NETH_NET_HOTBAR || NETH_ACTOR_GREEDY || NETH_ACTOR_TEMP)   /* the hotbar's draw is heads_draw's alone;
                                                                      * lane/ladder2: so is the greedy one */
#undef NETH_ACTOR_FUSE
#define NETH_ACTOR_FUSE 0
#endif
#if NETH_ACTOR_FUSE
#include "actorfused.cuh"
#endif
#define NETH_ACTOR_RNGF (NETH_ACTOR_FUSE ? 8 : 4)     /* the workspace's first floats: the RNG's seed, counter (and k_afused's ticket) */

__global__ void rng_set(unsigned long long *rng, unsigned long long seed, unsigned long long ctr)
{
    rng[0] = seed;
    rng[1] = ctr;
}

/* ---- loading ---- */

struct blob {
    const unsigned char *p;
    size_t len, off;
};

static const float *take(blob *b, int rank, int d0, int d1, int d2, int d3, const char **err)
{
    int dims[4] = {d0, d1, d2, d3}, r;
    if (b->off + 4 > b->len)
        return *err = "ntw: truncated", nullptr;
    memcpy(&r, b->p + b->off, 4);
    b->off += 4;
    if (r != rank)
        return *err = "ntw: a tensor's rank is not net.h's", nullptr;
    size_t n = 1;
    for (int i = 0; i < rank; i++) {
        int d;
        if (b->off + 4 > b->len)
            return *err = "ntw: truncated", nullptr;
        memcpy(&d, b->p + b->off, 4);
        b->off += 4;
        if (d != dims[i])
            return *err = "ntw: a tensor's shape is not net.h's", nullptr;
        n *= d;
    }
    if (b->off + 4 * n > b->len)
        return *err = "ntw: truncated", nullptr;
    const float *f = (const float *)(b->p + b->off);
    b->off += 4 * n;
    return f;
}

/* conv [out][in][kh][kw] -> [k][out], k = (kh * KW + kw) * in + ci */
template <class E>
static void conv_t(E *dst, const E *w, int out, int in, int kh, int kw)
{
    for (int o = 0; o < out; o++)
        for (int c = 0; c < in; c++)
            for (int y = 0; y < kh; y++)
                for (int x = 0; x < kw; x++)
                    dst[((y * kw + x) * in + c) * out + o] = w[((o * in + c) * kh + y) * kw + x];
}

/* the device image: every tensor in its kernel's layout, packed (every piece a multiple of 8 elements: 16-byte
 * aligned in bf16 too), its pieces' offsets in elements; then memf, the bf16 layers' fragments */
struct img_lay {
    size_t w0, b0, w1, b1, w2, b2, w3, b3, f1, fb1, f2, fb2, hw, hb, rw, rb, tot;
    size_t n1f, n2, n3, nf1, nf2, nfr;   /* memf's pieces: conv1's (only after the stem: from the bytes it is
                                          * gemm_bf16's), conv2's, conv3's, fc1's, fc2's */
};
static img_lay img_layout()
{
    img_lay L;
    const size_t n0 = NETH_NET_STEM ? 48 * NETH_NET_C0 : 0, nb0 = NETH_NET_STEM ? NETH_NET_C0 : 0, n1 = K1 * NETH_NET_C1;
    L.n2 = 16 * NETH_NET_C1 * NETH_NET_C2, L.n3 = 9 * NETH_NET_C2 * NETH_NET_C3, L.nf1 = F1K * NETH_NET_D, L.nf2 = NETH_NET_D * NETH_NET_D;
    size_t p = 0;
    L.w0 = p, p += n0;
    L.b0 = p, p += nb0;
    L.w1 = p, p += n1;
    L.b1 = p, p += NETH_NET_C1;
    L.w2 = p, p += L.n2;
    L.b2 = p, p += NETH_NET_C2;
    L.w3 = p, p += L.n3;
    L.b3 = p, p += NETH_NET_C3;
    L.f1 = p, p += L.nf1;
    L.fb1 = p, p += NETH_NET_D;
    L.f2 = p, p += L.nf2;
    L.fb2 = p, p += NETH_NET_D;
    L.hw = p, p += NETH_NET_D * NETH_NET_HP;
    L.hb = p, p += NETH_NET_HP;
    L.rw = p, p += NETH_NET_RNN ? 2 * NETH_NET_D * NETH_NET_D : 0;   /* lane/ntgru: the minGRU's [k][n], n the 2D gates */
    L.rb = p, p += NETH_NET_RNN ? 2 * NETH_NET_D : 0;
    L.tot = p;
    L.n1f = NETH_NET_STEM ? n1 : 0;
    L.nfr = L.n1f + L.n2 + L.n3 + L.nf1 + L.nf2;
    return L;
}

/* net.h's tensors (t, in NETH_NET_ORDER) into the image h (zeroed): every slot a copy of one weight, or left zero. E the
 * weights (float), or neth_actor_reload's indices (int32_t) */
template <class E>
static void place(const E *const t[NETH_NET_TENSORS], E *h, const img_lay &L)
{
    int i0 = NETH_NET_STEM ? 2 : 0;   /* the stem's two tensors first */
    const E *s0w = t[0], *s0b = t[1], *c1w = t[i0], *c1b = t[i0 + 1], *c2w = t[i0 + 2], *c2b = t[i0 + 3],
            *c3w = t[i0 + 4], *c3b = t[i0 + 5], *f1w = t[i0 + 6], *f1b = t[i0 + 7], *f2w = t[i0 + 8],
            *f2b = t[i0 + 9], *cw = t[i0 + 10], *cb = t[i0 + 11], *kw = t[i0 + 12], *kb = t[i0 + 13],
            *vw = t[i0 + 14], *vb = t[i0 + 15];
#if NETH_NET_HOTBAR
    const E *hw = t[i0 + 16], *hbb = t[i0 + 17];
#endif
    auto cp = [](E *d, const E *s, int n) {
        for (int i = 0; i < n; i++)
            d[i] = s[i];
    };
    if (NETH_NET_STEM) {
        conv_t(h + L.w0, s0w, NETH_NET_C0, NETH_NET_C, 4, 4);
        cp(h + L.b0, s0b, NETH_NET_C0);
    }
    conv_t(h + L.w1, c1w, NETH_NET_C1, NETH_NET_CI, 4, 4);
    cp(h + L.b1, c1b, NETH_NET_C1);
    conv_t(h + L.w2, c2w, NETH_NET_C2, NETH_NET_C1, 4, 4);
    cp(h + L.b2, c2b, NETH_NET_C2);
    conv_t(h + L.w3, c3w, NETH_NET_C3, NETH_NET_C2, 3, 3);
    cp(h + L.b3, c3b, NETH_NET_C3);
    E *p = h + L.f1;
    for (int n = 0; n < NETH_NET_D; n++) {
        for (int c = 0; c < NETH_NET_C3; c++)
            for (int q = 0; q < Q3; q++)          /* net.h's flatten c * Q3 + q against ours q * C3 + c */
                p[(q * NETH_NET_C3 + c) * NETH_NET_D + n] = f1w[n * NETH_NET_FLAT + c * Q3 + q];
        for (int k = NETH_NET_C3 * Q3; k < NETH_NET_FLAT; k++)
            p[k * NETH_NET_D + n] = f1w[n * NETH_NET_FLAT + k];
    }
    cp(h + L.fb1, f1b, NETH_NET_D);
    p = h + L.f2;
    for (int n = 0; n < NETH_NET_D; n++)
        for (int k = 0; k < NETH_NET_D; k++)
            p[k * NETH_NET_D + n] = f2w[n * NETH_NET_D + k];
    cp(h + L.fb2, f2b, NETH_NET_D);
    p = h + L.hw;
    for (int k = 0; k < NETH_NET_D; k++) {
        for (int j = 0; j < NETH_NET_CAM * NETH_NET_BINS; j++)
            p[k * NETH_NET_HP + j] = cw[j * NETH_NET_D + k];
        for (int j = 0; j < NETH_NET_KEYS; j++)
            p[k * NETH_NET_HP + NETH_NET_CAM * NETH_NET_BINS + j] = kw[j * NETH_NET_D + k];
#if NETH_NET_HOTBAR
        for (int j = 0; j < NETH_NET_HOT; j++)
            p[k * NETH_NET_HP + NETH_NET_HOFF + j] = hw[j * NETH_NET_D + k];
#endif
        p[k * NETH_NET_HP + NETH_NET_HEADS - 1] = vw[k];
    }
    p = h + L.hb;
    cp(p, cb, NETH_NET_CAM * NETH_NET_BINS);
    cp(p + NETH_NET_CAM * NETH_NET_BINS, kb, NETH_NET_KEYS);
#if NETH_NET_HOTBAR
    cp(p + NETH_NET_HOFF, hbb, NETH_NET_HOT);
#endif
    p[NETH_NET_HEADS - 1] = vb[0];
#if NETH_NET_RNN
    const E *rw = t[i0 + 16 + 2 * NETH_NET_HOTBAR], *rb = t[i0 + 17 + 2 * NETH_NET_HOTBAR];     /* rnn_w [2D][D] as [k][n] */
    p = h + L.rw;
    for (int n = 0; n < 2 * NETH_NET_D; n++)
        for (int k = 0; k < NETH_NET_D; k++)
            p[k * 2 * NETH_NET_D + n] = rw[n * NETH_NET_D + k];
    cp(h + L.rb, rb, 2 * NETH_NET_D);
#endif
}

/* the image's layers (h, in its bf16 copy or as indices) into memf's fragments */
template <class E>
static void frags(E *fr, const E *h, const img_lay &L)
{
    if (L.n1f)
        frag(fr, h + L.w1, K1, NETH_NET_C1);
    frag(fr + L.n1f, h + L.w2, 16 * NETH_NET_C1, NETH_NET_C2);
    frag(fr + L.n1f + L.n2, h + L.w3, 9 * NETH_NET_C2, NETH_NET_C3);
    frag(fr + L.n1f + L.n2 + L.n3, h + L.f1, F1K, NETH_NET_D);
    frag(fr + L.n1f + L.n2 + L.n3 + L.nf1, h + L.f2, NETH_NET_D, NETH_NET_D);
}

extern "C" neth_actor *neth_actor_load_blob(const void *data, size_t len, const char **err)
{
    const char *e0;
    if (!err)
        err = &e0;
    blob b = {(const unsigned char *)data, len, 0};
    int cnt;
    if (len < 12 || memcmp(data, "NTW1\0\0\0\0", 8))
        return *err = "ntw: not an NTW1 blob", nullptr;
    memcpy(&cnt, b.p + 8, 4);
    b.off = 12;
    if (cnt != NETH_NET_TENSORS)
        return *err = "ntw: tensor count is not net.h's", nullptr;
    const float *t[NETH_NET_TENSORS];
    int i = 0;
#define NETH_NET_TAKE(name, rank, d0, d1, d2, d3)                  \
    if (!(t[i++] = take(&b, rank, d0, d1, d2, d3, err)))     \
        return nullptr;
    NETH_NET_ORDER(NETH_NET_TAKE)
#undef NETH_NET_TAKE
    if (b.off != len)
        return *err = "ntw: trailing bytes", nullptr;
    const img_lay L = img_layout();
    /* the image, and neth_actor_reload's map made by the same placement over each weight's learner index + 1 (0 a
     * zero slot) */
    float *h = (float *)calloc(L.tot, sizeof(float));
    place(t, h, L);
    int32_t *li = (int32_t *)malloc(NETH_LEARN_NPARAM * sizeof(int32_t)), *hi = (int32_t *)calloc(L.tot, sizeof(int32_t));
    const int32_t *ti[NETH_NET_TENSORS];
#define NETH_NET_LI(name, i, k)                                         \
    for (int j = 0; j < (k); j++) li[NETH_LEARN_OFF(name) + j] = NETH_LEARN_OFF(name) + j + 1; \
    ti[i] = li + NETH_LEARN_OFF(name);
    NETH_LEARN_LAYOUT(NETH_NET_LI)
#undef NETH_NET_LI
    place(ti, hi, L);

    /* the bf16 path's copy of the same image (its biases unused: they stay fp32) and the layers' fragments (each
     * fragment blob holds its layer's weights once) */
    nt16 *h16 = (nt16 *)malloc(L.tot * sizeof(nt16)), *fr = (nt16 *)malloc(L.nfr * 2);
    for (size_t i = 0; i < L.tot; i++)
        h16[i] = f2h(h[i]);
    int32_t *map = (int32_t *)malloc((L.tot + L.nfr) * sizeof(int32_t));
    for (size_t i = 0; i < L.tot; i++)
        map[i] = hi[i];
    frags(fr, h16, L);
    frags(map + L.tot, hi, L);
    for (size_t i = 0; i < L.tot + L.nfr; i++)
        map[i] -= 1;
    neth_actor *a = (neth_actor *)calloc(1, sizeof *a);
    a->tot = L.tot;
    a->nfr = L.nfr;
    const char *e = nullptr;
    if (cudaMalloc(&a->mem, L.tot * sizeof(float)) != cudaSuccess ||
        cudaMemcpy(a->mem, h, L.tot * sizeof(float), cudaMemcpyHostToDevice) != cudaSuccess ||
        cudaMalloc(&a->mem16, L.tot * sizeof(nt16)) != cudaSuccess ||
        cudaMemcpy(a->mem16, h16, L.tot * sizeof(nt16), cudaMemcpyHostToDevice) != cudaSuccess ||
        cudaMalloc(&a->memf, L.nfr * 2) != cudaSuccess ||
        cudaMemcpy(a->memf, fr, L.nfr * 2, cudaMemcpyHostToDevice) != cudaSuccess ||
        cudaMalloc(&a->map, (L.tot + L.nfr) * sizeof(int32_t)) != cudaSuccess ||
        cudaMemcpy(a->map, map, (L.tot + L.nfr) * sizeof(int32_t), cudaMemcpyHostToDevice) != cudaSuccess)
        e = "ntw: device allocation or copy failed";
    free(h);
    free(h16);
    free(fr);
    free(li);
    free(hi);
    free(map);
    if (e) {
        neth_actor_free(a);
        return *err = e, nullptr;
    }
#define DEV(x) (a->mem + L.x)
    a->w0 = DEV(w0); a->b0 = DEV(b0);
    a->w1 = DEV(w1); a->b1 = DEV(b1); a->w2 = DEV(w2); a->b2 = DEV(b2); a->w3 = DEV(w3); a->b3 = DEV(b3);
    a->f1 = DEV(f1); a->fb1 = DEV(fb1); a->f2 = DEV(f2); a->fb2 = DEV(fb2); a->hw = DEV(hw); a->hb = DEV(hb);
    a->rw = DEV(rw); a->rb = DEV(rb);
#undef DEV
#define DEV16(x) (a->mem16 + L.x)
    a->w0h = DEV16(w0); a->w1h = DEV16(w1); a->w2h = DEV16(w2); a->w3h = DEV16(w3); a->f1h = DEV16(f1); a->f2h = DEV16(f2);
    a->hwh = DEV16(hw);
    a->rwh = DEV16(rw);
#undef DEV16
    a->wf1c = a->memf;
    a->wf2 = a->wf1c + L.n1f / 8;
    a->wf3 = a->wf2 + L.n2 / 8;
    a->wf1 = a->wf3 + L.n3 / 8;
    a->wff2 = a->wf1 + L.nf1 / 8;
    return a;
}

/* the learner's weights (learn.h's layout, on the device) into the image: one gather a slot, mem16 and memf the
 * bf16 roundings, as neth_actor_load_blob makes them */
__global__ void actor_reload_k(const float *__restrict__ w, const int32_t *__restrict__ map, size_t tot, size_t nfr,
                               float *mem, nt16 *mem16, nt16 *memf)
{
    for (size_t i = blockIdx.x * (size_t)blockDim.x + threadIdx.x; i < tot + nfr; i += (size_t)gridDim.x * blockDim.x) {
        int k = map[i];
        float v = k >= 0 ? w[k] : 0.f;
        if (i < tot) {
            mem[i] = v;
            mem16[i] = f2h(v);
        } else
            memf[i - tot] = f2h(v);
    }
}

extern "C" int neth_actor_reload(neth_actor *a, const float *w, cudaStream_t s)
{
    actor_reload_k<<<240, 256, 0, s>>>(w, a->map, a->tot, a->nfr, a->mem, a->mem16, (nt16 *)a->memf);
    return cudaGetLastError();
}

extern "C" long neth_actor_diff(const neth_actor *a, const neth_actor *b)
{
    if (a->tot != b->tot || a->nfr != b->nfr)
        return -1;
    size_t sz[3] = {a->tot * 4, a->tot * 2, a->nfr * 2};
    const void *pa[3] = {a->mem, a->mem16, a->memf}, *pb[3] = {b->mem, b->mem16, b->memf};
    long d = 0;
    for (int k = 0; k < 3 && d >= 0; k++) {
        unsigned char *x = (unsigned char *)malloc(sz[k]), *y = (unsigned char *)malloc(sz[k]);
        if (cudaMemcpy(x, pa[k], sz[k], cudaMemcpyDeviceToHost) != cudaSuccess ||
            cudaMemcpy(y, pb[k], sz[k], cudaMemcpyDeviceToHost) != cudaSuccess)
            d = -1;
        for (size_t i = 0; i < sz[k] && d >= 0; i++)
            d += x[i] != y[i];
        free(x);
        free(y);
    }
    return d;
}

extern "C" neth_actor *neth_actor_load(const char *path, const char **err)
{
    const char *e0;
    if (!err)
        err = &e0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return *err = "ntw: cannot open", nullptr;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *d = malloc(n > 0 ? n : 1);
    size_t got = fread(d, 1, n, f);
    fclose(f);
    neth_actor *a = got == (size_t)n ? neth_actor_load_blob(d, n, err) : (*err = "ntw: short read", nullptr);
    free(d);
    return a;
}

extern "C" void neth_actor_free(neth_actor *a)
{
    if (a) {
        cudaFree(a->mem);
        cudaFree(a->mem16);
        cudaFree(a->memf);
        cudaFree(a->map);
        free(a);
    }
}

extern "C" void neth_actor_set_mode(neth_actor *a, int mode) { a->mode = mode; }

extern "C" neth_actor_ws *neth_actor_ws_new(neth_actor *a, int max_b, uint64_t seed)
{
    neth_actor_ws *w = (neth_actor_ws *)calloc(1, sizeof *w);
    w->a = a;
    w->max_b = max_b;
    const size_t px0 = NETH_NET_STEM ? P0 * P0 * NETH_NET_C0 : 0;   /* x0: the stem's padded output (each piece 16-byte aligned) */
    size_t per = px0 + P1 * P1 * NETH_NET_C1 + P2 * P2 * NETH_NET_C2 + F1K + (2 + NETH_NET_RNN) * NETH_NET_D,
           per16 = px0 + P1 * P1 * NETH_NET_C1 + P2 * P2 * NETH_NET_C2 + F1K + NETH_NET_D;
    size_t bytes = ((size_t)max_b * per + NETH_ACTOR_RNGF) * sizeof(float) + (size_t)max_b * per16 * sizeof(nt16);
    if (cudaMalloc(&w->mem, bytes) != cudaSuccess || cudaMemset(w->mem, 0, bytes) != cudaSuccess) {   /* the borders */
        cudaFree(w->mem);
        free(w);
        return nullptr;
    }
    w->rng = (unsigned long long *)w->mem;
    w->x0 = w->mem + NETH_ACTOR_RNGF;
#if NETH_ACTOR_FUSE
    if (cudaFuncSetAttribute(k_afused<8>, cudaFuncAttributeMaxDynamicSharedMemorySize, AF_SMEMF(8)) ||
        cudaFuncSetAttribute(k_afused<4>, cudaFuncAttributeMaxDynamicSharedMemorySize, AF_SMEMF(4)) ||
        cudaFuncSetAttribute(k_afused<2>, cudaFuncAttributeMaxDynamicSharedMemorySize, AF_SMEMF(2))) {
        cudaFree(w->mem);
        free(w);
        return nullptr;
    }
#endif
    w->x1 = w->x0 + (size_t)max_b * px0;
    w->x2 = w->x1 + (size_t)max_b * P1 * P1 * NETH_NET_C1;
    w->z0 = w->x2 + (size_t)max_b * P2 * P2 * NETH_NET_C2;
    w->z1 = w->z0 + (size_t)max_b * F1K;
    w->z2 = w->z1 + (size_t)max_b * NETH_NET_D;
    w->y = w->z2 + (size_t)max_b * NETH_NET_D;      /* NETH_NET_RNN's (empty without it) */
    w->x0h = (nt16 *)(w->y + (size_t)max_b * NETH_NET_D * NETH_NET_RNN);
    w->x1h = w->x0h + (size_t)max_b * px0;
    w->x2h = w->x1h + (size_t)max_b * P1 * P1 * NETH_NET_C1;
    w->z0h = w->x2h + (size_t)max_b * P2 * P2 * NETH_NET_C2;
    w->z1h = w->z0h + (size_t)max_b * F1K;
    unsigned long long r[2] = {seed, 0};
    cudaMemcpy(w->rng, r, sizeof r, cudaMemcpyHostToDevice);
    return w;
}

extern "C" void neth_actor_ws_free(neth_actor_ws *w)
{
    if (w) {
        cudaFree(w->mem);
        free(w);
    }
}

extern "C" int neth_actor_ws_seed(neth_actor_ws *w, uint64_t seed, uint64_t counter, cudaStream_t s)
{
    rng_set<<<1, 1, 0, s>>>(w->rng, seed, counter);
    return cudaGetLastError();
}

/* Each layer's tile, measured on the 3090 (out/perf/ntactor.tsv, out/perf/ntwidth.tsv); compile-time so a sweep
 * builds variants with -D. gemm_tile: BM, BN, TM, TV, KS, KC (KC divides K; not whole kernel rows, its columns are
 * computed a chunk); gemm_bf16 (the first layer, from the bytes): BM, BN, KS, KC; gemmb: BM, BN, KS, K[, FM, FN] (the
 * layer's whole K, from net.h's widths: gemm_mmr where it fits, else gemm_mma; FM x FN a warp's tile in 16x16 tiles,
 * gemm_mma's). wm 1 at 16x16 keeps lane/ntactor's tiles; the other variants' bf16 layers are lane/ntwidth's sweep's
 * best at a group of 128 (attempt 1: 24 configurations, then each layer's best by nsys, out/perf/ntwidth.tsv) */
#if NETH_NET_WM == 1 && !NETH_NET_STEM
#define T32_F1_ 16, 16, 2, 1, 8, 224
#define T16_C2_ 32, 32, 2, 16 * NETH_NET_C1
#else
#define T32_F1_ 16, 16, 2, 1, 8, 64
#define T16_C2_ 16, 32, 4, 16 * NETH_NET_C1
#endif
#if !NETH_NET_STEM && NETH_NET_WM == 2
#define T16V_C2 64, 64, 2, 16 * NETH_NET_C1, 2, 2
#define T16V_C3 32, 32, 4, 9 * NETH_NET_C2, 1, 1
#define T16V_F1 16, 32, 8, F1K, 1, 1
#define T16V_F2 16, 32, 8, NETH_NET_D, 1, 1
#elif !NETH_NET_STEM && NETH_NET_WM == 4
#define T16V_C2 64, 64, 1, 16 * NETH_NET_C1, 2, 2
#define T16V_C3 32, 64, 2, 9 * NETH_NET_C2, 1, 2
#define T16V_F1 32, 64, 2, F1K, 2, 2
#define T16V_F2 32, 64, 2, NETH_NET_D, 2, 2
#elif !NETH_NET_STEM && NETH_NET_WM >= 8
#define T16V_C2 64, 128, 1, 16 * NETH_NET_C1, 2, 4
#define T16V_C3 64, 64, 1, 9 * NETH_NET_C2, 2, 2
#define T16V_F1 32, 64, 2, F1K, 2, 2
#define T16V_F2 32, 64, 2, NETH_NET_D, 2, 2
#elif NETH_NET_STEM && NETH_NET_WM == 1
#define T16V_C2 64, 64, 1, 16 * NETH_NET_C1, 2, 2
#define T16V_C3 32, 64, 2, 9 * NETH_NET_C2, 1, 2
#elif NETH_NET_STEM && NETH_NET_WM >= 2
#define T16V_C1 64, 64, 1, K1, 2, 2
#define T16V_C2 64, 64, 1, 16 * NETH_NET_C1, 2, 2
#define T16V_C3 64, 64, 1, 9 * NETH_NET_C2, 2, 2
#define T16V_F1 16, 64, 4, F1K, 1, 2
#define T16V_F2 16, 64, 4, NETH_NET_D, 1, 2
#endif
#ifndef T32_S0
#define T32_S0 64, 16, 4, 1, 2, 48
#endif
#ifndef T32_C1
#if NETH_NET_STEM
#define T32_C1 32, 32, 4, 2, 8, 128
#else
#define T32_C1 64, 32, 4, 1, 2, 48
#endif
#endif
#ifndef T32_C2
#define T32_C2 32, 32, 4, 2, 8, 128
#endif
#ifndef T32_C3
#define T32_C3 16, 16, 4, 1, 16, 192
#endif
#ifndef T32_F1
#define T32_F1 T32_F1_
#endif
#ifndef T32_F2
#define T32_F2 16, 16, 4, 1, 16, 256
#endif
#ifndef T16_S0
#define T16_S0 64, 16, 1, 48
#endif
#ifndef T16_C1
#if defined(T16V_C1)
#define T16_C1 T16V_C1
#elif NETH_NET_STEM
#define T16_C1 32, 32, 2, K1
#else
#define T16_C1 64, 32, 1, 48
#endif
#endif
#ifndef T16_C2
#ifdef T16V_C2
#define T16_C2 T16V_C2
#else
#define T16_C2 T16_C2_
#endif
#endif
#ifndef T16_C3
#ifdef T16V_C3
#define T16_C3 T16V_C3
#else
#define T16_C3 16, 16, 8, 9 * NETH_NET_C2
#endif
#endif
#ifndef T16_F1
#ifdef T16V_F1
#define T16_F1 T16V_F1
#else
#define T16_F1 16, 16, 8, F1K
#endif
#endif
#ifndef T16_F2
#ifdef T16V_F2
#define T16_F2 T16V_F2
#else
#define T16_F2 16, 16, 8, NETH_NET_D
#endif
#endif

#include "rnn_actor.cuh"   /* lane/ntgru: the minGRU's step (NETH_NET_RNN) */

/* conv3's outputs into fc1's row: row m = (b, position) */
template <typename T = float>
struct flat_o {
    T *z0;
    __device__ T *operator()(int m) const { return z0 + ((unsigned)m / Q3) * F1K + ((unsigned)m % Q3) * NETH_NET_C3; }
};

extern "C" int neth_actor_step(neth_actor_ws *w, int B, const neth_actor_io *io, cudaStream_t s)
{
    if (B <= 0 || B > w->max_b || (NETH_NET_RNN && !io->h))
        return cudaErrorInvalidValue;
    neth_actor *a = w->a;
    constexpr int M1 = NETH_NET_O1 * NETH_NET_O1, M2 = NETH_NET_O2 * NETH_NET_O2;   /* conv1's and conv2's positions */
    pre_conv1<> pre = {io->ucam && io->ukey ? nullptr : w->rng, io->sc, w->z0};
#if NETH_ACTOR_FUSE
    if (a->mode == NETH_NET_ACTOR_BF16) {     /* lane/ntfc: the whole step, a block a tile of samples */
        af_w W = {a->w1h, a->hwh, a->b1, a->b2, a->b3, a->fb1, a->fb2, a->hb, a->wf2, a->wf3, a->wf1, a->wff2};
        switch (AF_SBFOR(B)) {
        case 8: k_afused<8><<<(B + 7) / 8, AF_T, AF_SMEMF(8), s>>>(W, B, *io, w->rng); break;
        case 4: k_afused<4><<<(B + 3) / 4, AF_T, AF_SMEMF(4), s>>>(W, B, *io, w->rng); break;
        default: k_afused<2><<<(B + 1) / 2, AF_T, AF_SMEMF(2), s>>>(W, B, *io, w->rng); break;
        }
        return cudaGetLastError();
    }
#endif
    if (a->mode == NETH_NET_ACTOR_BF16) {
        typedef nt16 bf;
        pre_conv1<bf> preh = {pre.rng, io->sc, w->z0h};
#if NETH_NET_STEM
        gemm16<T16_S0>(conv1_a{io->obs}, pad_o<OS, OS, NETH_NET_C0, bf>{w->x0h}, a->w0h, a->b0, B * OS * OS, NETH_NET_C0, 48, s, preh);
        gemmb<T16_C1>(conv_a<P0, NETH_NET_C0, NETH_NET_O1, NETH_NET_O1, 4, bf>{w->x0h}, pad_o<NETH_NET_O1, NETH_NET_O1, NETH_NET_C1, bf>{w->x1h}, a->wf1c,
                      a->w1h, a->b1, B * M1, NETH_NET_C1, s);
#else
        gemm16<T16_C1>(conv1_a{io->obs}, pad_o<OS, OS, NETH_NET_C1, bf>{w->x1h}, a->w1h, a->b1, B * M1, NETH_NET_C1, 48, s, preh);
#endif
        gemmb<T16_C2>(conv_a<P1, NETH_NET_C1, NETH_NET_O2, NETH_NET_O2, 4, bf>{w->x1h}, pad_o<NETH_NET_O2, NETH_NET_O2, NETH_NET_C2, bf>{w->x2h}, a->wf2,
                      a->w2h, a->b2, B * M2, NETH_NET_C2, s);
        gemmb<T16_C3>(conv_a<P2, NETH_NET_C2, NETH_NET_O3, NETH_NET_O3, 3, bf>{w->x2h}, flat_o<bf>{w->z0h}, a->wf3, a->w3h, a->b3, B * Q3,
                      NETH_NET_C3, s);
        gemmb<T16_F1>(dense_a<F1K, bf>{w->z0h}, dense_o<bf>{w->z1h, NETH_NET_D}, a->wf1, a->f1h, a->fb1, B, NETH_NET_D, s);
        gemmb<T16_F2>(dense_a<NETH_NET_D, bf>{w->z1h}, dense_o<>{w->z2, NETH_NET_D}, a->wff2, a->f2h, a->fb2, B, NETH_NET_D, s);
        heads_draw<true><<<(B + HB - 1) / HB, HTH, 0, s>>>(rnn_step(w, a, B, io, s, true), a->hwh, a->hb, B, *io, w->rng);
    } else {
        /* tiles: BM, BN, TM, TV, KS, KC (gemm_tile); each layer about 128 blocks at B 128 (wm 1, 16x16) */
#if NETH_NET_STEM
        gemm<T32_S0>(conv1_a{io->obs}, pad_o<OS, OS, NETH_NET_C0>{w->x0}, a->w0, a->b0, B * OS * OS, NETH_NET_C0, 48, s, pre);
        gemm<T32_C1>(conv_a<P0, NETH_NET_C0, NETH_NET_O1, NETH_NET_O1, 4>{w->x0}, pad_o<NETH_NET_O1, NETH_NET_O1, NETH_NET_C1>{w->x1}, a->w1, a->b1, B * M1,
                     NETH_NET_C1, K1, s);
#else
        gemm<T32_C1>(conv1_a{io->obs}, pad_o<OS, OS, NETH_NET_C1>{w->x1}, a->w1, a->b1, B * M1, NETH_NET_C1, 48, s, pre);
#endif
        gemm<T32_C2>(conv_a<P1, NETH_NET_C1, NETH_NET_O2, NETH_NET_O2, 4>{w->x1}, pad_o<NETH_NET_O2, NETH_NET_O2, NETH_NET_C2>{w->x2}, a->w2, a->b2, B * M2,
                     NETH_NET_C2, 16 * NETH_NET_C1, s);
        gemm<T32_C3>(conv_a<P2, NETH_NET_C2, NETH_NET_O3, NETH_NET_O3, 3>{w->x2}, flat_o<>{w->z0}, a->w3, a->b3, B * Q3, NETH_NET_C3,
                     9 * NETH_NET_C2, s);
        gemm<T32_F1>(dense_a<F1K>{w->z0}, dense_o<>{w->z1, NETH_NET_D}, a->f1, a->fb1, B, NETH_NET_D, F1K, s);
        gemm<T32_F2>(dense_a<NETH_NET_D>{w->z1}, dense_o<>{w->z2, NETH_NET_D}, a->f2, a->fb2, B, NETH_NET_D, NETH_NET_D, s);
        heads_draw<false><<<(B + HB - 1) / HB, HTH, 0, s>>>(rnn_step(w, a, B, io, s, false), a->hw, a->hb, B, *io, w->rng);
    }
    return cudaGetLastError();
}
