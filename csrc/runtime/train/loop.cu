/* The loop's two policies (loop.h): vec_bench's stand-in on the device and the no-op. The stand-in is vec_bench.c's
 * draw() at --policy 1, one thread an env, its xorshift state kept on the device between steps. */
#include <stdlib.h>
#include <cuda_bf16.h>

#include "loop.h"
#include "net.h"

#if NETH_SC_BF16
__device__ __forceinline__ neth_sc_t loop_f32_sc(float x) { return __bfloat16_as_ushort(__float2bfloat16_rn(x)); }
#else
__device__ __forceinline__ neth_sc_t loop_f32_sc(float x) { return x; }
#endif

struct standin {
    uint64_t *rng;      /* [n] on the device */
    int n;
    int gui;            /* lane/ladder10: neth_policy_standin_gui's key 12 */
};

__device__ static uint64_t xs(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

/* p in 1/1024 */
__device__ static int coin(uint64_t *s, int p) { return (int)(xs(s) & 1023) < p; }

__global__ void loop_standin(uint64_t *rng, int base, int n, int32_t *act, float *logp, float *value, int gui)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    uint64_t s = rng[base + i];
    int32_t *a = act + (size_t)i * NETH_NET_ACTION;
    /* the camera: the centre bin half the time, else within 4 of it */
    for (int j = 0; j < 2; ++j) a[j] = coin(&s, 512) ? 20 : 16 + (int)(xs(&s) % 9);
    const int p[14] = {700, 40, 60, 60, 200, 100, 20, 500, 80, 20, 10, 100, 0, 0};
    /* lane/ladder10 (gui): key 12, actions.gui cursor's inventory key, at 20 in 1024 (the same draws either way) */
    for (int j = 0; j < 14; ++j) a[2 + j] = coin(&s, j == 12 && gui ? 20 : p[j]);
#if NETH_NET_HOTBAR
    a[NETH_NET_CAM + NETH_NET_KEYS] = 0;
#endif
    rng[base + i] = s;
    if (logp) logp[i] = 0;
    if (value) value[i] = 0;
}

static int standin_launch(void *ctx, const struct neth_io *io, cudaStream_t st)
{
    struct standin *c = (struct standin *)ctx;
    if (io->base < 0 || io->base + io->n > c->n) return -1;
    loop_standin<<<(io->n + 127) / 128, 128, 0, st>>>(c->rng, io->base, io->n, io->actions, io->logp, io->value,
                                                       c->gui);
    return cudaGetLastError() == cudaSuccess ? 0 : -1;
}

static void standin_release(void *ctx)
{
    struct standin *c = (struct standin *)ctx;
    cudaFree(c->rng);
    free(c);
}

extern "C" int neth_policy_standin(struct neth_policy *p, int n, uint64_t seed)
{
    struct standin *c = (struct standin *)calloc(1, sizeof *c);
    uint64_t *h = (uint64_t *)malloc((size_t)n * sizeof *h);
    if (c == NULL || h == NULL) return free(c), free(h), -1;
    /* vec_bench's seeding */
    for (int i = 0; i < n; ++i) h[i] = (seed * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)(i + 1) * 0xbf58476d1ce4e5b9ull);
    c->n = n;
    int bad = cudaMalloc(&c->rng, (size_t)n * sizeof *h) != cudaSuccess ||
              cudaMemcpy(c->rng, h, (size_t)n * sizeof *h, cudaMemcpyHostToDevice) != cudaSuccess;
    free(h);
    if (bad) return standin_release(c), -1;
    p->name = "standin";
    p->ctx = c;
    p->launch = standin_launch;
    p->release = standin_release;
    return 0;
}

/* lane/ladder10: the stand-in with key 12 (actions.gui cursor's inventory key) held at 20 in 1024 */
extern "C" int neth_policy_standin_gui(struct neth_policy *p, int n, uint64_t seed)
{
    if (neth_policy_standin(p, n, seed)) return -1;
    ((struct standin *)p->ctx)->gui = 1;
    p->name = "standin-gui";
    return 0;
}

static int noop_launch(void *ctx, const struct neth_io *io, cudaStream_t st) { return 0; }
static void noop_release(void *ctx) {}

