/* The learner's MLP and heads as one kernel (lane/ntfc, NETH_LEARN_FC 1; included by learn.cu, which defines the helpers it
 * uses: ldm4, wsum, wmax, NB, Z0LD, HS, P3, bf).
 *
 * k_fcrow takes FC_BM samples of a minibatch through the whole chain after the convolutions, each step's activations
 * in shared memory: z0 (relu(x3 + b3) and the scalars, the old k_flat, streamed in chunks of 64 columns) -> fc1, relu
 * -> fc2, relu -> the heads -> fastppo's loss (k_loss's arithmetic, a warp a sample) -> the heads' gradient dH -> d2 =
 * dH Wh relu'(z2) -> d1 = d2 W2 relu'(z1) -> dz0 = d1 W1, written as conv3's input gradient dx3 times relu'(x3 + b3)
 * (the old k_unflat). The weight gradients stay cuBLAS GEMMs over the batch (learn.cu), on the z0, z1, z2, dH, d2 and
 * d1 this kernel writes; the biases' gradients are this kernel's column sums a block (and row group), summed in order
 * by k_fcfin.
 *
 * z0 here is channels last (column p * C3 + c for x3's position p and channel c, x3's own layout: 16-byte loads), the
 * scalars after as net.h's; fc1's weight copies are permuted to match, and k_gunperm takes fc1's weight gradient back.
 * Every product is mma.sync m16n8k16 (prec.cuh's, so NETH_NET_MMA's modes hold) with A from shared memory (ldmatrix) and B
 * from fragment-packed 16-bit copies of the weights (k_fcpack, each minibatch: a warp's B fragments of a 16 x 16 tile
 * are one 16-byte load a lane, FC_PF k-steps in flight, the ring carried across fc1's chunks), L2-resident. FC_W warps:
 * 8 (a warp 32 output columns, 16 for the heads, of all FC_BM rows) or 16 (two row groups of FC_BM / 2). */
#ifndef NETH_LEARN_FC_CUH
#define NETH_LEARN_FC_CUH

#define FC_WP(w, n) ((w) + NETH_LEARN_OFF(n))              /* a tensor of the layout */
#define FC_C3P (NETH_NET_C3 * P3)                         /* z0's convolution part */
/* rows a block: 32 (two blocks an SM at D 256; learn_bench at minibatches of 4096 and 8192, s1 and w1, against 64
 * rows at 8 or 16 warps: 26.12 against 26.32 and 26.18 ms an update at s1's 4096, 4.126 against 4.206 at w1) */
/* fc1's and fc2's sums rounded to 16 bits before the bias, as the per-layer path's GEMM (bf16 out) then k_bias_relu_v
 * do: then k_fcrow's z1, z2 and dH equal the per-layer path's but for a few entries in millions (the sums' order). Rounded
 * once (FC_RND1, more exact) the rows near the PPO clip's edges chose the other side often enough that the a05 fixture's
 * conv.0 gradient missed its tolerance (1.48x fastppo's own bf16 against 0.86x; lane/ntfc) */
#ifdef FC_RND1
#define FC_ADD(a, bb) ((a) + (bb))
#else
#define FC_ADD(a, bb) (h2f(f2h(a)) + (bb))
#endif
#ifndef FC_BM
#define FC_BM 32
#endif
#ifndef FC_W
#define FC_W 8                                      /* warps: 8 or 16 */
#endif
#if FC_W == 16 && FC_BM < 64                        /* a chunk's loads need a uint4 a thread: 8 warps at D 512 */
#undef FC_W
#define FC_W 8
#endif
#define FC_WR (FC_W / 8)                            /* row groups */
#define FC_MT (FC_BM / 16 / FC_WR)                  /* a warp's m-tiles */
#define FC_T (FC_W * 32)
#define FC_HN 112                                   /* the heads padded to whole 16-column tiles */
#define FC_KC 64                                    /* fc1's A chunk */
#define FC_K1 ((Z0LD + FC_KC - 1) / FC_KC * FC_KC)  /* fc1's K, padded with zeros to whole chunks */
#define FC_LA (FC_KC + 8)                           /* rows of shared memory, padded 16 bytes (ldmatrix's banks) */
#define FC_LZ (NETH_NET_D + 8)
#define FC_LH (FC_HN + 4)                           /* hd's row (fp32) */
#define FC_LD (FC_HN + 8)                           /* dH's (bf16) */
#define FC_PF 4                                     /* B fragments' k-steps in flight */
#define FC_NP (NETH_NET_D / 256)                          /* passes of 256 columns over a D-wide output */
#define FC_BP (104 + 2 * NETH_NET_D)                      /* a row group's bias partials: dH's 97 (padded), d2's, d1's */
static_assert(NETH_NET_D % 256 == 0 && FC_C3P % 256 == 0 && FC_C3P % 8 == 0, "fc.cuh's passes");
static_assert(FC_KC == 16 * FC_PF, "a chunk is FC_PF k-steps");
static_assert((FC_W == 8 || FC_W == 16) && FC_MT >= 1 && FC_BM % (16 * FC_WR) == 0, "fc.cuh's warps");

