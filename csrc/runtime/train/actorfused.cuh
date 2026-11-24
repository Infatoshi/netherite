/* The actor's bf16 step as one kernel, k_afused (lane/ntfc, NETH_ACTOR_FUSE 1; included by actor.cu after heads_draw, whose
 * helpers it uses: philox's uniform, logsig, nt16 and prec.cuh's mma16816). 16x16 at wm 1 (net.h's default) only:
 * the activations of 8, 4 or 2 samples a block (AF_SBFOR: by the call's size) fit in shared memory. (fc1, fc2, the
 * heads and the draw alone as one kernel after the per-layer convolutions, 16 samples a block, was slower than the
 * per-layer kernels at every size the trainer calls: 4 to 8 blocks for the whole MLP; DEVLOG lane/ntfc.)
 *
 * k_afused takes SB samples from the cast's bytes to their draw: conv1 (its A fragments built in registers from the
 * bytes, B from a shared copy of the [48][32] weights), conv2 and conv3 (A by ldmatrix from the previous map, B from
 * the image's gemm_mmr fragments), then af_tail: fc1, fc2 (the same), the heads (B from hwh, [D][112], two 16-bit
 * loads a pair) and heads_draw's sampler, a warp a (sample, head). Maps are kept unpadded, a 16-byte chunk of
 * channels a slot, swizzled by the pixel (chunk ^ (pixel >> 1)); an out-of-map tap reads a zero row. The device
 * RNG's counter: k_afused's blocks read it first and draw at counter + 1, and the last block to finish (a ticket in
 * rng[2]) stores counter + 1, as pre_conv1's increment does before heads_draw reads it. */
#ifndef NETH_ACTOR_FUSED_CUH
#define NETH_ACTOR_FUSED_CUH

#if NETH_NET_STEM || NETH_NET_WM != 1
#error "actorfused.cuh: 16x16 at wm 1 only (actor.cu turns NETH_ACTOR_FUSE off elsewhere)"
#endif
#define AF_W 8                     /* warps */
#define AF_T (AF_W * 32)
/* lane/audit-feat: F, fc1's K (actor.cu's F1K, the run's scalar row padded to 64), a template parameter of k_afused and
 * af_tail: one instantiation each of net.h's NETH_NET_NSP_CASES */
#define AF_LZ0(F) ((F) + 8)         /* z0's row (bf16) */
#define AF_LZ (NETH_NET_D + 8)
#define AF_LH (NETH_NET_HP + 4)           /* the logits' row (fp32) */
#define AF_FN (NETH_NET_D / 128)          /* a warp's n16 tiles of fc1 and fc2 */
#define AF_Z0(F) (16 * AF_LZ0(F) * 2)
#define AF_Z (16 * AF_LZ * 2)
static_assert(16 * AF_LH * 4 <= AF_Z0(NETH_NET_FLAT0 + 192) && NETH_NET_D % 128 == 0, "the logits in z0's place");
/* k_afused's shared memory, bytes: the bytes; conv1's weights; x1 [SB][64 px][32] (then z0, z1, z2, the logits); x2
 * [SB][16][64]; a zero row */
#define AF_OB(SB) ((SB) * NETH_NET_H * NETH_NET_W * NETH_NET_C)
#define AF_W1 (48 * NETH_NET_C1 * 2)
#define AF_X1(SB, F) ((SB) * 64 * NETH_NET_C1 * 2 > AF_Z0(F) + 2 * AF_Z ? (SB) * 64 * NETH_NET_C1 * 2 : AF_Z0(F) + 2 * AF_Z)  /* or z0..z2 */
#define AF_X2(SB) ((SB) * 16 * NETH_NET_C2 * 2)
#define AF_SMEMF(SB, F) (AF_OB(SB) + AF_W1 + AF_X1(SB, F) + AF_X2(SB) + 128)
/* the samples a block for a call of B: the SMs filled at small groups (the trainer's 128 at 16x16) */
#define AF_SBFOR(B) ((B) >= 512 ? 8 : (B) >= 256 ? 4 : 2)

/* a pixel's chunk q (8 channels) of a map with C channels: its slot */
template <int C>
__device__ __forceinline__ int af_slot(int px, int q) { return px * C + ((q ^ (px >> 1)) & (C / 8 - 1)) * 8; }