extern "C" int neth_policy_noop(struct neth_policy *p)
{
    p->name = "noop";
    p->ctx = NULL;
    p->launch = noop_launch;
    p->release = noop_release;
    return 0;
}

/* --stage 1 (lane/ntcast): a group step's inputs from the env's page-locked buffers (mapped: read in place over the
 * bus) into the segment in one launch, in place of up to five copies: the last action's reward, end and driver at
 * step t - 1 (rew, done and drv NULL at a segment's first step), the scalars and, with the cast on the host, the
 * observation at step t (obs NULL: the device's cast fills it). A block an env. lane/audit-fixes: the termination
 * beside the end (term, NULL with rew). lane/audit-feat: ns the run's scalars (net.h NETH_NET_NS, the host's) */
__global__ void neth_stage_k(const float *rew_h, const uint8_t *done_h, const uint8_t *drv_h, const uint8_t *term_h,
                           const float *sc_h, const uint8_t *obs_h, int lo, size_t oe, float *rew, uint8_t *done,
                           uint8_t *drv, uint8_t *term, neth_sc_t *sc, uint8_t *obs, int ns)
{
    const int i = blockIdx.x, e = lo + i;
    if (rew && threadIdx.x == 0) rew[i] = rew_h[e], done[i] = done_h[e], drv[i] = drv_h[e], term[i] = term_h[e];
    for (int j = threadIdx.x; j < ns; j += blockDim.x) sc[(size_t)i * ns + j] = loop_f32_sc(sc_h[(size_t)e * ns + j]);
    if (obs)
    {
        /* 16 bytes a thread where the rows allow it (oe a multiple of 16: every cast size the nets take) */
        const uint4 *s = (const uint4 *)(obs_h + (size_t)e * oe);
        uint4 *d = (uint4 *)(obs + (size_t)i * oe);
        for (size_t j = threadIdx.x; j < oe / 16; j += blockDim.x) d[j] = s[j];
    }
}

/* lane/rawpix, --obs-tone (loop.h neth_tone): each sample's bytes through its tone, in place. A block a sample: mode 1
 * (auto) the sample's largest byte m first (a block max), then x * 255 / m rounded down (integers: exact; m 0 leaves
 * the frame black); mode 2 (gamma) the table lut[x] */
__global__ void neth_tone_k(uint8_t *obs, size_t oe, int mode, const uint8_t *lut)
{
    __shared__ int red[128];
    uint8_t *p = obs + (size_t)blockIdx.x * oe;
    int scale = 0;
    if (mode == 1)
    {
        int m = 0;
        for (size_t j = threadIdx.x; j < oe; j += blockDim.x) m = max(m, (int)p[j]);
        red[threadIdx.x] = m;
        __syncthreads();
        for (int w = blockDim.x / 2; w > 0; w >>= 1)
        {
            if ((int)threadIdx.x < w) red[threadIdx.x] = max(red[threadIdx.x], red[threadIdx.x + w]);
            __syncthreads();
        }
        scale = red[0];
        if (scale == 0) return;
    }
    for (size_t j = threadIdx.x; j < oe; j += blockDim.x)
        p[j] = mode == 1 ? (uint8_t)((int)p[j] * 255 / scale) : lut[p[j]];
}

extern "C" void neth_tone(uint8_t *obs, int n, size_t oe, int mode, const uint8_t *lut, cudaStream_t s)
{
    neth_tone_k<<<n, 128, 0, s>>>(obs, oe, mode, lut);
}

extern "C" void neth_stage(const float *rew_h, const uint8_t *done_h, const uint8_t *drv_h, const uint8_t *term_h,
                         const float *sc_h, const uint8_t *obs_h, int lo, int n, size_t oe, float *rew, uint8_t *done,
                         uint8_t *drv, uint8_t *term, neth_sc_t *sc, uint8_t *obs, cudaStream_t s)
{
    neth_stage_k<<<n, 128, 0, s>>>(rew_h, done_h, drv_h, term_h, sc_h, obs_h, lo, oe, rew, done, drv, term, sc, obs,
                                   NETH_NET_NS);
}

/* ---- lane/cuda-graph: a group's completion as a mapped flag, and the captured group step's two kernels ---- */

