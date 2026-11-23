/* The learner's minGRU (net.h NETH_NET_RNN 1; lane/ntgru), included by learn.cu after its GEMM helpers: the gates' GEMM
 * over a minibatch of whole env columns, the elementwise scan over their T steps, and its reverse in the backward.
 * fastppo.py's Small.seq (gru=1) is the reference: G = rnn(z2) for every step at once, then per step h' = h + sigmoid(
 * G_z) (G_h - h) from the stored start state, zeroed where an episode starts, and the heads read z2 + h'.
 *
 * A minibatch is be = b / T env columns in T-major rows: row t * be + k is env column k's step t, idx[row] its sample
 * (t * gs + env in the segment; idx NULL: the row itself, the whole segment's forward), so idx[k] is the env and the
 * step before row r is row r - be. The state at a column's start is h0[env] (the loop stores it already zero where its
 * first step starts an episode); step t > 0 starts from zero where done[step t - 1] (the segment's ends, [T][gs]).
 *
 * The forward keeps G (the GEMM's output in T: bf16 on the 16-bit path, as fastppo's autocast rounds rnn(z); the bias
 * not added), every step's state hs (fp32) and y = z2 + h' (T, the heads' input). The backward's carry is dL/dh
 * through (1 - g), cut where a step starts an episode; the reverse scan also sums each column's bias gradient (fp32,
 * then the columns in order: no column-sum GEMM), and dG W goes into d2 by the GEMM itself (beta 1) where the GEMMs
 * accumulate in fp32. Measured on the 3090 (nsys on learn_bench, lane/ntgru): with G in fp32, a colsum and a separate
 * add, the layer cost 1.0 ms an update at w1 and s1 alike (5.50 against 4.49 ms at w1). */
#if NETH_NET_RNN
#define RD NETH_NET_D
#define RT 32   /* lane/hill-rnn-hotbar: 1 warp per 64 units (2 units/thread, 4*be blocks to saturate all SMs) */
static_assert(RD % 64 == 0, "rnn.cuh requires NETH_NET_D divisible by 64");
static int rnn_grid(long n, int t) { return (int)((n + t - 1) / t); }

struct neth_learn_rnn {
    int T;                      /* the scan's length (neth_learn_update's T; neth_learn_seq's for a lone forward) */
    const uint8_t *done;        /* the segment's ends [T][gs] */
    const float *h0;            /* the segment's start states [gs][D] */
    float *hs, *bpart;          /* [mb][D] each step's state; [mb / T][2D] each column's bias gradient */
    void *G, *y, *dG, *wr;      /* T: [mb][2D] the gates before their bias, [mb][D] z2 + h, [mb][2D] dL/dG,
                                 * the weight [2D][D] */
    int32_t *ref_idx;           /* [ref_n] column-ordered sample indices for neth_learn_ref_forward when n > mb */
    int ref_n, ref_T, ref_mb;
};

static neth_learn_rnn *rnn_new(long mb, size_t es) {
    neth_learn_rnn *R = (neth_learn_rnn *)calloc(1, sizeof *R);
    if (cudaMalloc(&R->G, mb * 2 * RD * es) != cudaSuccess ||
        cudaMalloc((void **)&R->hs, mb * RD * sizeof(float)) != cudaSuccess ||
        cudaMalloc((void **)&R->bpart, mb * 2 * RD * sizeof(float)) != cudaSuccess ||
        cudaMalloc(&R->y, mb * RD * es) != cudaSuccess || cudaMalloc(&R->dG, mb * 2 * RD * es) != cudaSuccess ||
        cudaMalloc(&R->wr, 2 * RD * RD * es) != cudaSuccess) {
        fprintf(stderr, "neth_learn_new: the minGRU's buffers (minibatch %ld): cudaMalloc failed\n", mb);
        return NULL;
    }
    return R;
}

static void rnn_free(neth_learn_rnn *R) {
    if (!R) return;
    cudaFree(R->G), cudaFree(R->hs), cudaFree(R->bpart), cudaFree(R->y), cudaFree(R->dG);
    cudaFree(R->wr), cudaFree(R->ref_idx);
    free(R);
}

template <class T>
__global__ void k_rnn_wcvt(const float *w, T *dst) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < 2L * RD * RD) dst[i] = cvt<T>(w[i]);
}

__device__ __forceinline__ float rnn_sig(float x) { return 1.0f / (1.0f + expf(-x)); }

template <class T> __device__ __forceinline__ float2 rnn_ld2(const T *p);
template <> __device__ __forceinline__ float2 rnn_ld2<float>(const float *p) { return *(const float2 *)p; }
template <> __device__ __forceinline__ float2 rnn_ld2<nt16>(const nt16 *p) { return h22f2(*(const nt162 *)p); }

