/* lane/acrl: the privileged critic (learn.h neth_critic_*), included by learn.cu. An MLP over a sample's whole scalar row as
 * the vector env wrote it, before --zero-sc (the privileged scalars the actor never reads: the .or build's seen-ore
 * and seen-log slots, task.hint's nearest block; the dig's; the player's own state):
 *   a1 [H] = relu(W1 x + b1)       x [NETH_NET_NS]
 *   a2 [H] = relu(W2 a1 + b2)
 *   v_c    = u . a2 + c
 * added to the trunk's value head (v = v_w z2 + v_b + v_c): the actor's sample value (GAE's val and boot) and the
 * learner's value term both read the sum. Its inputs join after the trunk and it shares nothing with the actor, so
 * the actor's logits never depend on x; the value loss still trains the trunk through v_w z2 (the shared-stem
 * critic's half). u and c start at 0, so a fresh critic adds nothing and a net plays and values as without it.
 *
 * lane/neon-prec-nn: master weights, gradients, Adam moments, and value output stay fp32; actor inference uses
 * transposed coalesced weights (nt16 in 16-bit mode, fp32 in fp32 mode) with warp-shuffle reduction; learner
 * forward/backward uses cuBLAS GEMMs (nt16 inputs with fp32 accumulation in 16-bit mode, fp32 in fp32 mode) with
 * NETH_NET_LSCALE loss scaling on fp16 activation gradients and fp32 gradient output. */

#include <mma.h>

#define NETH_CRITIC_MAXH 256
#define NETH_CRITIC_NSP (((NETH_NET_NS + 7) / 8) * 8)
#define NETH_CRITIC_NSP16 (((NETH_NET_NS + 15) / 16) * 16)

struct neth_critic {
    int H, HP, P;               /* hidden width, padded multiple of 8, parameter count */
    int bf16;                   /* actor 16-bit mode (set by neth_critic_set_mode) */
    float *w, *g, *m, *v;       /* the learner's: parameters, gradient, Adam's moments [P] (fp32) */
    float *wa[2];               /* the actor's two images (fp32 layout: b1, b2, u, c live here) */
    float *w1t[2], *w2t[2];     /* actor transposed fp32 weights: [NS][H], [H][H] */
    bf *w1th[2], *w2th[2];      /* actor transposed nt16 weights: [NS][H], [H][H] */
    bf *w1ah[2], *w2ah[2];      /* actor row-major nt16 weights for wmma: [H][NSP16], [H][H] */
    int cur;
    int step;
    int nmax;                   /* the learner's minibatch at most */
    void *xb, *a1, *a2, *d1, *d2;   /* learner scratch (sized for fp32 or nt16): [nmax][NSP], [nmax][HP] */
    void *w1p, *w2p;            /* learner packed weights: [HP][NSP], [HP][HP] */
    void *dvst;                 /* [nmax] dv * scale in T (for gu GEMM) */
    float *dvs;                 /* [nmax] contiguous dv in fp32 (for gc sum) */
    float *gw1p, *gw2p;         /* [HP][NSP], [HP][HP] fp32 gradient scratch for unpadding */
    float *part;                /* the norm's partials */
};

/* the parameters' offsets: W1 [H][NS], b1 [H], W2 [H][H], b2 [H], u [H], c */
#define NETH_CRITIC_W1(C) 0
#define NETH_CRITIC_B1(C) ((long)(C)->H * NETH_NET_NS)
#define NETH_CRITIC_W2(C) (NETH_CRITIC_B1(C) + (C)->H)
#define NETH_CRITIC_B2(C) (NETH_CRITIC_W2(C) + (long)(C)->H * (C)->H)
#define NETH_CRITIC_U(C) (NETH_CRITIC_B2(C) + (C)->H)
#define NETH_CRITIC_C(C) (NETH_CRITIC_U(C) + (C)->H)