/* the flag last: every thread's writes to the host (the actions) fenced to the system, the block's barrier, then thread
 * 0 publishes seq (the host reads the flag with acquire, then the actions) */
__device__ __forceinline__ void loop_publish(uint32_t *flag, uint32_t seq)
{
    __threadfence_system();
    __syncthreads();
    if (threadIdx.x == 0)
    {
        __threadfence_system();
        *(volatile uint32_t *)flag = seq;
    }
}

/* --poll-flag (lane/cuda-graph): the group's actions to the env's mapped buffer, then the flag (one block) */
__global__ void neth_commit_k(const int32_t *__restrict__ act, int na, int32_t *act_h, uint32_t *flag, uint32_t seq)
{
    for (int k = threadIdx.x; k < na; k += blockDim.x) act_h[k] = act[k];
    loop_publish(flag, seq);
}

extern "C" void neth_commit(const int32_t *act, int na, int32_t *act_h, uint32_t *flag, uint32_t seq, cudaStream_t s)
{
    neth_commit_k<<<1, 256, 0, s>>>(act, na, act_h, flag, seq);
}

/* --graph's first kernel, a block an env (neth_stage_k's work, its places read from the slot): the last action's reward,
 * end and driver into the segment the last step filled (has_prev), the scalars into the fixed scratch the captured actor
 * reads and into the segment at step t, the cast's frame (fixed: the actor reads it there) copied into the segment */
#define GSTAGE_T 256
#define GSTAGE_CB 4     /* the frame copy's blocks an env */
/* lane/audit-feat: the scalars' loop strides GSTAGE_T, so any run's count (up to net.h NETH_NET_NS_MAX, 1001) fits; the
 * build-time bound it had (NETH_NET_NS <= 2 GSTAGE_T) is gone with the build's count */
static_assert(sizeof(struct neth_gslot) == 8 * sizeof(int32_t), "the slot is eight words");

/* Device memory only: the graph's first nodes DMA the group's staging block (stg: the slot, the rewards, ends,
 * drivers and terminations) and its scalars (scf, f32) to the device. A host-mapped read by a kernel costs a bus round trip that the
 * device barely overlaps (measured on the RTX PRO 6000: neth_stage_k's ~300 small reads 13.5 us; 128 blocks reading
 * the 32-byte slot mapped 57 us), so nothing here reads the host. Blocks [0, gs) an env's scalars and outcome, the
 * rest GSTAGE_CB an env for the frame's copy */
__global__ void __launch_bounds__(GSTAGE_T) neth_gstage_k(const uint8_t *__restrict__ stg, const float *__restrict__ scf,
                              struct neth_gseg sg, const uint8_t *__restrict__ frames, int gs, size_t oe, neth_sc_t *gsc,
                              int ns)
{
    const struct neth_gslot sl = *(const struct neth_gslot *)stg;
    const int i = blockIdx.x, x = threadIdx.x, h = sl.half;
    if (i >= gs)
    {
        const int c = i - gs, env = c / GSTAGE_CB, part = c % GSTAGE_CB;
        const size_t n16 = oe / 16, per = (n16 + GSTAGE_CB - 1) / GSTAGE_CB, j0 = part * per;
        const size_t j1 = j0 + per < n16 ? j0 + per : n16;
        const uint4 *s = (const uint4 *)(frames + (size_t)env * oe);
        uint4 *d = (uint4 *)(sg.obs[h] + ((size_t)sl.t * gs + env) * oe);
        for (size_t j = j0 + x; j < j1; j += GSTAGE_T) d[j] = s[j];
        return;
    }
    const size_t at = (size_t)sl.t * gs + i;
    if (sl.has_prev && x == 0)
    {
        const float *rew = (const float *)(stg + sizeof sl);
        const uint8_t *done = stg + sizeof sl + (size_t)gs * sizeof(float), *drv = done + gs, *term = drv + gs;
        const size_t pa = (size_t)sl.pt * gs + i;
        sg.rew[sl.phalf][pa] = rew[i], sg.done[sl.phalf][pa] = done[i], sg.drv[sl.phalf][pa] = drv[i];
        sg.term[sl.phalf][pa] = term[i];    /* lane/audit-fixes */
    }
    for (int j = x; j < ns; j += GSTAGE_T)
    {
        neth_sc_t c = loop_f32_sc(scf[(size_t)i * ns + j]);
        gsc[(size_t)i * ns + j] = c;
        sg.sc[h][at * ns + j] = c;
    }
}