/* acc += A B over KST k-steps for MT m-tiles and FN n16 tiles: arow(r, kc) the 16-byte A row address (shared) of the
 * warp's row r at column kc (a multiple of 8), B the gemm_mmr fragments of the warp's first n16 tile (tile f at
 * B + f KST 32) */
template <int MT, int FN, int KST, class AR>
__device__ __forceinline__ void af_mma(acc4 (&acc)[MT][2 * FN], AR arow, const uint4 *__restrict__ B, int lane) {
    constexpr int PF = KST < 4 ? KST : 4;
    uint4 b[PF][FN];
#pragma unroll
    for (int i = 0; i < PF; i++)
#pragma unroll
        for (int f = 0; f < FN; f++) b[i][f] = __ldg(B + ((long)f * KST + i) * 32 + lane);
#pragma unroll 1
    for (int s0 = 0; s0 < KST; s0 += PF) {
#pragma unroll
        for (int i = 0; i < PF; i++) {
            int s = s0 + i;
            if (KST % PF == 0 || s < KST) {
                uint32_t a[MT][4];
#pragma unroll
                for (int mt = 0; mt < MT; mt++) {
                    unsigned p = (unsigned)__cvta_generic_to_shared(arow(mt * 16 + (lane & 15), s * 16 + (lane >> 4) * 8));
                    asm volatile("ldmatrix.sync.aligned.m8n8.x4.shared.b16 {%0, %1, %2, %3}, [%4];\n"
                                 : "=r"(a[mt][0]), "=r"(a[mt][1]), "=r"(a[mt][2]), "=r"(a[mt][3]) : "r"(p));
                }
#pragma unroll
                for (int f = 0; f < FN; f++) {
                    const uint32_t b0[2] = {b[i][f].x, b[i][f].y}, b1[2] = {b[i][f].z, b[i][f].w};
#pragma unroll
                    for (int mt = 0; mt < MT; mt++) {
                        mma16816(acc[mt][2 * f], a[mt], b0);
                        mma16816(acc[mt][2 * f + 1], a[mt], b1);
                    }
                }
                acc_step(acc, s);
                if (s + PF < KST)
#pragma unroll
                    for (int f = 0; f < FN; f++) b[i][f] = __ldg(B + ((long)f * KST + s + PF) * 32 + lane);
            }
        }
    }
    acc_fin(acc);
}

__device__ __forceinline__ uint32_t af_pack(float lo, float hi) {
    nt162 v = f22h2(lo, hi);
    return *(uint32_t *)&v;
}

/* B from [K][ldb] 16-bit rows (row-major k by n): the m16n8k16 fragment pair of n8 tile n0 at k-step s */
__device__ __forceinline__ void af_bkn(uint32_t *b, const nt16 *W, int ldb, int s, int n0, int lane) {
    int k = s * 16 + (lane & 3) * 2, n = n0 + (lane >> 2);
    nt16 v[4] = {W[k * ldb + n], W[(k + 1) * ldb + n], W[(k + 8) * ldb + n], W[(k + 9) * ldb + n]};
    b[0] = *(uint32_t *)&v[0];
    b[1] = *(uint32_t *)&v[2];
}

struct af_w {
    const nt16 *w1h, *hwh;
    const float *b1, *b2, *b3, *fb1, *fb2, *hb;
    const uint4 *wf2, *wf3, *wf1, *wff2;
};

/* fc1, fc2, the heads and the draw for the ns samples from b0 whose z0 rows (16 of them, those past ns finite) are in
 * z0s; z1s and z2s 16 rows; the logits lg may be z0s */