/* copy master weights to actor image and build coalesced transposed + row-major wmma actor weights */
__global__ void k_c_prep_act(int H, int P, const float *__restrict__ src, float *__restrict__ dst,
                             float *__restrict__ w1t, bf *__restrict__ w1th, bf *__restrict__ w1ah,
                             float *__restrict__ w2t, bf *__restrict__ w2th, bf *__restrict__ w2ah) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= P) return;
    float v = src[i];
    dst[i] = v;
    long nW1 = (long)H * NETH_NET_NS;
    long oW2 = nW1 + H;
    long nW2 = (long)H * H;
    if (i < nW1) {
        int j = (int)(i / NETH_NET_NS), k = (int)(i % NETH_NET_NS);
        long t = (long)k * H + j;
        w1t[t] = v;
        bf vh = f2h(v);
        w1th[t] = vh;
        w1ah[(long)j * NETH_CRITIC_NSP16 + k] = vh;
    } else if (i >= oW2 && i < oW2 + nW2) {
        long r = i - oW2;
        int j = (int)(r / H), k = (int)(r % H);
        long t = (long)k * H + j;
        w2t[t] = v;
        bf vh = f2h(v);
        w2th[t] = vh;
        w2ah[r] = vh;
    }
}

/* fused 2-layer tensor-core actor forward: one block (8 warps = 256 threads) per 32 samples, H multiple of 16 */
#if NETH_CRITIC_NSP16 <= 256
__global__ void __launch_bounds__(256)
k_cf_act_tc(int b, int H, const neth_sc_t *__restrict__ x, const float *__restrict__ wa,
            const bf *__restrict__ w1ah, const bf *__restrict__ w2ah, float *__restrict__ out, int os) {
    namespace wm = nvcuda::wmma;
    constexpr int XS = NETH_CRITIC_NSP16 + 8;
    constexpr int H1S = NETH_CRITIC_MAXH + 8;
    __shared__ __align__(16) bf xs[32][XS];
    __shared__ __align__(16) bf h1[32][H1S];
    __shared__ __align__(16) float tmp[8][16][16];
    __shared__ float part[8][32];
    int b0 = blockIdx.x * 32, ns = b - b0 < 32 ? b - b0 : 32;
    int t = threadIdx.x, lane = t & 31, wid = t >> 5;
    for (int i = t; i < 32 * NETH_CRITIC_NSP16; i += 256) {
        int m = i / NETH_CRITIC_NSP16, k = i % NETH_CRITIC_NSP16;
        xs[m][k] = (m < ns && k < NETH_NET_NS) ? f2h(neth_sc_f32(x[(long)(b0 + m) * NETH_NET_NS + k])) : f2h(0.0f);
    }
    __syncthreads();
    const float *b1 = wa + (long)H * NETH_NET_NS;
    const float *b2 = b1 + H + (long)H * H;
    const float *u = b2 + H;
    for (int wn = wid * 16; wn < H; wn += 128) {
        wm::fragment<wm::accumulator, 16, 16, 16, float> c0, c1;
        wm::fill_fragment(c0, 0.0f);
        wm::fill_fragment(c1, 0.0f);
        for (int k = 0; k < NETH_CRITIC_NSP16; k += 16) {
            wm::fragment<wm::matrix_a, 16, 16, 16, bf, wm::row_major> fa0, fa1;
            wm::fragment<wm::matrix_b, 16, 16, 16, bf, wm::col_major> fb;
            wm::load_matrix_sync(fa0, &xs[0][k], XS);
            wm::load_matrix_sync(fa1, &xs[16][k], XS);
            wm::load_matrix_sync(fb, w1ah + (long)wn * NETH_CRITIC_NSP16 + k, NETH_CRITIC_NSP16);
            wm::mma_sync(c0, fa0, fb, c0);
            wm::mma_sync(c1, fa1, fb, c1);
        }
        for (int rm = 0; rm < 2; rm++) {
            wm::store_matrix_sync(&tmp[wid][0][0], rm ? c1 : c0, 16, wm::mem_row_major);
            __syncwarp();
            for (int e = lane; e < 256; e += 32) {
                int r = e >> 4, c = e & 15;
                h1[rm * 16 + r][wn + c] = f2h(fmaxf(tmp[wid][r][c] + b1[wn + c], 0.0f));
            }
            __syncwarp();
        }
    }
    part[wid][lane] = 0.0f;
    __syncthreads();
    for (int wn = wid * 16; wn < H; wn += 128) {
        wm::fragment<wm::accumulator, 16, 16, 16, float> c0, c1;
        wm::fill_fragment(c0, 0.0f);
        wm::fill_fragment(c1, 0.0f);
        for (int k = 0; k < H; k += 16) {
            wm::fragment<wm::matrix_a, 16, 16, 16, bf, wm::row_major> fa0, fa1;
            wm::fragment<wm::matrix_b, 16, 16, 16, bf, wm::col_major> fb;
            wm::load_matrix_sync(fa0, &h1[0][k], H1S);
            wm::load_matrix_sync(fa1, &h1[16][k], H1S);
            wm::load_matrix_sync(fb, w2ah + (long)wn * H + k, H);
            wm::mma_sync(c0, fa0, fb, c0);
            wm::mma_sync(c1, fa1, fb, c1);
        }
        for (int rm = 0; rm < 2; rm++) {
            wm::store_matrix_sync(&tmp[wid][0][0], rm ? c1 : c0, 16, wm::mem_row_major);
            __syncwarp();
            int r = lane & 15, c0i = (lane >> 4) * 8;
            float s = 0.0f;
#pragma unroll
            for (int c = 0; c < 8; c++)
                s = fmaf(u[wn + c0i + c], fmaxf(tmp[wid][r][c0i + c] + b2[wn + c0i + c], 0.0f), s);
            s += __shfl_xor_sync(0xffffffffu, s, 16);
            if (lane < 16) part[wid][rm * 16 + r] += s;
            __syncwarp();
        }
    }
    __syncthreads();
    if (t < ns) {
        float s = u[H];
#pragma unroll
        for (int q = 0; q < 8; q++) s += part[q][t];
        out[(long)(b0 + t) * os] += s;
    }
}
#endif