/* z0's column j (channels last) is net.h's flattened column fc_zcol(j) (channel-major); the scalars stay put */
__host__ __device__ __forceinline__ int fc_zcol(int j) { return j < FC_C3P ? (j % NETH_NET_C3) * P3 + j / NETH_NET_C3 : j; }

/* the packed copies, in uint4 (8 halves: one lane's B fragments of one 16 x 16 tile): [n tile][k step][lane] */
#define FC_PK(N, K) ((long)((N) / 16) * ((K) / 16) * 32)
#define PK_1F 0L                                    /* fc1 forward: B(k, n) = W1[n][fc_zcol(k)], K FC_K1, N D */
#define PK_2F (PK_1F + FC_PK(NETH_NET_D, FC_K1))          /* fc2 forward: W2[n][k] */
#define PK_HF (PK_2F + FC_PK(NETH_NET_D, NETH_NET_D))           /* the heads: Wh[n][k], N 112 (rows 97 on zero) */
#define PK_HB (PK_HF + FC_PK(FC_HN, NETH_NET_D))          /* d2 = dH Wh: Wh[k][n], K 112 */
#define PK_2B (PK_HB + FC_PK(NETH_NET_D, FC_HN))          /* d1 = d2 W2: W2[k][n] */
#define PK_1B (PK_2B + FC_PK(NETH_NET_D, NETH_NET_D))           /* dz0 = d1 W1: W1[k][fc_zcol(n)], N C3P */
#define PK_N (PK_1B + FC_PK(FC_C3P, NETH_NET_D))

__device__ __forceinline__ float fc_b(const float *w, int m, int k, int n) {
    const float *W1 = w + NETH_LEARN_OFF(fc1_w), *W2 = w + NETH_LEARN_OFF(fc2_w), *Wh = w + NETH_LEARN_OFF(cam_w);
    switch (m) {
    case 0: return k < NETH_NET_FLAT ? W1[(long)n * NETH_NET_FLAT + fc_zcol(k)] : 0.0f;
    case 1: return W2[(long)n * NETH_NET_D + k];
    case 2: return n < NETH_LEARN_HEADS ? Wh[(long)n * NETH_NET_D + k] : 0.0f;
    case 3: return k < NETH_LEARN_HEADS ? Wh[(long)k * NETH_NET_D + n] : 0.0f;
    case 4: return W2[(long)k * NETH_NET_D + n];
    default: return W1[(long)k * NETH_NET_FLAT + fc_zcol(n)];
    }
}

/* one uint4 a thread: lane l of tile (j, s) holds B at k = 16 s + 2 (l % 4) + {0, 1, 8, 9}, n = 16 j + l / 4 (+ 8) */
__global__ void k_fcpack(const float *__restrict__ w, uint4 *pk) {
    long t = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= PK_N) return;
    const long off[6] = {PK_1F, PK_2F, PK_HF, PK_HB, PK_2B, PK_1B};
    const int KK[6] = {FC_K1, NETH_NET_D, NETH_NET_D, FC_HN, NETH_NET_D, NETH_NET_D};
    int m = 5;
    while (t < off[m]) m--;
    long i = t - off[m];
    int ks = KK[m] / 16, l = i % 32, s = (i / 32) % ks, j = i / 32 / ks;
    int k = 16 * s + 2 * (l & 3), na = 16 * j + (l >> 2), nb = na + 8;
    const int kk[4] = {k, k + 1, k + 8, k + 9};
    bf v[8];
#pragma unroll
    for (int q = 0; q < 4; q++) {
        v[q] = f2h(fc_b(w, m, kk[q], na));
        v[4 + q] = f2h(fc_b(w, m, kk[q], nb));
    }
    pk[t] = *(const uint4 *)v;
}