template <int F>
__device__ __forceinline__ void af_tail(const af_w &W, const nt16 *z0s, nt16 *z1s, nt16 *z2s, float *lg,
                                        float (*lpp)[3], int b0, int ns, const neth_actor_io &io,
                                        unsigned long long seed, unsigned long long ctr) {
    const int t = threadIdx.x, lane = t & 31, w = t >> 5, g = lane >> 2, tq = (lane & 3) * 2;
    /* ---- fc1, fc2: one m-tile, a warp D / 8 columns ---- */
    {
        acc4 acc[1][2 * AF_FN];
#pragma unroll
        for (int q = 0; q < 2 * AF_FN; q++) acc[0][q] = acc4{};
        auto arow = [&](int r, int kc) -> const nt16 * { return z0s + r * AF_LZ0(F) + kc; };
        af_mma<1, AF_FN, F / 16>(acc, arow, W.wf1 + (long)(w * AF_FN) * (F / 16) * 32, lane);
#pragma unroll
        for (int q = 0; q < 2 * AF_FN; q++) {
            int n = w * (NETH_NET_D / 8) + q * 8 + tq;
#pragma unroll
            for (int hh = 0; hh < 2; hh++)
                *(nt162 *)(z1s + (g + hh * 8) * AF_LZ + n) =
                    f22h2(fmaxf(acc[0][q][2 * hh] + W.fb1[n], 0.0f), fmaxf(acc[0][q][2 * hh + 1] + W.fb1[n + 1], 0.0f));
        }
    }
    __syncthreads();
    {
        acc4 acc[1][2 * AF_FN];
#pragma unroll
        for (int q = 0; q < 2 * AF_FN; q++) acc[0][q] = acc4{};
        auto arow = [&](int r, int kc) -> const nt16 * { return z1s + r * AF_LZ + kc; };
        af_mma<1, AF_FN, NETH_NET_D / 16>(acc, arow, W.wff2 + (long)(w * AF_FN) * (NETH_NET_D / 16) * 32, lane);
#pragma unroll
        for (int q = 0; q < 2 * AF_FN; q++) {
            int n = w * (NETH_NET_D / 8) + q * 8 + tq;
#pragma unroll
            for (int hh = 0; hh < 2; hh++)
                *(nt162 *)(z2s + (g + hh * 8) * AF_LZ + n) =
                    f22h2(fmaxf(acc[0][q][2 * hh] + W.fb2[n], 0.0f), fmaxf(acc[0][q][2 * hh + 1] + W.fb2[n + 1], 0.0f));
        }
    }
    __syncthreads();

    /* ---- the heads: warps 0..6 a 16-column tile, the logits fp32 ---- */
    if (w < NETH_NET_HP / 16) {
        acc4 acc[2] = {};
        const nt16 *ap = z2s + (lane & 15) * AF_LZ + (lane >> 4) * 8;
#pragma unroll 4
        for (int s = 0; s < NETH_NET_D / 16; s++) {
            uint32_t a[4], bb[2][2];
            unsigned p = (unsigned)__cvta_generic_to_shared(ap + s * 16);
            asm volatile("ldmatrix.sync.aligned.m8n8.x4.shared.b16 {%0, %1, %2, %3}, [%4];\n"
                         : "=r"(a[0]), "=r"(a[1]), "=r"(a[2]), "=r"(a[3]) : "r"(p));
            af_bkn(bb[0], W.hwh, NETH_NET_HP, s, w * 16, lane);
            af_bkn(bb[1], W.hwh, NETH_NET_HP, s, w * 16 + 8, lane);
            mma16816(acc[0], a, bb[0]);
            mma16816(acc[1], a, bb[1]);
            acc_step(acc, s);
        }
        acc_fin(acc);
#pragma unroll
        for (int q = 0; q < 2; q++) {
            int n = w * 16 + q * 8 + tq;
#pragma unroll
            for (int hh = 0; hh < 2; hh++) {
                lg[(g + hh * 8) * AF_LH + n] = acc[q][2 * hh] + W.hb[n];
                lg[(g + hh * 8) * AF_LH + n + 1] = acc[q][2 * hh + 1] + W.hb[n + 1];
            }
        }
    }
    __syncthreads();

    /* ---- the draw: heads_draw's, a warp a (sample, head) ---- */
    for (int task = w; task < 3 * ns; task += AF_W) {
        int s = task / 3, h = task % 3, b = b0 + s;
        const float *l = lg + s * AF_LH;
        int e0 = h < NETH_NET_CAM ? h * NETH_NET_BINS + lane : NETH_NET_CAM * NETH_NET_BINS + lane, e1 = h * NETH_NET_BINS + lane + 32;
        bool has0 = h < NETH_NET_CAM || lane < NETH_NET_KEYS, has1 = h < NETH_NET_CAM && lane + 32 < NETH_NET_BINS;
        float u0 = 0.5f, u1 = 0.5f;
        if (io.ucam) {
            if (has0) u0 = h < NETH_NET_CAM ? io.ucam[b * NETH_NET_CAM * NETH_NET_BINS + e0] : io.ukey[b * NETH_NET_KEYS + lane];
            if (has1) u1 = io.ucam[b * NETH_NET_CAM * NETH_NET_BINS + e1];
        } else {
            unsigned long long r2[2] = {seed, ctr};
            if (has0) u0 = uniform(r2, b, e0);
            if (has1) u1 = uniform(r2, b, e1);
        }
        float lp;
        if (h < NETH_NET_CAM) {
            const float *lh = l + h * NETH_NET_BINS;
            float x0 = lh[lane], x1 = has1 ? lh[lane + 32] : -INFINITY;
            float g0 = x0 - logf(-logf(u0)), gb = g0, mx = fmaxf(x0, x1);
            int gi = lane;
            if (has1) {
                float g1 = x1 - logf(-logf(u1));
                if (g1 > gb) gb = g1, gi = lane + 32;
            }
#pragma unroll
            for (int o = 16; o > 0; o >>= 1) {
                float og = __shfl_xor_sync(0xffffffffu, gb, o);
                int oi = __shfl_xor_sync(0xffffffffu, gi, o);
                if (og > gb || (og == gb && oi < gi)) gb = og, gi = oi;
                mx = fmaxf(mx, __shfl_xor_sync(0xffffffffu, mx, o));
            }
            float se = expf(x0 - mx) + (has1 ? expf(x1 - mx) : 0.f);
#pragma unroll
            for (int o = 16; o > 0; o >>= 1) se += __shfl_xor_sync(0xffffffffu, se, o);
            lp = (lh[gi] - mx) - logf(se);
            if (lane == 0) io.act[b * NETH_NET_ACTION + h] = gi;
            if (io.cam)
                for (int j = lane; j < NETH_NET_BINS; j += 32) io.cam[b * NETH_NET_CAM * NETH_NET_BINS + h * NETH_NET_BINS + j] = lh[j];
        } else {
            float lk = 0.f;
            if (lane < NETH_NET_KEYS) {
                float x = l[NETH_NET_CAM * NETH_NET_BINS + lane];
                int key = u0 < 1.f / (1.f + expf(-x));
                lk = key ? logsig(x) : logsig(-x);
                io.act[b * NETH_NET_ACTION + NETH_NET_CAM + lane] = key;
                if (io.keys) io.keys[b * NETH_NET_KEYS + lane] = x;
            }
#pragma unroll
            for (int o = 16; o > 0; o >>= 1) lk += __shfl_xor_sync(0xffffffffu, lk, o);
            lp = lk;
            if (lane == 0) io.value[b] = l[NETH_NET_HEADS - 1];
        }
        if (lane == 0) lpp[s][h] = lp;
    }
    __syncthreads();
    if (t < ns) io.logp[b0 + t] = (lpp[t][0] + lpp[t][1]) + lpp[t][2];
}