/* --graph's last kernel (one block): the policy's outputs from the fixed scratch into the segment at step t, the
 * finished segment's bootstrap (neth_boot_k's expression) where the slot says, the actions to the env's mapped buffer,
 * then the flag */
__global__ void neth_gcommit_k(const uint8_t *stg, struct neth_gseg sg, int gs, int T, const int32_t *__restrict__ gact,
                               const float *__restrict__ glogp, const float *__restrict__ gval, int32_t *act_h, uint32_t *flag)
{
    const struct neth_gslot sl = *(const struct neth_gslot *)stg;    /* the staging block's device copy */
    const int h = sl.half, na = gs * NETH_NET_ACTION;
    int32_t *act = sg.act[h] + (size_t)sl.t * na;
    for (int k = threadIdx.x; k < na; k += blockDim.x)
    {
        int32_t a = gact[k];
        act[k] = a;
        act_h[k] = a;
    }
    for (int i = threadIdx.x; i < gs; i += blockDim.x)
    {
        sg.logp[h][(size_t)sl.t * gs + i] = glogp[i];
        sg.val[h][(size_t)sl.t * gs + i] = gval[i];
        if (sl.boot)
        {
            /* lane/audit-fixes: a last row the clock ended bootstraps from its own value (trainer.cu neth_boot_k) */
            const size_t la = (size_t)(T - 1) * gs + i;
            const uint8_t d = sg.done[sl.phalf][la];
            sg.boot[sl.phalf][i] = d && !sg.term[sl.phalf][la] ? sg.val[sl.phalf][la] : gval[i] * (1.0f - (float)d);
        }
    }
    loop_publish(flag, sl.seq);
}

/* --graph over a recurrent net: neth_rnn_h0_k's work where the slot says (at its step 0, the segment's start state into
 * the half the step fills), first the staging block's done bytes (the last action's outcome; has_prev 0: none, as the
 * launch without the graph passes NULL) */
__global__ void neth_grnn_h0_k(const uint8_t *__restrict__ stg, const float *__restrict__ h, int gs, float *h0a, float *h0b)
{
    const struct neth_gslot sl = *(const struct neth_gslot *)stg;
    if (sl.t != 0) return;
    const uint8_t *first = stg + sizeof sl + (size_t)gs * sizeof(float);
    float *h0 = sl.half ? h0b : h0a;
    const long n = (long)gs * NETH_NET_D;
    for (long i = (long)blockIdx.x * blockDim.x + threadIdx.x; i < n; i += (long)gridDim.x * blockDim.x)
        h0[i] = sl.has_prev && first[i / NETH_NET_D] ? 0.0f : h[i];
}

extern "C" void neth_grnn_h0(const uint8_t *stg, const float *h, int gs, float *h0a, float *h0b, cudaStream_t s)
{
    long m = (long)gs * NETH_NET_D;
    neth_grnn_h0_k<<<(unsigned)((m + 255) / 256), 256, 0, s>>>(stg, h, gs, h0a, h0b);
}

extern "C" size_t neth_gstg_bytes(int gs)
{
    return (sizeof(struct neth_gslot) + (size_t)gs * (sizeof(float) + 3) + 15) & ~(size_t)15;
}

extern "C" void neth_gstage(const uint8_t *stg, const float *scf, const struct neth_gseg *sg, const uint8_t *frames, int gs,
                            size_t oe, neth_sc_t *gsc, cudaStream_t s)
{
    neth_gstage_k<<<gs * (1 + GSTAGE_CB), GSTAGE_T, 0, s>>>(stg, scf, *sg, frames, gs, oe, gsc, NETH_NET_NS);
}

extern "C" void neth_gcommit(const uint8_t *stg, const struct neth_gseg *sg, int gs, int T, const int32_t *gact,
                             const float *glogp, const float *gval, int32_t *act_h, uint32_t *flag, cudaStream_t s)
{
    neth_gcommit_k<<<1, 256, 0, s>>>(stg, *sg, gs, T, gact, glogp, gval, act_h, flag);
}