/* a warp's B fragments, FC_PF k-steps of FN n16 tiles in registers: the ring */
template <int FN>
struct fc_ring {
    uint4 b[FC_PF][FN];
};
/* the ring's first FC_PF steps of B (tile f at B + f TS 32: TS the whole K's k-steps) */
template <int FN, int TS>
__device__ __forceinline__ void fc_ring0(fc_ring<FN> &r, const uint4 *__restrict__ B, int lane) {
#pragma unroll
    for (int i = 0; i < FC_PF; i++)
#pragma unroll
        for (int f = 0; f < FN; f++)
            if (i < TS) r.b[i][f] = __ldg(B + ((long)f * TS + i) * 32 + lane);
}
/* acc[mt][2 f + h] += A[16 mt.., 16 i..] B[s0 + i][16 f + 8 h..] for the FC_PF steps i (s0 + i < TS) from the ring,
 * each step's slot refilled with step s0 + i + FC_PF: A in shared memory (row stride lda), its column 0 step s0's */
template <int FN, int TS>
__device__ __forceinline__ void fc_steps(acc4 (&acc)[FC_MT][2 * FN], fc_ring<FN> &r, const bf *As, int lda,
                                         const uint4 *__restrict__ B, int s0, int lane) {
    const bf *ap = As + (lane & 15) * lda + (lane >> 4) * 8;
#pragma unroll
    for (int i = 0; i < FC_PF; i++) {
        int s = s0 + i;
        if (TS % FC_PF == 0 || s < TS) {
            uint32_t a[FC_MT][4];
#pragma unroll
            for (int mt = 0; mt < FC_MT; mt++) ldm4(a[mt], ap + mt * 16 * lda + i * 16);
#pragma unroll
            for (int f = 0; f < FN; f++) {
                const uint32_t b0[2] = {r.b[i][f].x, r.b[i][f].y}, b1[2] = {r.b[i][f].z, r.b[i][f].w};
#pragma unroll
                for (int mt = 0; mt < FC_MT; mt++) {
                    mma16816(acc[mt][2 * f], a[mt], b0);
                    mma16816(acc[mt][2 * f + 1], a[mt], b1);
                }
            }
            acc_step(acc, s);
            if (s + FC_PF < TS)
#pragma unroll
                for (int f = 0; f < FN; f++) r.b[i][f] = __ldg(B + ((long)f * TS + s + FC_PF) * 32 + lane);
        }
    }
}
/* the whole K (TS steps) with A resident */
template <int FN, int TS>
__device__ __forceinline__ void fc_mma(acc4 (&acc)[FC_MT][2 * FN], const bf *As, int lda, const uint4 *__restrict__ B,
                                       int lane) {
    fc_ring<FN> r;
    fc_ring0<FN, TS>(r, B, lane);
#pragma unroll 1
    for (int s0 = 0; s0 < TS; s0 += FC_PF) fc_steps<FN, TS>(acc, r, As + s0 * 16, lda, B, s0, lane);
}

template <int N>
__device__ __forceinline__ void fc_zero(acc4 (&acc)[FC_MT][N]) {
#pragma unroll
    for (int mt = 0; mt < FC_MT; mt++)
#pragma unroll
        for (int q = 0; q < N; q++) acc[mt][q] = acc4{};
}

/* the column sums of a warp's tile (rows of all lanes with the same lane & 3: lanes 0..3 end with them) */
__device__ __forceinline__ float fc_csum(float v) {
    v += __shfl_xor_sync(0xffffffffu, v, 4);
    v += __shfl_xor_sync(0xffffffffu, v, 8);
    v += __shfl_xor_sync(0xffffffffu, v, 16);
    return v;
}

struct fc_args {
    int b;
    const int32_t *idx;
    const bf *x3;
    const float *w;
    const neth_sc_t *sc;
    const uint4 *pk;
    const int32_t *cam;
    const float *keys, *logp0, *adv, *ret, *mask, *nmp;
    float clip, vf, entc;
    bf *z0, *z1, *z2, *dh, *d2, *d1, *dx3;
    float *lpart, *bpart;
};