/* coalesced actor forward: one block per sample, whole-warp blockDim >= H; out[n * os] += v_c */
template <class T>
__global__ void k_cf_act(int b, int H, const neth_sc_t *__restrict__ x, const float *__restrict__ wa,
                         const T *__restrict__ w1t, const T *__restrict__ w2t, float *__restrict__ out, int os) {
    __shared__ float xs[NETH_NET_NS], h1[NETH_CRITIC_MAXH], wred[8];
    int n = blockIdx.x, j = threadIdx.x, lane = j & 31, wid = j >> 5;
    if (n >= b) return;
    const neth_sc_t *xr = x + (long)n * NETH_NET_NS;
    for (int k = j; k < NETH_NET_NS; k += blockDim.x) xs[k] = neth_sc_f32(xr[k]);
    __syncthreads();
    const float *b1 = wa + (long)H * NETH_NET_NS;
    const float *b2 = b1 + H + (long)H * H;
    const float *u = b2 + H;
    if (j < H) {
        float a = b1[j];
#pragma unroll 4
        for (int k = 0; k < NETH_NET_NS; k++) a = fmaf(ldf(w1t[(long)k * H + j]), xs[k], a);
        h1[j] = fmaxf(a, 0.0f);
    }
    __syncthreads();
    float prod = 0.0f;
    if (j < H) {
        float a2 = b2[j];
#pragma unroll 4
        for (int k = 0; k < H; k++) a2 = fmaf(ldf(w2t[(long)k * H + j]), h1[k], a2);
        a2 = fmaxf(a2, 0.0f);
        prod = u[j] * a2;
    }
    prod = wsum(prod);
    if (lane == 0) wred[wid] = prod;
    __syncthreads();
    if (j == 0) {
        int nw = blockDim.x >> 5;
        float s = u[H];     /* c */
        for (int q = 0; q < nw; q++) s += wred[q];
        out[(long)n * os] += s;
    }
}