template <class T> __device__ __forceinline__ void rnn_st2(T *p, float2 v);
template <> __device__ __forceinline__ void rnn_st2<float>(float *p, float2 v) { *(float2 *)p = v; }
template <> __device__ __forceinline__ void rnn_st2<nt16>(nt16 *p, float2 v) { *(nt162 *)p = f22h2(v.x, v.y); }

template <class T>
__global__ void __launch_bounds__(RT) k_rnn_scan(int be, int T_, const int32_t *__restrict__ idx,
                                                 const uint8_t *__restrict__ done, const float *__restrict__ h0,
                                                 const T *__restrict__ G, const float *__restrict__ bias,
                                                 const T *__restrict__ z2, float *__restrict__ hs, T *__restrict__ y) {
    const int nchunks = RD / 64;
    int blk = blockIdx.x, lane = threadIdx.x;
    int k = blk / nchunks, u = (blk % nchunks) * 64 + lane * 2;
    int e0 = 0;
    if (lane == 0) e0 = idx ? idx[k] : k;
    e0 = __shfl_sync(0xffffffffu, e0, 0);
    float2 bz = make_float2(bias[u], bias[u + 1]);
    float2 bh = make_float2(bias[RD + u], bias[RD + u + 1]);
    float2 h = *(const float2 *)(h0 + (long)e0 * RD + u);
    for (int t = 0; t < T_; t++) {
        long r = (long)t * be + k;
        int cut = 0;
        if (t && lane == 0) cut = done[idx ? idx[r - be] : r - be];
        cut = __shfl_sync(0xffffffffu, cut, 0);
        if (cut) h = make_float2(0.0f, 0.0f);
        float2 gz = rnn_ld2(G + r * 2 * RD + u);
        float2 gh = rnn_ld2(G + r * 2 * RD + RD + u);
        float2 zv = rnn_ld2(z2 + r * RD + u);
        float gx = rnn_sig(gz.x + bz.x), gy = rnn_sig(gz.y + bz.y);
        h.x = h.x + gx * ((gh.x + bh.x) - h.x);
        h.y = h.y + gy * ((gh.y + bh.y) - h.y);
        *(float2 *)(hs + r * RD + u) = h;
        rnn_st2(y + r * RD + u, make_float2(zv.x + h.x, zv.y + h.y));
    }
}

/* the reverse scan: dY the heads' input gradient (dL/dy = dL/dh' + the residual's dL/dz2), dG = (dh (h~ - h) g (1 -
 * g), dh g), the carry dh (1 - g) into the step before unless this one starts an episode; the column's bias gradient
 * (its dG summed over its steps, fp32) into bpart[k] */
template <class T>
__global__ void __launch_bounds__(RT) k_rnn_rscan(int be, int T_, const int32_t *__restrict__ idx,
                                                  const uint8_t *__restrict__ done, const float *__restrict__ h0,
                                                  const T *__restrict__ G, const float *__restrict__ bias,
                                                  const float *__restrict__ hs, const T *__restrict__ dY,
                                                  T *__restrict__ dG, float *__restrict__ bpart) {
    const int nchunks = RD / 64;
    int blk = blockIdx.x, lane = threadIdx.x;
    int k = blk / nchunks, u = (blk % nchunks) * 64 + lane * 2;
    int e0 = 0;
    if (lane == 0) e0 = idx ? idx[k] : k;
    e0 = __shfl_sync(0xffffffffu, e0, 0);
    float2 bz = make_float2(bias[u], bias[u + 1]);
    float2 bh = make_float2(bias[RD + u], bias[RD + u + 1]);
    float2 c = make_float2(0.0f, 0.0f), sz = make_float2(0.0f, 0.0f), sh = make_float2(0.0f, 0.0f);
    for (int t = T_ - 1; t >= 0; t--) {
        long r = (long)t * be + k;
        int cut = 0;
        if (t && lane == 0) cut = done[idx ? idx[r - be] : r - be];
        cut = __shfl_sync(0xffffffffu, cut, 0);
        float2 hp = t == 0 ? *(const float2 *)(h0 + (long)e0 * RD + u)
                           : cut ? make_float2(0.0f, 0.0f) : *(const float2 *)(hs + (r - be) * RD + u);
        float2 gz = rnn_ld2(G + r * 2 * RD + u);
        float2 gh = rnn_ld2(G + r * 2 * RD + RD + u);
        float2 dy = rnn_ld2(dY + r * RD + u);
        float gx = rnn_sig(gz.x + bz.x), gy = rnn_sig(gz.y + bz.y);
        float dhx = dy.x + c.x, dhy = dy.y + c.y;
        float dzx = dhx * ((gh.x + bh.x) - hp.x) * (gx * (1.0f - gx)), dcx = dhx * gx;
        float dzy = dhy * ((gh.y + bh.y) - hp.y) * (gy * (1.0f - gy)), dcy = dhy * gy;
        rnn_st2(dG + r * 2 * RD + u, make_float2(dzx, dzy));
        rnn_st2(dG + r * 2 * RD + RD + u, make_float2(dcx, dcy));
        sz.x += dzx, sz.y += dzy;
        sh.x += dcx, sh.y += dcy;
        c = cut ? make_float2(0.0f, 0.0f) : make_float2(dhx * (1.0f - gx), dhy * (1.0f - gy));
    }
    *(float2 *)(bpart + (long)k * 2 * RD + u) = sz;
    *(float2 *)(bpart + (long)k * 2 * RD + RD + u) = sh;
}

