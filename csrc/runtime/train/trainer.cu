/* The native trainer's device side (lane/ntwire, SPEC.md "The environment as configuration" item 6, a's loop with b's
 * learner): lane ntactor's actor as the loop's policy (loop.h), the actor's weights refreshed on the device from the
 * learner's (learn.h's layout), and the loop's small kernels around the learner (the actions split into the learner's
 * camera bins and keys, a segment's bootstrap, the actor's gap to the learner). Built once a net.h variant (loop.mk).
 *
 * Two actor images, so a refresh never writes weights a launched step reads: the policy's launches use the current
 * one, a refresh writes the other with neth_actor_reload (lane/ntwidth: one gather kernel on the learner's stream, no
 * allocation) once every group's launches so far are done (the caller's events), and the caller makes it current
 * (neth_policy_actor_switch) once the refresh has run on the device. A workspace is bound to its image, so each group
 * has one an image; the group's RNG counter (one a call that draws, actor.h) carries over when its launches move to the
 * other image (neth_actor_ws_seed), so a group's draws are one sequence whatever the refreshes (lane/ntcast: this
 * replaced lane/ntwire's private compile of actor.cu and its probe of the image's layout). */
#include "actor.h"
#include "learn.h"
#include "loop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct actor_pol {
    neth_actor *img[2];
    int cur;                /* the image launches use */
    neth_actor_ws **ws[2];    /* [image][groups]: one a group's stream (its scratch and its RNG's counter) */
    int *wimg;              /* [groups]: the image the group's last launch used */
    uint64_t *seed, *draws; /* [groups]: the group's RNG seed and the calls that drew (the device counter) */
    int groups, gs;
    float *src[2];          /* device: the learner's weights each image was refreshed from (actor_gap) */
    const float *ucam, *ukey;   /* the check's uniforms over the whole batch (io->base indexes them), else NULL */
    const float *uhot;          /* and the hotbar's (NETH_NET_HOTBAR; NULL: its draws the device RNG's) */
};

static int actor_launch(void *ctx, const struct neth_io *io, cudaStream_t s)
{
    actor_pol *c = (actor_pol *)ctx;
    if (io->g < 0 || io->g >= c->groups) return -1;
    int g = io->g;
    neth_actor_ws *w = c->ws[c->cur][g];
    if (c->wimg[g] != c->cur) {
        if (neth_actor_ws_seed(w, c->seed[g], c->draws[g], s)) return -1;
        c->wimg[g] = c->cur;
    }
    /* obs, sc, act, logp, value, cam, keys, ucam, ukey, hot, uhot (lane/nthotbar), h, first (lane/ntgru) */
    neth_actor_io a = {io->obs, io->scalars, io->actions, io->logp, io->value, NULL, NULL, NULL, NULL, NULL, NULL, io->h,
                     io->first};
    if (c->ucam) {
        a.ucam = c->ucam + (size_t)io->base * NETH_NET_CAM * NETH_NET_BINS;
        a.ukey = c->ukey + (size_t)io->base * NETH_NET_KEYS;
        if (c->uhot) a.uhot = c->uhot + (size_t)io->base * NETH_NET_HOT;
    }
    else
        ++c->draws[g];
    return neth_actor_step(w, io->n, &a, s) ? -1 : 0;
}

/* lane/cuda-graph: actor_launch split in two for a captured step. prep (outside any capture, on the group's stream):
 * the group's workspace moved to the current image as actor_launch does, the group's call counted; the image the
 * step must read returned (-1 on error). step: the step on that image alone, nothing on the host changed (the capture
 * records it once, every replay is one launch's work). A policy with the check's uniforms is not capturable */
extern "C" int neth_policy_actor_graphable(struct neth_policy *p)
{
    actor_pol *c = (actor_pol *)p->ctx;
    return p->launch == actor_launch && c && !c->ucam;
}

extern "C" int neth_policy_actor_prep(struct neth_policy *p, int g, cudaStream_t s)
{
    actor_pol *c = (actor_pol *)p->ctx;
    if (g < 0 || g >= c->groups) return -1;
    if (c->wimg[g] != c->cur) {
        if (neth_actor_ws_seed(c->ws[c->cur][g], c->seed[g], c->draws[g], s)) return -1;
        c->wimg[g] = c->cur;
    }
    ++c->draws[g];
    return c->cur;
}

extern "C" int neth_policy_actor_step(struct neth_policy *p, int img, const struct neth_io *io, cudaStream_t s)
{
    actor_pol *c = (actor_pol *)p->ctx;
    if (io->g < 0 || io->g >= c->groups || (img & ~1)) return -1;
    neth_actor_io a = {io->obs, io->scalars, io->actions, io->logp, io->value, NULL, NULL, NULL, NULL, NULL, NULL, io->h,
                     io->first};
    return neth_actor_step(c->ws[img][io->g], io->n, &a, s) ? -1 : 0;
}