/* learner helpers: pack weights [H][NS]->[HP][NSP] and [H][H]->[HP][HP], gather x->[b][NSP] */
template <class T>
__global__ void k_c_wpack(int H, int HP, const float *__restrict__ w, T *__restrict__ w1p, T *__restrict__ w2p) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    long n1 = (long)HP * NETH_CRITIC_NSP, n2 = (long)HP * HP;
    const float *W1 = w, *W2 = w + (long)H * NETH_NET_NS + H;
    if (i < n1) {
        int j = (int)(i / NETH_CRITIC_NSP), k = (int)(i % NETH_CRITIC_NSP);
        w1p[i] = (j < H && k < NETH_NET_NS) ? cvt<T>(W1[(long)j * NETH_NET_NS + k]) : cvt<T>(0.0f);
    } else if (i < n1 + n2) {
        long r = i - n1;
        int j = (int)(r / HP), k = (int)(r % HP);
        w2p[r] = (j < H && k < H) ? cvt<T>(W2[(long)j * H + k]) : cvt<T>(0.0f);
    }
}

template <class T>
__global__ void k_c_gather(int b, const int32_t *__restrict__ idx, const neth_sc_t *__restrict__ x, T *__restrict__ xb) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)b * NETH_CRITIC_NSP) return;
    int n = (int)(i / NETH_CRITIC_NSP), k = (int)(i % NETH_CRITIC_NSP);
    long r = idx ? idx[n] : n;
    xb[i] = k < NETH_NET_NS ? cvt<T>(neth_sc_f32(x[r * NETH_NET_NS + k])) : cvt<T>(0.0f);
}

template <class T>
__global__ void k_c_bias_relu(int b, int H, int HP, T *__restrict__ a, const float *__restrict__ bias) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)b * HP) return;
    int j = (int)(i % HP);
    a[i] = j < H ? cvt<T>(fmaxf(ldf(a[i]) + bias[j], 0.0f)) : cvt<T>(0.0f);
}

/* layer 2 bias + relu + dot(u, a2) + c -> out[n * os] += v_c; one warp per sample */
template <class T>
__global__ void k_c_relu_out(int b, int H, int HP, T *__restrict__ a2, const float *__restrict__ b2,
                             const float *__restrict__ u, float *__restrict__ out, int os) {
    int lane = threadIdx.x & 31, wid = threadIdx.x >> 5;
    int n = blockIdx.x * (blockDim.x >> 5) + wid;
    if (n >= b) return;
    T *row = a2 + (long)n * HP;
    float s = 0.0f;
    for (int j = lane; j < HP; j += 32) {
        float v = j < H ? fmaxf(ldf(row[j]) + b2[j], 0.0f) : 0.0f;
        row[j] = cvt<T>(v);
        if (j < H) s = fmaf(u[j], v, s);
    }
    s = wsum(s);
    if (lane == 0) out[(long)n * os] += s + u[H];
}

/* backward through head + layer 2 relu: d2[n][j] = (a2 > 0 ? dv * u[j] * scale : 0); also stage dvs & dvst */
template <class T>
__global__ void k_c_bwd_head(int b, int H, int HP, const T *__restrict__ a2, const float *__restrict__ u,
                             const float *__restrict__ dv, int os, float scale,
                             T *__restrict__ d2, float *__restrict__ dvs, T *__restrict__ dvst) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)b * HP) return;
    int n = (int)(i / HP), j = (int)(i % HP);
    float d = dv[(long)n * os];
    if (j == 0) {
        dvs[n] = d;
        dvst[n] = cvt<T>(d * scale);
    }
    if (j < H && ldf(a2[i]) > 0.0f) d2[i] = cvt<T>(d * u[j] * scale);
    else d2[i] = cvt<T>(0.0f);
}

template <class T>
__global__ void k_c_relu_bwd(int b, int H, int HP, T *__restrict__ d1, const T *__restrict__ a1) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)b * HP) return;
    int j = (int)(i % HP);
    if (j >= H || ldf(a1[i]) <= 0.0f) d1[i] = cvt<T>(0.0f);
}

__global__ void k_c_unpad(int H, int HP, const float *__restrict__ gw1p, const float *__restrict__ gw2p,
                          float *__restrict__ gw1, float *__restrict__ gw2) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    long n1 = (long)H * NETH_NET_NS, n2 = (long)H * H;
    if (i < n1) {
        int j = (int)(i / NETH_NET_NS), k = (int)(i % NETH_NET_NS);
        gw1[i] = gw1p[(long)j * NETH_CRITIC_NSP + k];
    } else if (gw2p && i < n1 + n2) {
        long r = i - n1;
        int j = (int)(r / H), k = (int)(r % H);
        gw2[r] = gw2p[(long)j * HP + k];
    }
}