/* SB samples a block (8, 4 or 2): conv1 SB / 2 m-tiles a warp, conv2 two row groups of SB / 2 m-tiles, conv3 the
 * (SB * 4 + 15) / 16 m-tiles of its rows */
template <int SB, int F>
__global__ void __launch_bounds__(AF_T, 1) k_afused(af_w W, int B, neth_actor_io io, unsigned long long *rng) {
    static_assert(SB == 2 || SB == 4 || SB == 8, "k_afused's samples a block");
    constexpr int M1 = SB / 2, M2 = SB / 2, M3 = (SB * 4 + 15) / 16;
    extern __shared__ __align__(16) unsigned char asm_[];
    uint8_t *ob = asm_;
    nt16 *w1s = (nt16 *)(asm_ + AF_OB(SB));
    nt16 *x1s = (nt16 *)(asm_ + AF_OB(SB) + AF_W1);
    nt16 *z0s = x1s, *z1s = (nt16 *)((unsigned char *)x1s + AF_Z0(F)), *z2s = (nt16 *)((unsigned char *)x1s + AF_Z0(F) + AF_Z);
    float *lg = (float *)x1s;
    nt16 *x2s = (nt16 *)(asm_ + AF_OB(SB) + AF_W1 + AF_X1(SB, F));
    nt16 *zr = (nt16 *)(asm_ + AF_OB(SB) + AF_W1 + AF_X1(SB, F) + AF_X2(SB));   /* 128 zero bytes */
    __shared__ float lpp[SB][3];
    const int t = threadIdx.x, lane = t & 31, w = t >> 5, g = lane >> 2, tq = (lane & 3) * 2;
    const int b0 = blockIdx.x * SB, ns = min(SB, B - b0);
    const bool dev_rng = !(io.ucam && io.ukey);
    unsigned long long seed = 0, ctr = 0;
    if (dev_rng) seed = rng[0], ctr = rng[1] + 1;

    /* the bytes (16 a thread), conv1's weights, the zero row */
    for (int i = t; i < AF_OB(SB) / 16; i += AF_T)
        ((uint4 *)ob)[i] = i * 16 < ns * NETH_NET_H * NETH_NET_W * NETH_NET_C ? __ldg((const uint4 *)(io.obs + (long)b0 * NETH_NET_H * NETH_NET_W * NETH_NET_C) + i)
                                                           : make_uint4(0, 0, 0, 0);
    for (int i = t; i < AF_W1 / 16; i += AF_T) ((uint4 *)w1s)[i] = __ldg((const uint4 *)W.w1h + i);
    if (t < 8) ((uint4 *)zr)[t] = make_uint4(0, 0, 0, 0);
    __syncthreads();

    /* ---- conv1: rows (s, oy, ox) of 8 x 8 a sample, 32 channels, k = (kh * 4 + kw) * 3 + ci of 48; a warp 4 m-tiles
     * x 32 columns ---- */
    {
        acc4 acc[M1][4];
#pragma unroll
        for (int mt = 0; mt < M1; mt++)
#pragma unroll
            for (int q = 0; q < 4; q++) acc[mt][q] = acc4{};
        auto av = [&](int m, int k) -> float {
            int s = m >> 6, oy = (m >> 3) & 7, ox = m & 7, ci = k % 3, c = k / 3;
            int iy = 2 * oy - 1 + (c >> 2), ix = 2 * ox - 1 + (c & 3);
            return iy >= 0 && iy < NETH_NET_H && ix >= 0 && ix < NETH_NET_W ? (float)ob[((s * NETH_NET_H + iy) * NETH_NET_W + ix) * NETH_NET_C + ci] * (1.0f / 255)
                                                                 : 0.0f;
        };
#pragma unroll
        for (int s = 0; s < 3; s++) {
            uint32_t bb[4][2];
#pragma unroll
            for (int q = 0; q < 4; q++) af_bkn(bb[q], w1s, NETH_NET_C1, s, q * 8, lane);
#pragma unroll
            for (int mt = 0; mt < M1; mt++) {
                int m = (w * M1 + mt) * 16 + g, k = s * 16 + tq;
                uint32_t a[4] = {af_pack(av(m, k), av(m, k + 1)), af_pack(av(m + 8, k), av(m + 8, k + 1)),
                                 af_pack(av(m, k + 8), av(m, k + 9)), af_pack(av(m + 8, k + 8), av(m + 8, k + 9))};
#pragma unroll
                for (int q = 0; q < 4; q++) mma16816(acc[mt][q], a, bb[q]);
            }
            acc_step(acc, s);
        }
        acc_fin(acc);
#pragma unroll
        for (int mt = 0; mt < M1; mt++)
#pragma unroll
            for (int q = 0; q < 4; q++) {
                int n = q * 8 + tq;
#pragma unroll
                for (int hh = 0; hh < 2; hh++) {
                    int m = (w * M1 + mt) * 16 + g + hh * 8;
                    *(nt162 *)(x1s + (m >> 6) * 64 * NETH_NET_C1 + af_slot<NETH_NET_C1>(m & 63, n >> 3) + (n & 7)) =
                        f22h2(fmaxf(acc[mt][q][2 * hh] + W.b1[n], 0.0f), fmaxf(acc[mt][q][2 * hh + 1] + W.b1[n + 1], 0.0f));
                }
            }
    }
    __syncthreads();

    /* ---- conv2: rows (s, oy, ox) of 4 x 4, 64 channels, k = (kh * 4 + kw) * 32 + ci of 512; warps 2 (4 m-tiles) x
     * 4 n16 tiles ---- */
    {
        acc4 acc[M2][2];
#pragma unroll
        for (int mt = 0; mt < M2; mt++) acc[mt][0] = acc[mt][1] = acc4{};
        const int mg = w >> 2, nt = w & 3;
        auto arow = [&](int r, int kc) -> const nt16 * {
            int m = mg * (M2 * 16) + r, s = m >> 4, oy = (m >> 2) & 3, ox = m & 3, c = kc >> 5;
            int iy = 2 * oy - 1 + (c >> 2), ix = 2 * ox - 1 + (c & 3);
            if (iy < 0 || iy >= 8 || ix < 0 || ix >= 8) return zr;
            return x1s + s * 64 * NETH_NET_C1 + af_slot<NETH_NET_C1>(iy * 8 + ix, (kc & 31) >> 3);
        };
        af_mma<M2, 1, 512 / 16>(acc, arow, W.wf2 + (long)nt * 32 * 32, lane);
#pragma unroll
        for (int mt = 0; mt < M2; mt++)
#pragma unroll
            for (int q = 0; q < 2; q++) {
                int n = nt * 16 + q * 8 + tq;
#pragma unroll
                for (int hh = 0; hh < 2; hh++) {
                    int m = mg * (M2 * 16) + mt * 16 + g + hh * 8;
                    *(nt162 *)(x2s + (m >> 4) * 16 * NETH_NET_C2 + af_slot<NETH_NET_C2>(m & 15, n >> 3) + (n & 7)) =
                        f22h2(fmaxf(acc[mt][q][2 * hh] + W.b2[n], 0.0f), fmaxf(acc[mt][q][2 * hh + 1] + W.b2[n + 1], 0.0f));
                }
            }
    }
    __syncthreads();

    /* ---- conv3: rows (s, oy, ox) of 2 x 2, 64 channels, k = (kh * 3 + kw) * 64 + ci of 576, into z0 (q * 64 + c),
     * the scalars after; warps 2 m-tiles x 4 n16 ---- */
    for (int i = t; i < 16 * (F - NETH_NET_C3 * Q3); i += AF_T) {
        int s = i / (F - NETH_NET_C3 * Q3), j = i % (F - NETH_NET_C3 * Q3);
        z0s[s * AF_LZ0(F) + NETH_NET_C3 * Q3 + j] = f2h(s < ns && j < NETH_NET_NS ? neth_sc_f32(io.sc[(long)(b0 + s) * NETH_NET_NS + j]) : 0.0f);
    }
    if ((w >> 2) < M3) {
        acc4 acc[1][2];
        acc[0][0] = acc[0][1] = acc4{};
        const int mt = w >> 2, nt = w & 3;
        auto arow = [&](int r, int kc) -> const nt16 * {
            int m = mt * 16 + r, s = m >> 2, oy = (m >> 1) & 1, ox = m & 1, c = kc >> 6;
            int iy = 2 * oy - 1 + c / 3, ix = 2 * ox - 1 + c % 3;
            if (s >= SB || iy < 0 || iy >= 4 || ix < 0 || ix >= 4) return zr;
            return x2s + s * 16 * NETH_NET_C2 + af_slot<NETH_NET_C2>(iy * 4 + ix, (kc & 63) >> 3);
        };
        af_mma<1, 1, 576 / 16>(acc, arow, W.wf3 + (long)nt * 36 * 32, lane);
#pragma unroll
        for (int q = 0; q < 2; q++) {
            int n = nt * 16 + q * 8 + tq;
#pragma unroll
            for (int hh = 0; hh < 2; hh++) {
                int m = mt * 16 + g + hh * 8, s = m >> 2;
                float v0 = fmaxf(acc[0][q][2 * hh] + W.b3[n], 0.0f), v1 = fmaxf(acc[0][q][2 * hh + 1] + W.b3[n + 1], 0.0f);
                *(nt162 *)(z0s + s * AF_LZ0(F) + (m & 3) * NETH_NET_C3 + n) = f22h2(s < ns ? v0 : 0.0f, s < ns ? v1 : 0.0f);
            }
        }
    }
    __syncthreads();
    af_tail<F>(W, z0s, z1s, z2s, lg, lpp, b0, ns, io, seed, ctr);
    /* the counter: the last block to finish stores counter + 1 (every block read it above) */
    if (dev_rng && t == 0) {
        __threadfence();
        unsigned long long k = atomicAdd(&rng[2], 1ull);
        if (k == gridDim.x - 1) {
            rng[1] = ctr;
            rng[2] = 0;
            __threadfence();
        }
    }
}

#endif