#define FC_SM0 (2 * FC_BM * FC_LA * 2 > FC_BM * FC_LD * 2 ? 2 * FC_BM * FC_LA * 2 : FC_BM * FC_LD * 2)
#define FC_SMZ (FC_BM * FC_LZ * 2)
#define FC_NMK (FC_NP * FC_W * FC_MT * 16)          /* relu'(z1) as bits: a word a (pass, warp, m tile, n8, element) */
#define FC_MK3 (FC_BM * FC_C3P / 8)                /* relu'(x3 + b3) as bits: a byte a row's 8 columns */
#define FC_SMEM (FC_SM0 + 2 * FC_SMZ + FC_NMK * 4 + FC_MK3)
static_assert(FC_SMEM <= 99 * 1024, "fc.cuh's shared memory");
static_assert(FC_BM * FC_LH * 4 <= FC_SMZ, "hd in z1's place");

/* two blocks an SM where two fit its 100 KB (FC_BM 32 at D 256), their registers then at most 128 a thread */
#define FC_MINB (2 * (FC_SMEM + 1024) <= 100 * 1024 && FC_T * 2 <= 1024 ? 2 : 1)
__global__ void __launch_bounds__(FC_T, FC_MINB) k_fcrow(fc_args p) {
    extern __shared__ __align__(16) unsigned char fsm[];
    bf *st = (bf *)fsm;                             /* fc1's A chunks [2][BM][LA]; then dH [BM][LD] */
    bf *dhs = (bf *)fsm;
    bf *z1s = (bf *)(fsm + FC_SM0);                 /* z1; then hd (fp32 [BM][LH]); then d1 */
    float *hds = (float *)z1s;
    bf *z2s = (bf *)(fsm + FC_SM0 + FC_SMZ);        /* z2; then d2 */
    unsigned *mk = (unsigned *)(fsm + FC_SM0 + 2 * FC_SMZ);
    unsigned char *mk3 = fsm + FC_SM0 + 2 * FC_SMZ + FC_NMK * 4;
    __shared__ float lsh[FC_W][NB];
    const int t = threadIdx.x, lane = t & 31, w = t >> 5, g = lane >> 2, tq = (lane & 3) * 2;
    const int wc = w & 7, wr = w >> 3, mr = wr * FC_MT * 16;    /* the warp's column group, row group, first row */
    const int r0 = blockIdx.x * FC_BM, b = p.b;
    const float *w0 = p.w;
    float *bp = p.bpart + ((long)blockIdx.x * FC_WR + wr) * FC_BP;

    /* ---- fc1: z0 streamed a chunk at a time (written out for the weight gradient), z1 = relu(z0 W1^T + b1) ---- */
    constexpr int NC = FC_K1 / FC_KC, CU = FC_BM * FC_KC / 8 / FC_T;   /* uint4 a thread a chunk */
    static_assert(FC_BM * FC_KC / 8 % FC_T == 0, "a chunk's loads");
    for (int pp = 0; pp < FC_NP; pp++) {
        acc4 acc[FC_MT][4];
        fc_zero(acc);
        /* a chunk's loads are issued before the chunk before it is summed and converted after (the waits behind the
         * products): x3's 8 channels raw, or 8 scalars; relu'(x3 + b3) kept as bits for dz0 */
        uint4 rx[CU];
        float rs[CU][8];
        auto load = [&](int c) {
#pragma unroll
            for (int u = 0; u < CU; u++) {
                int e = t + u * FC_T, m = e / (FC_KC / 8), j = c * FC_KC + e % (FC_KC / 8) * 8, n = r0 + m;
                if (n < b && j < FC_C3P)
                    rx[u] = __ldg((const uint4 *)(p.x3 + (long)n * FC_C3P + j));
                else if (n < b && j < Z0LD) {
                    const neth_sc_t *r = p.sc + (long)(p.idx ? p.idx[n] : n) * NETH_NET_NS - FC_C3P;
#pragma unroll
                    for (int q = 0; q < 8; q++) rs[u][q] = j + q < NETH_NET_FLAT ? neth_sc_f32(__ldg(r + j + q)) : 0.0f;
                }
            }
        };
        auto store = [&](int c) {
#pragma unroll
            for (int u = 0; u < CU; u++) {
                int e = t + u * FC_T, m = e / (FC_KC / 8), j = c * FC_KC + e % (FC_KC / 8) * 8, n = r0 + m;
                float f[8];
                if (n < b && j < FC_C3P) {
                    vld((const bf *)&rx[u], f);
                    const float *b3 = FC_WP(w0, conv3_b) + j % NETH_NET_C3;
                    unsigned bits = 0;
#pragma unroll
                    for (int q = 0; q < 8; q++) {
                        float x = f[q] + b3[q];
                        bits |= (x > 0.0f) << q;
                        f[q] = fmaxf(x, 0.0f);
                    }
                    if (pp == 0) mk3[m * (FC_C3P / 8) + j / 8] = (unsigned char)bits;
                } else if (n < b && j < Z0LD) {
#pragma unroll
                    for (int q = 0; q < 8; q++) f[q] = rs[u][q];
                } else {
#pragma unroll
                    for (int q = 0; q < 8; q++) f[q] = 0.0f;
                }
                uint4 v;
                vst((bf *)&v, f);
                *(uint4 *)(st + (c & 1) * FC_BM * FC_LA + m * FC_LA + j - c * FC_KC) = v;
                if (pp == 0 && n < b && j < Z0LD) *(uint4 *)(p.z0 + (long)n * Z0LD + j) = v;
            }
        };
        const uint4 *B = p.pk + PK_1F + (long)((pp * 256 + wc * 32) / 16) * (FC_K1 / 16) * 32;
        fc_ring<2> ring;
        fc_ring0<2, FC_K1 / 16>(ring, B, lane);
        load(0);
        store(0);
        __syncthreads();
        for (int c = 0; c < NC; c++) {
            if (c + 1 < NC) load(c + 1);
            fc_steps<2, FC_K1 / 16>(acc, ring, st + (c & 1) * FC_BM * FC_LA + mr * FC_LA, FC_LA, B, c * FC_PF, lane);
            if (c + 1 < NC) store(c + 1);
            __syncthreads();
        }
        acc_fin(acc);
        /* the bias, relu, z1 to shared memory and out, relu' as bits */
#pragma unroll
        for (int mt = 0; mt < FC_MT; mt++)
#pragma unroll
            for (int q = 0; q < 4; q++) {
                int n = pp * 256 + wc * 32 + q * 8 + tq;
                float b0 = FC_WP(w0, fc1_b)[n], b1 = FC_WP(w0, fc1_b)[n + 1];
#pragma unroll
                for (int hh = 0; hh < 2; hh++) {
                    int m = mr + mt * 16 + g + hh * 8;
                    nt162 v = f22h2(fmaxf(FC_ADD(acc[mt][q][2 * hh], b0), 0.0f),
                                    fmaxf(FC_ADD(acc[mt][q][2 * hh + 1], b1), 0.0f));
                    *(nt162 *)(z1s + m * FC_LZ + n) = v;
                    if (r0 + m < b) *(nt162 *)(p.z1 + (long)(r0 + m) * NETH_NET_D + n) = v;
                    float2 f = h22f2(v);
                    unsigned b0w = __ballot_sync(0xffffffffu, f.x > 0.0f), b1w = __ballot_sync(0xffffffffu, f.y > 0.0f);
                    if (lane == 0) {
                        unsigned *q4 = mk + (((pp * FC_W + w) * FC_MT + mt) * 4 + q) * 4;
                        q4[2 * hh] = b0w;
                        q4[2 * hh + 1] = b1w;
                    }
                }
            }
    }
    __syncthreads();

    /* ---- fc2: z2 = relu(z1 W2^T + b2) ---- */
    for (int pp = 0; pp < FC_NP; pp++) {
        acc4 acc[FC_MT][4];
        fc_zero(acc);
        fc_mma<2, NETH_NET_D / 16>(acc, z1s + mr * FC_LZ, FC_LZ,
                             p.pk + PK_2F + (long)((pp * 256 + wc * 32) / 16) * (NETH_NET_D / 16) * 32, lane);
        acc_fin(acc);
#pragma unroll
        for (int mt = 0; mt < FC_MT; mt++)
#pragma unroll
            for (int q = 0; q < 4; q++) {
                int n = pp * 256 + wc * 32 + q * 8 + tq;
                float b0 = FC_WP(w0, fc2_b)[n], b1 = FC_WP(w0, fc2_b)[n + 1];
#pragma unroll
                for (int hh = 0; hh < 2; hh++) {
                    int m = mr + mt * 16 + g + hh * 8;
                    nt162 v = f22h2(fmaxf(FC_ADD(acc[mt][q][2 * hh], b0), 0.0f),
                                    fmaxf(FC_ADD(acc[mt][q][2 * hh + 1], b1), 0.0f));
                    *(nt162 *)(z2s + m * FC_LZ + n) = v;
                    if (r0 + m < b) *(nt162 *)(p.z2 + (long)(r0 + m) * NETH_NET_D + n) = v;
                }
            }
    }
    __syncthreads();

    /* ---- the heads: hd = z2 Wh^T + bh, fp32, into z1's place (column groups 0..6, a 16-column tile each) ---- */
    if (wc < FC_HN / 16) {
        acc4 acc[FC_MT][2];
        fc_zero(acc);
        fc_mma<1, NETH_NET_D / 16>(acc, z2s + mr * FC_LZ, FC_LZ, p.pk + PK_HF + (long)wc * (NETH_NET_D / 16) * 32, lane);
        acc_fin(acc);
#pragma unroll
        for (int mt = 0; mt < FC_MT; mt++)
#pragma unroll
            for (int q = 0; q < 2; q++) {
                int n = wc * 16 + q * 8 + tq;
                float b0 = n < NETH_LEARN_HEADS ? FC_WP(w0, cam_b)[n] : 0.0f;
                float b1 = n + 1 < NETH_LEARN_HEADS ? FC_WP(w0, cam_b)[n + 1] : 0.0f;
#pragma unroll
                for (int hh = 0; hh < 2; hh++) {
                    int m = mr + mt * 16 + g + hh * 8;
                    hds[m * FC_LH + n] = acc[mt][q][2 * hh] + b0;
                    hds[m * FC_LH + n + 1] = acc[mt][q][2 * hh + 1] + b1;
                }
            }
    }
    __syncthreads();

    /* ---- the loss: k_loss's arithmetic, a warp a sample, dH (times NETH_NET_LSCALE, 16-bit) into fc1's chunks' place ---- */
    {
        float acc[NB] = {};     /* the sixth (ref_kl, lane/ntppo) stays 0: ref_kl and vonly take k_loss's path */
        for (int m = w; m < FC_BM; m += FC_W) {
            int n = r0 + m;
            bf *o = dhs + m * FC_LD;
            if (n >= b) {
                for (int j = lane; j < FC_HN; j += 32) o[j] = f2h(0.0f);
                continue;
            }
            long s = p.idx ? p.idx[n] : n;
            const float *h = hds + m * FC_LH;
            float nm = *p.nmp, mj = p.mask[s], A = p.adv[s];
            int hi = lane + 32 < NETH_NET_BINS;
            float la[NETH_NET_CAM], lb[NETH_NET_CAM];
            float lp = 0.0f, E = 0.0f;
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
                int k = p.cam[s * NETH_NET_CAM + c];
                lp += __shfl_sync(0xffffffffu, k < 32 ? la[c] : lb[c], k & 31);
                E += -e;
            }
            int kl = lane < NETH_NET_KEYS;
            float x = kl ? h[NETH_NET_CAM * NETH_NET_BINS + lane] : 0.0f, kj = kl ? p.keys[s * NETH_NET_KEYS + lane] : 0.0f;
            float lsp = fminf(x, 0.0f) - log1pf(expf(-fabsf(x))), lsn = fminf(-x, 0.0f) - log1pf(expf(-fabsf(x)));
            float pr = 1.0f / (1.0f + expf(-x));
            lp += wsum(kl ? kj * lsp + (1.0f - kj) * lsn : 0.0f);
            E += -wsum(kl ? pr * logf(pr + 1e-8f) + (1.0f - pr) * logf(1.0f - pr + 1e-8f) : 0.0f);
            float lp0 = p.logp0[s], ratio = expf(lp - lp0);
            float rc = fminf(fmaxf(ratio, 1.0f - p.clip), 1.0f + p.clip);
            float s1 = ratio * A, s2 = rc * A;
            float v = h[NETH_LEARN_HEADS - 1], dv = v - p.ret[s];
            acc[0] += -fminf(s1, s2) * mj;
            acc[1] += dv * dv;
            acc[2] += E * mj;
            acc[3] += (lp0 - lp) * mj;
            acc[4] += fabsf(ratio - 1.0f) > p.clip ? mj : 0.0f;
            float gm = -mj / nm;
            float w1 = s1 < s2 ? 1.0f : s1 == s2 ? 0.5f : 0.0f, w2 = 1.0f - w1;
            float inr = (ratio >= 1.0f - p.clip && ratio <= 1.0f + p.clip) ? 1.0f : 0.0f;
            float glp = (gm * w1 * A + gm * w2 * A * inr) * ratio;
            float gE = -p.entc * mj / nm;
#pragma unroll
            for (int c = 0; c < NETH_NET_CAM; c++) {
                int k = p.cam[s * NETH_NET_CAM + c];
                float pa = expf(la[c]), pb = hi ? expf(lb[c]) : 0.0f;
                float ga = gE * -(pa * la[c] + pa) + (lane == k ? glp : 0.0f);
                float gb = hi ? gE * -(pb * lb[c] + pb) + (lane + 32 == k ? glp : 0.0f) : 0.0f;
                float sg = wsum(ga + gb);
                o[c * NETH_NET_BINS + lane] = f2h((ga - pa * sg) * NETH_NET_LSCALE);
                if (hi) o[c * NETH_NET_BINS + lane + 32] = f2h((gb - pb * sg) * NETH_NET_LSCALE);
            }
            float dE = -(logf(pr + 1e-8f) + pr / (pr + 1e-8f)) + (logf(1.0f - pr + 1e-8f) + (1.0f - pr) / (1.0f - pr + 1e-8f));
            float gk = glp * (kj - pr) + gE * dE * pr * (1.0f - pr);
            if (kl) o[NETH_NET_CAM * NETH_NET_BINS + lane] = f2h(gk * NETH_NET_LSCALE);
            if (lane == 0) o[NETH_LEARN_HEADS - 1] = f2h(p.vf * dv / (float)b * NETH_NET_LSCALE);
            if (lane < FC_HN - NETH_LEARN_HEADS) o[NETH_LEARN_HEADS + lane] = f2h(0.0f);
            __syncwarp();
            for (int j = lane; j < HS; j += 32) p.dh[(long)n * HS + j] = o[j];
        }
        if (lane == 0)
            for (int k = 0; k < NB; k++) lsh[w][k] = acc[k];
    }
    __syncthreads();
    if (t < NB) {
        float s = 0.0f;
        for (int k = 0; k < FC_W; k++) s += lsh[k][t];
        p.lpart[blockIdx.x * NB + t] = s;
    }
    if (t < 104 * FC_WR) {  /* the heads' bias partials: dH's column sums over a row group's rows in order */
        int c = t % 104, rg = t / 104;
        float s = 0.0f;
        for (int m = rg * FC_BM / FC_WR; m < (rg + 1) * FC_BM / FC_WR; m++) s += h2f(dhs[m * FC_LD + c]);
        p.bpart[((long)blockIdx.x * FC_WR + rg) * FC_BP + c] = s;
    }

    /* ---- d2 = dH Wh relu'(z2), in z2's place ---- */
    for (int pp = 0; pp < FC_NP; pp++) {
        acc4 acc[FC_MT][4];
        fc_zero(acc);
        fc_mma<2, FC_HN / 16>(acc, dhs + mr * FC_LD, FC_LD,
                              p.pk + PK_HB + (long)((pp * 256 + wc * 32) / 16) * (FC_HN / 16) * 32, lane);
        acc_fin(acc);
#pragma unroll
        for (int q = 0; q < 4; q++) {
            int n = pp * 256 + wc * 32 + q * 8 + tq;
            float c0 = 0.0f, c1 = 0.0f;
#pragma unroll
            for (int mt = 0; mt < FC_MT; mt++)
#pragma unroll
                for (int hh = 0; hh < 2; hh++) {
                    int m = mr + mt * 16 + g + hh * 8;
                    float2 z = h22f2(*(nt162 *)(z2s + m * FC_LZ + n));
                    nt162 v = f22h2(z.x > 0.0f ? acc[mt][q][2 * hh] : 0.0f, z.y > 0.0f ? acc[mt][q][2 * hh + 1] : 0.0f);
                    *(nt162 *)(z2s + m * FC_LZ + n) = v;
                    if (r0 + m < b) *(nt162 *)(p.d2 + (long)(r0 + m) * NETH_NET_D + n) = v;
                    float2 f = h22f2(v);
                    c0 += f.x, c1 += f.y;
                }
            c0 = fc_csum(c0), c1 = fc_csum(c1);
            if (g == 0) bp[104 + n] = c0, bp[104 + n + 1] = c1;
        }
    }
    __syncthreads();

    /* ---- d1 = d2 W2 relu'(z1), in z1's place ---- */
    for (int pp = 0; pp < FC_NP; pp++) {
        acc4 acc[FC_MT][4];
        fc_zero(acc);
        fc_mma<2, NETH_NET_D / 16>(acc, z2s + mr * FC_LZ, FC_LZ,
                             p.pk + PK_2B + (long)((pp * 256 + wc * 32) / 16) * (NETH_NET_D / 16) * 32, lane);
        acc_fin(acc);
#pragma unroll
        for (int q = 0; q < 4; q++) {
            int n = pp * 256 + wc * 32 + q * 8 + tq;
            float c0 = 0.0f, c1 = 0.0f;
#pragma unroll
            for (int mt = 0; mt < FC_MT; mt++) {
                const unsigned *q4 = mk + (((pp * FC_W + w) * FC_MT + mt) * 4 + q) * 4;
#pragma unroll
                for (int hh = 0; hh < 2; hh++) {
                    int m = mr + mt * 16 + g + hh * 8;
                    bool k0 = q4[2 * hh] >> lane & 1, k1 = q4[2 * hh + 1] >> lane & 1;
                    nt162 v = f22h2(k0 ? acc[mt][q][2 * hh] : 0.0f, k1 ? acc[mt][q][2 * hh + 1] : 0.0f);
                    *(nt162 *)(z1s + m * FC_LZ + n) = v;
                    if (r0 + m < b) *(nt162 *)(p.d1 + (long)(r0 + m) * NETH_NET_D + n) = v;
                    float2 f = h22f2(v);
                    c0 += f.x, c1 += f.y;
                }
            }
            c0 = fc_csum(c0), c1 = fc_csum(c1);
            if (g == 0) bp[104 + NETH_NET_D + n] = c0, bp[104 + NETH_NET_D + n + 1] = c1;
        }
    }
    __syncthreads();

    /* ---- dz0 = d1 W1 over z0's convolution part, as dx3 times relu'(x3 + b3) ---- */
    for (int pp = 0; pp < FC_C3P / 256; pp++) {
        acc4 acc[FC_MT][4];
        fc_zero(acc);
        fc_mma<2, NETH_NET_D / 16>(acc, z1s + mr * FC_LZ, FC_LZ,
                             p.pk + PK_1B + (long)((pp * 256 + wc * 32) / 16) * (NETH_NET_D / 16) * 32, lane);
        acc_fin(acc);
#pragma unroll
        for (int mt = 0; mt < FC_MT; mt++)
#pragma unroll
            for (int q = 0; q < 4; q++) {
                int j = pp * 256 + wc * 32 + q * 8 + tq;
#pragma unroll
                for (int hh = 0; hh < 2; hh++) {
                    int m = mr + mt * 16 + g + hh * 8;
                    if (r0 + m >= b) continue;
                    unsigned k = mk3[m * (FC_C3P / 8) + j / 8] >> (j % 8);
                    *(nt162 *)(p.dx3 + (long)(r0 + m) * FC_C3P + j) =
                        f22h2(k & 1 ? acc[mt][q][2 * hh] : 0.0f, k & 2 ? acc[mt][q][2 * hh + 1] : 0.0f);
                }
            }
    }
}

/* the biases' gradients: each column's row-group partials summed in block, then row-group order */
__global__ void k_fcfin(int nblk, const float *bpart, float *grad) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= NETH_LEARN_HEADS + 2 * NETH_NET_D) return;
    int c = i < NETH_LEARN_HEADS ? i : i < NETH_LEARN_HEADS + NETH_NET_D ? 104 + i - NETH_LEARN_HEADS : 104 + NETH_NET_D + i - NETH_LEARN_HEADS - NETH_NET_D;
    float s = 0.0f;
    for (int k = 0; k < nblk * FC_WR; k++) s += bpart[(long)k * FC_BP + c];
    if (i < NETH_LEARN_HEADS) grad[NETH_LEARN_OFF(cam_b) + i] = s;
    else if (i < NETH_LEARN_HEADS + NETH_NET_D) grad[NETH_LEARN_OFF(fc2_b) + i - NETH_LEARN_HEADS] = s;
    else grad[NETH_LEARN_OFF(fc1_b) + i - NETH_LEARN_HEADS - NETH_NET_D] = s;
}

#endif