__global__ void k_c_sumc(int b, const float *__restrict__ dvs, float *__restrict__ gc) {
    __shared__ float sh[256];
    float s = 0.0f;
    for (int i = threadIdx.x; i < b; i += 256) s += dvs[i];
    sh[threadIdx.x] = s;
    __syncthreads();
    for (int w = 128; w > 0; w >>= 1) {
        if (threadIdx.x < w) sh[threadIdx.x] += sh[threadIdx.x + w];
        __syncthreads();
    }
    if (threadIdx.x == 0) *gc = sh[0];
}

static uint64_t neth_critic_mix(uint64_t *s) {     /* splitmix64 */
    uint64_t z = (*s += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static void critic_sync_act(neth_critic *C, int slot, cudaStream_t s) {
    int blk = (C->P + 255) / 256;
    k_c_prep_act<<<blk, 256, 0, s>>>(C->H, C->P, C->w, C->wa[slot], C->w1t[slot], C->w1th[slot], C->w1ah[slot],
                                     C->w2t[slot], C->w2th[slot], C->w2ah[slot]);
}

neth_critic *neth_critic_new(int H, int nmax, uint64_t seed) {
    if (H < 1 || H > NETH_CRITIC_MAXH) {
        fprintf(stderr, "neth_critic_new: hidden width %d outside 1..%d\n", H, NETH_CRITIC_MAXH);
        return NULL;
    }
    neth_critic *C = (neth_critic *)calloc(1, sizeof *C);
    C->H = H;
    C->HP = ((H + 7) / 8) * 8;
    C->nmax = nmax;
    C->P = (int)(NETH_CRITIC_C(C) + 1);
    float **bufs[] = {&C->w, &C->g, &C->m, &C->v, &C->wa[0], &C->wa[1]};
    for (size_t i = 0; i < sizeof bufs / sizeof *bufs; i++) CK(cudaMalloc((void **)bufs[i], C->P * sizeof(float)));
    for (int i = 0; i < 2; i++) {
        CK(cudaMalloc((void **)&C->w1t[i], (size_t)NETH_NET_NS * H * sizeof(float)));
        CK(cudaMalloc((void **)&C->w2t[i], (size_t)H * H * sizeof(float)));
        CK(cudaMalloc((void **)&C->w1th[i], (size_t)NETH_NET_NS * H * sizeof(bf)));
        CK(cudaMalloc((void **)&C->w2th[i], (size_t)H * H * sizeof(bf)));
        CK(cudaMalloc((void **)&C->w1ah[i], (size_t)H * NETH_CRITIC_NSP16 * sizeof(bf)));
        CK(cudaMalloc((void **)&C->w2ah[i], (size_t)H * H * sizeof(bf)));
        CK(cudaMemset(C->w1ah[i], 0, (size_t)H * NETH_CRITIC_NSP16 * sizeof(bf)));
    }
    CK(cudaMalloc(&C->xb, (size_t)nmax * NETH_CRITIC_NSP * sizeof(float)));
    void **acts[] = {&C->a1, &C->a2, &C->d1, &C->d2};
    for (size_t i = 0; i < sizeof acts / sizeof *acts; i++)
        CK(cudaMalloc(acts[i], (size_t)nmax * C->HP * sizeof(float)));
    CK(cudaMalloc(&C->w1p, (size_t)C->HP * NETH_CRITIC_NSP * sizeof(float)));
    CK(cudaMalloc(&C->w2p, (size_t)C->HP * C->HP * sizeof(float)));
    CK(cudaMalloc(&C->dvst, (size_t)nmax * sizeof(float)));
    CK(cudaMalloc((void **)&C->dvs, (size_t)nmax * sizeof(float)));
    CK(cudaMalloc((void **)&C->gw1p, (size_t)C->HP * NETH_CRITIC_NSP * sizeof(float)));
    CK(cudaMalloc((void **)&C->gw2p, (size_t)C->HP * C->HP * sizeof(float)));
    CK(cudaMalloc((void **)&C->part, 64 * sizeof(float)));
    /* torch's Linear default: weights and biases uniform in +-1/sqrt(fan_in); u and c zero (a fresh critic adds 0) */
    float *h = (float *)calloc(C->P, sizeof(float));
    uint64_t s = seed ^ 0xac71c0deULL;
    for (long p = 0; p < NETH_CRITIC_U(C); p++) {
        float bound = p < NETH_CRITIC_W2(C) ? 1.0f / sqrtf((float)NETH_NET_NS) : 1.0f / sqrtf((float)H);
        double u = (double)(neth_critic_mix(&s) >> 11) * (1.0 / 9007199254740992.0);
        h[p] = (float)((2.0 * u - 1.0) * bound);
    }
    CK(cudaMemcpy(C->w, h, C->P * sizeof(float), cudaMemcpyHostToDevice));
    free(h);
    critic_sync_act(C, 0, 0);
    critic_sync_act(C, 1, 0);
    CK(cudaMemset(C->m, 0, C->P * sizeof(float)));
    CK(cudaMemset(C->v, 0, C->P * sizeof(float)));
    CK(cudaDeviceSynchronize());
    return C;
}

void neth_critic_free(neth_critic *C) {
    if (!C) return;
    cudaFree(C->w), cudaFree(C->g), cudaFree(C->m), cudaFree(C->v), cudaFree(C->wa[0]), cudaFree(C->wa[1]);
    for (int i = 0; i < 2; i++)
        cudaFree(C->w1t[i]), cudaFree(C->w2t[i]), cudaFree(C->w1th[i]), cudaFree(C->w2th[i]), cudaFree(C->w1ah[i]), cudaFree(C->w2ah[i]);
    cudaFree(C->xb), cudaFree(C->a1), cudaFree(C->a2), cudaFree(C->d1), cudaFree(C->d2);
    cudaFree(C->w1p), cudaFree(C->w2p), cudaFree(C->dvst), cudaFree(C->dvs), cudaFree(C->gw1p), cudaFree(C->gw2p);
    cudaFree(C->part);
    free(C);
}

int neth_critic_params(const neth_critic *C) { return C ? C->P : 0; }

void neth_critic_set_mode(neth_critic *C, int bf16) {
    if (C) C->bf16 = bf16 ? 1 : 0;
}

void neth_critic_act(neth_critic *C, int b, const neth_sc_t *x, float *value, void *stream) {
#if NETH_CRITIC_NSP16 <= 256
    if (C->bf16 && (C->H & 15) == 0 && b >= 8) {
        k_cf_act_tc<<<(b + 31) / 32, 256, 0, (cudaStream_t)stream>>>(b, C->H, x, C->wa[C->cur], C->w1ah[C->cur], C->w2ah[C->cur], value, 1);
        return;
    }
#endif
    int th = ((C->H + 31) / 32) * 32;
    if (C->bf16)
        k_cf_act<bf><<<b, th, 0, (cudaStream_t)stream>>>(b, C->H, x, C->wa[C->cur], C->w1th[C->cur], C->w2th[C->cur], value, 1);
    else
        k_cf_act<float><<<b, th, 0, (cudaStream_t)stream>>>(b, C->H, x, C->wa[C->cur], C->w1t[C->cur], C->w2t[C->cur], value, 1);
}

void neth_critic_refresh(neth_critic *C, void *stream) {
    critic_sync_act(C, C->cur ^ 1, (cudaStream_t)stream);
}

void neth_critic_switch(neth_critic *C) { C->cur ^= 1; }

int neth_critic_cur(const neth_critic *C) { return C->cur; }

int neth_critic_save(const neth_critic *C, const char *path) {
    float *h = (float *)malloc(C->P * sizeof(float));
    CK(cudaMemcpy(h, C->w, C->P * sizeof(float), cudaMemcpyDeviceToHost));
    FILE *f = fopen(path, "wb");
    int rc = -1;
    if (f) {
        int32_t hd[4] = {0x3143544e /* "NTC1" */, C->H, NETH_NET_NS, C->P};
        rc = fwrite(hd, sizeof hd, 1, f) == 1 && fwrite(h, sizeof(float), C->P, f) == (size_t)C->P ? 0 : -1;
        rc |= fclose(f);
    }
    free(h);
    return rc;
}

int neth_critic_load(neth_critic *C, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int32_t hd[4];
    float *h = (float *)malloc(C->P * sizeof(float));
    int rc = fread(hd, sizeof hd, 1, f) == 1 && hd[0] == 0x3143544e && hd[1] == C->H && hd[2] == NETH_NET_NS &&
                     hd[3] == C->P && fread(h, sizeof(float), C->P, f) == (size_t)C->P ? 0 : -1;
    fclose(f);
    if (!rc) {
        CK(cudaMemcpy(C->w, h, C->P * sizeof(float), cudaMemcpyHostToDevice));
        critic_sync_act(C, 0, 0);
        critic_sync_act(C, 1, 0);
        CK(cudaDeviceSynchronize());
    }
    free(h);
    return rc;
}

/* the learner's side (learn.cu neth_learn_grad and neth_learn_update): cuBLAS GEMMs + coalesced epilogues */
template <class T>
static void critic_fwd_t(neth_learn *L, neth_critic *C, int b, const int32_t *idx, const neth_sc_t *x, float *out, int os) {
    cudaStream_t s = L->s;
    int H = C->H, HP = C->HP;
    const float one = 1.0f, zero = 0.0f;
    cudaDataType_t ab = L->ab;
    cublasComputeType_t ct = L->prec == NETH_LEARN_BF16 ? CUBLAS_COMPUTE_32F : L->ct;
    long npk = (long)HP * NETH_CRITIC_NSP + (long)HP * HP;
    k_c_wpack<T><<<(int)((npk + 255) / 256), 256, 0, s>>>(H, HP, C->w, (T *)C->w1p, (T *)C->w2p);
    k_c_gather<T><<<(int)(((long)b * NETH_CRITIC_NSP + 255) / 256), 256, 0, s>>>(b, idx, x, (T *)C->xb);
    /* a1 [b][HP] = xb [b][NSP] * w1p^T [NSP][HP] */
    CB(cublasGemmEx(L->cb, CUBLAS_OP_T, CUBLAS_OP_N, HP, b, NETH_CRITIC_NSP, &one, C->w1p, ab, NETH_CRITIC_NSP,
                    C->xb, ab, NETH_CRITIC_NSP, &zero, C->a1, ab, HP, ct, CUBLAS_GEMM_DEFAULT));
    k_c_bias_relu<T><<<(int)(((long)b * HP + 255) / 256), 256, 0, s>>>(b, H, HP, (T *)C->a1, C->w + NETH_CRITIC_B1(C));
    /* a2 [b][HP] = a1 [b][HP] * w2p^T [HP][HP] */
    CB(cublasGemmEx(L->cb, CUBLAS_OP_T, CUBLAS_OP_N, HP, b, HP, &one, C->w2p, ab, HP,
                    C->a1, ab, HP, &zero, C->a2, ab, HP, ct, CUBLAS_GEMM_DEFAULT));
    /* relu(a2 + b2) and out[n * os] += u . a2 + c (8 warps = 256 threads per block) */
    k_c_relu_out<T><<<(b + 7) / 8, 256, 0, s>>>(b, H, HP, (T *)C->a2, C->w + NETH_CRITIC_B2(C), C->w + NETH_CRITIC_U(C), out, os);
}

template <class T>
static void critic_bwd_t(neth_learn *L, neth_critic *C, int b, const int32_t *idx, const neth_sc_t *x, const float *dv, int os) {
    (void)idx;
    (void)x;
    cudaStream_t s = L->s;
    int H = C->H, HP = C->HP;
    const float scale = L->prec == NETH_LEARN_BF16 ? NETH_NET_LSCALE : 1.0f;
    const float one = 1.0f, zero = 0.0f, inv = 1.0f / scale;
    cudaDataType_t ab = L->ab;
    cublasComputeType_t ct = L->prec == NETH_LEARN_BF16 ? CUBLAS_COMPUTE_32F : L->ct;
    k_c_bwd_head<T><<<(int)(((long)b * HP + 255) / 256), 256, 0, s>>>(b, H, HP, (const T *)C->a2, C->w + NETH_CRITIC_U(C),
                                                                       dv, os, scale, (T *)C->d2, C->dvs, (T *)C->dvst);
    /* d1 [b][HP] = d2 [b][HP] * w2p [HP][HP] */
    CB(cublasGemmEx(L->cb, CUBLAS_OP_N, CUBLAS_OP_N, HP, b, HP, &one, C->w2p, ab, HP,
                    C->d2, ab, HP, &zero, C->d1, ab, HP, ct, CUBLAS_GEMM_DEFAULT));
    k_c_relu_bwd<T><<<(int)(((long)b * HP + 255) / 256), 256, 0, s>>>(b, H, HP, (T *)C->d1, (const T *)C->a1);
    /* gW1_pad [HP][NSP] = inv * d1^T [HP][b] * xb [b][NSP] */
    CB(cublasGemmEx(L->cb, CUBLAS_OP_N, CUBLAS_OP_T, NETH_CRITIC_NSP, HP, b, &inv, C->xb, ab, NETH_CRITIC_NSP,
                    C->d1, ab, HP, &zero, C->gw1p, CUDA_R_32F, NETH_CRITIC_NSP, ct, CUBLAS_GEMM_DEFAULT));
    /* gW2 [H][H] = inv * d2^T [HP][b] * a1 [b][HP] */
    float *gw2_dst = (HP == H) ? (C->g + NETH_CRITIC_W2(C)) : C->gw2p;
    CB(cublasGemmEx(L->cb, CUBLAS_OP_N, CUBLAS_OP_T, HP, HP, b, &inv, C->a1, ab, HP,
                    C->d2, ab, HP, &zero, gw2_dst, CUDA_R_32F, HP, ct, CUBLAS_GEMM_DEFAULT));
    long nunp = (long)H * NETH_NET_NS + (HP != H ? (long)H * H : 0);
    k_c_unpad<<<(int)((nunp + 255) / 256), 256, 0, s>>>(H, HP, C->gw1p, HP != H ? C->gw2p : NULL,
                                                        C->g + NETH_CRITIC_W1(C), C->g + NETH_CRITIC_W2(C));
    /* gb1 [H] = inv * ones^T * d1, gb2 [H] = inv * ones^T * d2, gu [H] = inv * dvst^T * a2, gc = sum(dvs) */
    CB(cublasGemmEx(L->cb, CUBLAS_OP_N, CUBLAS_OP_N, H, 1, b, &inv, C->d1, ab, HP,
                    L->ones, ab, b, &zero, C->g + NETH_CRITIC_B1(C), CUDA_R_32F, H, ct, CUBLAS_GEMM_DEFAULT));
    CB(cublasGemmEx(L->cb, CUBLAS_OP_N, CUBLAS_OP_N, H, 1, b, &inv, C->d2, ab, HP,
                    L->ones, ab, b, &zero, C->g + NETH_CRITIC_B2(C), CUDA_R_32F, H, ct, CUBLAS_GEMM_DEFAULT));
    CB(cublasGemmEx(L->cb, CUBLAS_OP_N, CUBLAS_OP_N, H, 1, b, &inv, C->a2, ab, HP,
                    C->dvst, ab, b, &zero, C->g + NETH_CRITIC_U(C), CUDA_R_32F, H, ct, CUBLAS_GEMM_DEFAULT));
    k_c_sumc<<<1, 256, 0, s>>>(b, C->dvs, C->g + NETH_CRITIC_C(C));
}

static void neth_critic_fwd(neth_learn *L, neth_critic *C, int b, const int32_t *idx, const neth_sc_t *x, float *out, int os) {
    if (L->prec == NETH_LEARN_BF16) critic_fwd_t<bf>(L, C, b, idx, x, out, os);
    else critic_fwd_t<float>(L, C, b, idx, x, out, os);
}
static void neth_critic_bwd(neth_learn *L, neth_critic *C, int b, const int32_t *idx, const neth_sc_t *x, const float *dv, int os) {
    if (L->prec == NETH_LEARN_BF16) critic_bwd_t<bf>(L, C, b, idx, x, dv, os);
    else critic_bwd_t<float>(L, C, b, idx, x, dv, os);
}
