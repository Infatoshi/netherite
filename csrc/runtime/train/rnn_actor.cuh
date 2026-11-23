/* The actor's minGRU step (net.h NETH_NET_RNN 1; lane/ntgru), included by actor.cu: the gates' GEMM with the state update in
 * its epilogue, one kernel. A block takes RA_S samples' z2 (rounded to bf16 on the bf16 path, as a tensor core's
 * inputs are, and as fastppo's autocast z is) into shared memory, and each of its RA_U threads one unit u of the 2D
 * gates over k (the gate's column u and the candidate's D + u, fp32 sums), then h' = h + sigmoid(G_z + b_z) (G_h + b_h
 * - h) from the env's state (zero where first), written back in place, and y = z2 + h' (the heads' input; heads_draw
 * rounds it to bf16 on that path, as torch's (z.float() + h).to(bf16)). fastppo.py's Small.step (gru=1) is the
 * reference. */
#if NETH_NET_RNN
/* a block: RA_S samples and RA_U units (their 2 RA_U gate columns), RA_K k-slices of RA_U threads (a warp a slice's
 * units at RA_U 32: each weight row's load coalesced); each thread sums its slice's k (every RA_K-th) for the block's
 * samples, the slices' partials summed in order in shared memory, then a thread a (sample, unit) the update. The first
 * version (one thread a unit over all k, 32 blocks at a group of 128) took 25 us of a 57.8 us w1 call */
#define RA_S 8      /* samples a block */
#define RA_U 32     /* units a block */
#define RA_K 8      /* k-slices */
#define RA_T (RA_U * RA_K)
static_assert(NETH_NET_D % RA_U == 0 && NETH_NET_D % RA_K == 0 && RA_T == RA_S * RA_U, "the minGRU step's tiles");

template <bool BF>
__global__ void __launch_bounds__(RA_T) k_rnn_step(const float *__restrict__ z2, const void *__restrict__ rw,
                                                   const float *__restrict__ rb, int B, float *__restrict__ h,
                                                   const uint8_t *__restrict__ first, float *__restrict__ y)
{
    __shared__ float zs[RA_S][NETH_NET_D];
    __shared__ float part[RA_K][RA_S][2 * RA_U];
    int b0 = blockIdx.x * RA_S, ns = min(RA_S, B - b0), u0 = blockIdx.y * RA_U, t = threadIdx.x;
    for (int i = t; i < RA_S * NETH_NET_D; i += RA_T) {
        int j = i / NETH_NET_D, k = i % NETH_NET_D;
        float v = j < ns ? z2[(size_t)(b0 + j) * NETH_NET_D + k] : 0.f;
        zs[j][k] = BF ? h2f(f2h(v)) : v;
    }
    __syncthreads();
    int ul = t % RA_U, ks = t / RA_U, u = u0 + ul;
    float az[RA_S], ah[RA_S];
#pragma unroll
    for (int j = 0; j < RA_S; j++)
        az[j] = ah[j] = 0.f;
#pragma unroll 4
    for (int k = ks; k < NETH_NET_D; k += RA_K) {
        float wz, wh;
        if constexpr (BF) {
            wz = h2f(((const nt16 *)rw)[(size_t)k * 2 * NETH_NET_D + u]);
            wh = h2f(((const nt16 *)rw)[(size_t)k * 2 * NETH_NET_D + NETH_NET_D + u]);
        } else {
            wz = ((const float *)rw)[(size_t)k * 2 * NETH_NET_D + u];
            wh = ((const float *)rw)[(size_t)k * 2 * NETH_NET_D + NETH_NET_D + u];
        }
#pragma unroll
        for (int j = 0; j < RA_S; j++) {
            az[j] = fmaf(zs[j][k], wz, az[j]);
            ah[j] = fmaf(zs[j][k], wh, ah[j]);
        }
    }
#pragma unroll
    for (int j = 0; j < RA_S; j++) {
        part[ks][j][ul] = az[j];
        part[ks][j][RA_U + ul] = ah[j];
    }
    __syncthreads();
    int j = t / RA_U;   /* the update: a thread a (sample j, unit ul) */
    if (j >= ns)
        return;
    float sz = 0.f, sh = 0.f;
#pragma unroll
    for (int q = 0; q < RA_K; q++)
        sz += part[q][j][ul], sh += part[q][j][RA_U + ul];
    size_t i = (size_t)(b0 + j) * NETH_NET_D + u;
    float hp = first && first[b0 + j] ? 0.f : h[i];
    float g = 1.f / (1.f + expf(-(sz + rb[u]))), hn = hp + g * ((sh + rb[NETH_NET_D + u]) - hp);
    h[i] = hn;
    y[i] = zs[j][u] + hn;
}

/* the heads' input: y after the step (h and first from io), or z2 without the layer */
static const float *rnn_step(neth_actor_ws *w, const neth_actor *a, int B, const neth_actor_io *io, cudaStream_t s, bool bf)
{
    dim3 g((B + RA_S - 1) / RA_S, NETH_NET_D / RA_U);
    if (bf)
        k_rnn_step<true><<<g, RA_T, 0, s>>>(w->z2, a->rwh, a->rb, B, io->h, io->first, w->y);
    else
        k_rnn_step<false><<<g, RA_T, 0, s>>>(w->z2, a->rw, a->rb, B, io->h, io->first, w->y);
    return w->y;
}
#else
static const float *rnn_step(neth_actor_ws *w, const neth_actor *, int, const neth_actor_io *, cudaStream_t, bool)
{
    return w->z2;
}
#endif