static void actor_release(void *ctx)
{
    actor_pol *c = (actor_pol *)ctx;
    if (!c) return;
    for (int i = 0; i < 2; i++) {
        for (int g = 0; c->ws[i] && g < c->groups; g++) neth_actor_ws_free(c->ws[i][g]);
        free(c->ws[i]);
        neth_actor_free(c->img[i]);
        cudaFree(c->src[i]);
    }
    free(c->wimg), free(c->seed), free(c->draws);
    free(c);
}

/* lane/audit-fixes: neth_checkpoint.c's one writer (NTW2, the build's header) */
extern "C" unsigned char *neth_ntw_write(const float *const t[NETH_NET_TENSORS], size_t *len);

/* net.h's blob of the layout's weights (host) */
static unsigned char *blob_of(const float *flat, size_t *len)
{
    static const int rank[NETH_NET_TENSORS] = {
#define NETH_NET_R(n, r, a, b, c, d) r,
        NETH_NET_ORDER(NETH_NET_R)
#undef NETH_NET_R
    };
    const int dims[NETH_NET_TENSORS][4] = {   /* lane/audit-feat: fc1's columns the run's (net.h NETH_NET_FLAT) */
#define NETH_NET_D4(n, r, a, b, c, d) {a, b, c, d},
        NETH_NET_ORDER(NETH_NET_D4)
#undef NETH_NET_D4
    };
    float *t[NETH_NET_TENSORS];
    for (int i = 0; i < NETH_NET_TENSORS; i++) {
        size_t cnt = 1;
        for (int k = 0; k < rank[i]; k++) cnt *= dims[i][k];
        t[i] = (float *)malloc(4 * cnt);
    }
    neth_learn_to_ntw(flat, t);
    unsigned char *b = neth_ntw_write(t, len);
    for (int i = 0; i < NETH_NET_TENSORS; i++) free(t[i]);
    return b;
}

extern "C" int neth_policy_actor(struct neth_policy *p, const void *ntw, size_t len, int groups, int gs, int bf16,
                               uint64_t seed, char *err, size_t n)
{
    actor_pol *c = (actor_pol *)calloc(1, sizeof *c);
    c->groups = groups;
    c->gs = gs;
    c->wimg = (int *)calloc((size_t)groups, sizeof *c->wimg);
    c->seed = (uint64_t *)calloc((size_t)groups, sizeof *c->seed);
    c->draws = (uint64_t *)calloc((size_t)groups, sizeof *c->draws);
    const char *e = NULL;
    for (int i = 0; i < 2; i++) {
        c->ws[i] = (neth_actor_ws **)calloc((size_t)groups, sizeof *c->ws[i]);
        if (!(c->img[i] = neth_actor_load_blob(ntw, len, &e))) {
            snprintf(err, n, "the actor's weights: %s", e);
            return actor_release(c), -1;
        }
        neth_actor_set_mode(c->img[i], bf16 ? NETH_NET_ACTOR_BF16 : NETH_NET_ACTOR_FP32);
        /* each group's stream its own RNG stream: the seed's counter space split by the group */
        for (int g = 0; g < groups; g++) {
            c->seed[g] = seed * 0x9e3779b97f4a7c15ull + (uint64_t)g + 1;
            if (!(c->ws[i][g] = neth_actor_ws_new(c->img[i], gs, c->seed[g]))) {
                snprintf(err, n, "the actor's workspace %d", g);
                return actor_release(c), -1;
            }
        }
        if (cudaMalloc(&c->src[i], NETH_LEARN_NPARAM * 4) != cudaSuccess) {
            snprintf(err, n, "the actor's refresh source on the device");
            return actor_release(c), -1;
        }
    }
    p->name = "actor";
    p->ctx = c;
    p->launch = actor_launch;
    p->release = actor_release;
    return 0;
}

/* lane/audit-fixes: both images' draw (actor.h neth_actor_set_draw), before the first launch */
extern "C" void neth_policy_actor_draw(struct neth_policy *p, float itemp, int greedy)
{
    actor_pol *c = (actor_pol *)p->ctx;
    for (int i = 0; i < 2; i++) neth_actor_set_draw(c->img[i], itemp, greedy);
}

extern "C" int neth_policy_actor_refresh(struct neth_policy *p, const float *w, cudaStream_t s)
{
    actor_pol *c = (actor_pol *)p->ctx;
    int o = c->cur ^ 1;
    if (neth_actor_reload(c->img[o], w, s)) return -1;
    return cudaMemcpyAsync(c->src[o], w, NETH_LEARN_NPARAM * 4, cudaMemcpyDeviceToDevice, s) == cudaSuccess ? 0 : -1;
}