/* the bias gradient: each column's partials summed in column order (assigned, as colsum's) */
__global__ void k_rnn_bsum(int be, const float *__restrict__ bpart, float *__restrict__ gb) {
    int c = blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= 2 * RD) return;
    float s = 0.0f;
    for (int k = 0; k < be; k++) s += bpart[(long)k * 2 * RD + c];
    gb[c] = s;
}

/* the forward's dispatch: z2 (relu'd, its bias added) in, the heads' input out (z2 itself without the layer) */
template <class T>
static const T *rnn_forward(neth_learn *L, const float *w, int b, const int32_t *idx, const T *z2) {
    neth_learn_rnn *R = L->rnn;
    if (b % R->T) {
        fprintf(stderr, "learn.cu: a minibatch of %d samples is not whole columns of T %d\n", b, R->T);
        abort();
    }
    int be = b / R->T;
    k_rnn_wcvt<T><<<rnn_grid(2L * RD * RD, 256), 256, 0, L->s>>>(w + NETH_LEARN_OFF(rnn_w), (T *)R->wr);
    gemm(L, 0, 1, b, 2 * RD, RD, z2, RD, R->wr, RD, R->G, L->ab, 2 * RD);
    k_rnn_scan<T><<<be * (RD / 64), RT, 0, L->s>>>(be, R->T, idx, R->done, R->h0, (const T *)R->G,
                                                   w + NETH_LEARN_OFF(rnn_b), z2, R->hs, (T *)R->y);
    return (const T *)R->y;
}

/* the backward's: d2 the heads' input gradient (before relu'), the layer's gradients into grad, d2 += dG W */
template <class T>
static void rnn_backward(neth_learn *L, const float *w, int b, const int32_t *idx, const T *z2, T *d2, float *grad) {
    neth_learn_rnn *R = L->rnn;
    int be = b / R->T;
    k_rnn_rscan<T><<<be * (RD / 64), RT, 0, L->s>>>(be, R->T, idx, R->done, R->h0, (const T *)R->G,
                                                    w + NETH_LEARN_OFF(rnn_b), R->hs, d2, (T *)R->dG, R->bpart);
    k_rnn_bsum<<<rnn_grid(2 * RD, 256), 256, 0, L->s>>>(be, R->bpart, grad + NETH_LEARN_OFF(rnn_b));
    gemm(L, 1, 0, 2 * RD, RD, b, R->dG, 2 * RD, z2, RD, grad + NETH_LEARN_OFF(rnn_w), CUDA_R_32F, RD, 1);
#if NETH_NET_ACC16
    if (L->prec == NETH_LEARN_BF16) {  /* fp16 accumulation: beta=1 fuses d2 += dG W directly (no R->dz / k_rnn_add) */
        const __half one = __float2half(1.0f);
        CB(cublasGemmEx(L->cb, CUBLAS_OP_N, CUBLAS_OP_N, RD, b, 2 * RD, &one, R->wr, CUDA_R_16F, RD, R->dG, CUDA_R_16F,
                        2 * RD, &one, d2, CUDA_R_16F, RD, CUBLAS_COMPUTE_16F, CUBLAS_GEMM_DEFAULT));
        return;
    }
#endif
    /* d2 = 1 dG W + 1 d2 (row-major as gemm's: column-major C^T = W^T dG^T) */
    const float one = 1.0f;
    CB(cublasGemmEx(L->cb, CUBLAS_OP_N, CUBLAS_OP_N, RD, b, 2 * RD, &one, R->wr, L->ab, RD, R->dG, L->ab, 2 * RD, &one,
                    d2, L->ab, RD, L->ct, CUBLAS_GEMM_DEFAULT));
}

/* the heads' input the forward made: y, or z2 without the layer */
template <class T>
static const T *rnn_y(neth_learn *L, const T *z2) { return (const T *)L->rnn->y; }
#undef RD
#undef RT
#else
struct neth_learn_rnn;
static neth_learn_rnn *rnn_new(long, size_t) { return NULL; }
static void rnn_free(neth_learn_rnn *) {}
template <class T>
static const T *rnn_forward(neth_learn *, const float *, int, const int32_t *, const T *z2) { return z2; }
template <class T>
static void rnn_backward(neth_learn *, const float *, int, const int32_t *, const T *, T *, float *) {}
template <class T>
static const T *rnn_y(neth_learn *, const T *z2) { return z2; }
#endif