extern "C" void neth_policy_actor_switch(struct neth_policy *p)
{
    actor_pol *c = (actor_pol *)p->ctx;
    c->cur ^= 1;
}

extern "C" const float *neth_policy_actor_source(struct neth_policy *p)
{
    actor_pol *c = (actor_pol *)p->ctx;
    return c->src[c->cur];
}

extern "C" void neth_policy_actor_uniforms(struct neth_policy *p, const float *ucam, const float *ukey, const float *uhot)
{
    actor_pol *c = (actor_pol *)p->ctx;
    c->ucam = ucam;
    c->ukey = ukey;
    c->uhot = uhot;
}

/* bytes of the current image (mem, mem16, memf) that differ from the image neth_actor_load_blob makes of ntw; -1 on
 * error */
extern "C" long neth_policy_actor_diff(struct neth_policy *p, const void *ntw, size_t len)
{
    actor_pol *c = (actor_pol *)p->ctx;
    const char *e;
    neth_actor *b = neth_actor_load_blob(ntw, len, &e);
    if (!b) return -1;
    long d = neth_actor_diff(c->img[c->cur], b);
    neth_actor_free(b);
    return d;
}

extern "C" unsigned char *neth_ntw_blob(const float *flat, size_t *len) { return blob_of(flat, len); }

/* ---- the loop's kernels around the learner ---- */

/* the actions [n][NETH_NET_ACTION] into the learner's camera bins [n][2] and keys [n][14] (0 or 1), and (NETH_NET_HOTBAR) the
 * hotbar's draw [n] */
__global__ void neth_split_k(const int32_t *__restrict__ act, int n, int32_t *cam, float *keys, int32_t *hot)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n * NETH_NET_ACTION) return;
    int s = i / NETH_NET_ACTION, j = i % NETH_NET_ACTION;
    if (j < NETH_NET_CAM) cam[s * NETH_NET_CAM + j] = act[i];
    else if (j < NETH_NET_CAM + NETH_NET_KEYS) keys[s * NETH_NET_KEYS + j - NETH_NET_CAM] = (float)act[i];
    else hot[s] = act[i];
}

extern "C" void neth_split_actions(const int32_t *act, int n, int32_t *cam, float *keys, int32_t *hot, cudaStream_t s)
{
    neth_split_k<<<(n * NETH_NET_ACTION + 255) / 256, 256, 0, s>>>(act, n, cam, keys, hot);
}

/* fastppo's bootstrap: the next segment's first value, zero where the segment's last action ended the episode.
 * lane/audit-fixes: where the clock ended it (done, not term) the segment's own last value instead (k_gae's
 * approximation, learn.cu); term = done is the bootstrap as it was, bit for bit */
__global__ void neth_boot_k(const float *__restrict__ val0, const uint8_t *__restrict__ done,
                            const uint8_t *__restrict__ term, const float *__restrict__ vlast, int n, float *boot)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) boot[i] = done[i] && !term[i] ? vlast[i] : val0[i] * (1.0f - (float)done[i]);
}

extern "C" void neth_boot(const float *val0, const uint8_t *done_last, const uint8_t *term_last, const float *val_last,
                          int n, float *boot, cudaStream_t s)
{
    neth_boot_k<<<(n + 127) / 128, 128, 0, s>>>(val0, done_last, term_last, val_last, n, boot);
}

/* lane/ntgru: the segment's start states, h0 = h zeroed where first (NULL: none) */
__global__ void neth_rnn_h0_k(const float *__restrict__ h, const uint8_t *__restrict__ first, long n, float *h0)
{
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) h0[i] = first && first[i / NETH_NET_D] ? 0.0f : h[i];
}

extern "C" void neth_rnn_h0(const float *h, const uint8_t *first, int n, float *h0, cudaStream_t s)
{
    long m = (long)n * NETH_NET_D;
    neth_rnn_h0_k<<<(unsigned)((m + 255) / 256), 256, 0, s>>>(h, first, m, h0);
}

/* max |a - b| over n into *out (one block) */
__global__ void neth_maxabs_k(const float *__restrict__ a, const float *__restrict__ b, int n, float *out)
{
    __shared__ float r[1024];
    float m = 0;
    for (int i = threadIdx.x; i < n; i += blockDim.x) m = fmaxf(m, fabsf(a[i] - b[i]));
    r[threadIdx.x] = m;
    __syncthreads();
    for (int k = blockDim.x / 2; k > 0; k >>= 1) {
        if ((int)threadIdx.x < k) r[threadIdx.x] = fmaxf(r[threadIdx.x], r[threadIdx.x + k]);
        __syncthreads();
    }
    if (threadIdx.x == 0) *out = r[0];
}

extern "C" void neth_maxabs(const float *a, const float *b, int n, float *out, cudaStream_t s)
{
    neth_maxabs_k<<<1, 1024, 0, s>>>(a, b, n, out);
}
