/* The native trainer's loop (SPEC.md "The environment as configuration" item 6a, lane/ntloop): the policy as the loop
 * sees it. A policy is a launch on a stream over one group's device buffers; the loop copies the group's observation
 * and scalars to the device before it (or has the vector env cast on that stream), copies the actions back after it,
 * and sends the group when the stream's event fires.
 *
 * Two policies live in loop.cu, the third in trainer.cu (lane/ntwire):
 *   standin  vec_bench's stand-in (its xorshift a env, the same draws: the camera's bins near the centre, forward and
 *            attack often), on the device, so the loop's trajectory digest equals vec_bench --digest's
 *   noop     no kernel: the actions stay the idle action (the centre bins, no key), the loop's own cost
 *   actor    lane ntactor's forward and gumbel sampler (actor.h), a workspace a group's stream, logp and value into
 *            the segment; its weights refreshed on the device from the learner's (two images: the launches read one
 *            while a refresh writes the other) */
#ifndef NETH_NET_LOOP_H
#define NETH_NET_LOOP_H

#include <stdint.h>

#include <cuda_runtime.h>
#include "net.h"

/* a group's device buffers for one step, valid on the launch's stream */
struct neth_io {
    int g, base, n;             /* the group, its first env id, its envs */
    const uint8_t *obs;         /* [n][16][16][3] the cast's RGB bytes (net.h's x0 before /255) */
    const neth_sc_t *scalars;   /* [n][NETH_NET_NS] */
    int32_t *actions;           /* [n][NETH_NET_ACTION] out: the two camera bins, the 14 keys 0/1 */
    float *logp, *value;        /* [n] out: what the learner stores with the step (the stand-in writes 0) */
    float *h;                   /* (net.h NETH_NET_RNN; lane/ntgru) [n][NETH_NET_D] the envs' recurrent state, stepped in place */
    const uint8_t *first;       /* [n] nonzero where an episode starts at this step (NULL: none) */
};

struct neth_policy {
    const char *name;
    void *ctx;
    /* 0, or -1 on a failed launch */
    int (*launch)(void *ctx, const struct neth_io *io, cudaStream_t s);
    void (*release)(void *ctx);
};

/* lane/cuda-graph (ntloop --graph 1): a group's two segments as its captured step's kernels index them ([half]), and
 * the launch's control the host writes (the staging block's head) before each replay: the segment half and step the step
 * fills, the half and step the last action's outcome goes to (has_prev: not the group's first step), boot (the
 * finished half's bootstrap, the learner's), seq (the group's flag once its actions are on the host) */
struct neth_gseg {
    uint8_t *obs[2];
    neth_sc_t *sc[2];
    int32_t *act[2];
    float *logp[2], *val[2], *rew[2], *boot[2];
    uint8_t *done[2], *drv[2];
    uint8_t *term[2];           /* lane/audit-fixes: the segment's terminations (neth_seg's term) */
};
struct neth_gslot {
    int32_t half, t, phalf, pt, has_prev, boot;
    uint32_t seq, pad;
};

#ifdef __cplusplus
extern "C" {
#endif
/* vec_bench's stand-in over n envs, its seed (vec_bench --seed): each env's xorshift seeded as vec_bench seeds it */
int neth_policy_standin(struct neth_policy *p, int n, uint64_t seed);
/* lane/ladder10: the same with key 12 (actions.gui cursor's inventory key) held at 20 in 1024 */
int neth_policy_standin_gui(struct neth_policy *p, int n, uint64_t seed);
int neth_policy_noop(struct neth_policy *p);

/* train/trainer.cu (lane/ntwire): lane ntactor's actor as the policy, net.h's blob its weights, a workspace and a
 * device RNG stream a group (groups of gs), bf16 or fp32; 0, or -1 with err */
int neth_policy_actor(struct neth_policy *p, const void *ntw, size_t len, int groups, int gs, int bf16, uint64_t seed,
                    char *err, size_t n);
/* lane/audit-fixes: the actor's draw on both images (actor.h neth_actor_set_draw: itemp 1, greedy 0 the policy's own),
 * set before the first launch; a captured graph keeps the draw of its capture */
void neth_policy_actor_draw(struct neth_policy *p, float itemp, int greedy);
/* the image launches do not use, written on s from the learner's weights w (learn.h's layout, device): the caller
 * orders s after every launch so far (their events), and calls switch once it has run on the device */
int neth_policy_actor_refresh(struct neth_policy *p, const float *w, cudaStream_t s);
void neth_policy_actor_switch(struct neth_policy *p);
/* the learner's weights the current image was refreshed from (device; the initial image's: never written) */
const float *neth_policy_actor_source(struct neth_policy *p);
/* the check: the sampler's uniforms for the whole batch (device, [n][2][41] and [n][14], io->base indexes them) */
void neth_policy_actor_uniforms(struct neth_policy *p, const float *ucam, const float *ukey, const float *uhot);
/* bytes of the current image differing from the one neth_actor_load_blob makes of ntw (-1 on error) */
long neth_policy_actor_diff(struct neth_policy *p, const void *ntw, size_t len);
/* net.h's blob of the layout's weights (host; malloc'd) */
unsigned char *neth_ntw_blob(const float *flat, size_t *len);
/* lane/cuda-graph: the actor's launch split for a captured step (trainer.cu): graphable (the actor without the check's
 * uniforms), prep (outside the capture: the group's workspace on the current image, its call counted; the image, -1
 * on error), step (the step on image img alone, capturable) */
int neth_policy_actor_graphable(struct neth_policy *p);
int neth_policy_actor_prep(struct neth_policy *p, int g, cudaStream_t s);
int neth_policy_actor_step(struct neth_policy *p, int img, const struct neth_io *io, cudaStream_t s);
/* --poll-flag: na actions to act_h (the env's mapped buffer), then *flag = seq (mapped), one launch */
void neth_commit(const int32_t *act, int na, int32_t *act_h, uint32_t *flag, uint32_t seq, cudaStream_t s);
/* --graph's kernels around the captured actor and critic (struct neth_gseg's comment). A group's staging block (stg,
 * device; the graph's first node copies the host's page-locked one): the slot, then float rew[gs], uint8 done[gs],
 * uint8 drv[gs], uint8 term[gs] (lane/audit-fixes), padded to 16 bytes (neth_gstg_bytes); scf the group's scalars (f32, device). Neither kernel reads the
 * host: a kernel's mapped reads cost a bus trip each */
size_t neth_gstg_bytes(int gs);
void neth_gstage(const uint8_t *stg, const float *scf, const struct neth_gseg *sg, const uint8_t *frames, int gs, size_t oe,
                 neth_sc_t *gsc, cudaStream_t s);
void neth_gcommit(const uint8_t *stg, const struct neth_gseg *sg, int gs, int T, const int32_t *gact,
                  const float *glogp, const float *gval, int32_t *act_h, uint32_t *flag, cudaStream_t s);
/* --graph over a recurrent net (NETH_NET_RNN): at the slot's step 0, the group's state h [gs][NETH_NET_D] into the
 * segment's start state of the slot's half (h0a half 0, h0b half 1), zero where the staging block's done says */
void neth_grnn_h0(const uint8_t *stg, const float *h, int gs, float *h0a, float *h0b, cudaStream_t s);

/* the loop's kernels around the learner, on s: the actions [n][16] into its camera bins [n][2] and keys [n][14];
 * the bootstrap boot = val0 * (1 - done_last), or val_last where done_last and not term_last (lane/audit-fixes: the
 * segment's last row timed out); max |a - b| over n into *out (device) */
void neth_split_actions(const int32_t *act, int n, int32_t *cam, float *keys, int32_t *hot, cudaStream_t s);
void neth_boot(const float *val0, const uint8_t *done_last, const uint8_t *term_last, const float *val_last, int n,
               float *boot, cudaStream_t s);
/* lane/ntcast, ntloop --stage 1: envs lo .. lo + n - 1's reward, end, driver and termination (device pointers of the
 * env's mapped page-locked buffers, indexed by env) into rew, done, drv and term [n] (all NULL: none), their scalars
 * into sc [n][NETH_NET_NS] and, obs_h not NULL, their observations (oe bytes each, a multiple of 16) into obs, in one
 * launch on s */
void neth_stage(const float *rew_h, const uint8_t *done_h, const uint8_t *drv_h, const uint8_t *term_h, const float *sc_h,
              const uint8_t *obs_h, int lo, int n, size_t oe, float *rew, uint8_t *done, uint8_t *drv, uint8_t *term,
              neth_sc_t *sc, uint8_t *obs, cudaStream_t s);
void neth_maxabs(const float *a, const float *b, int n, float *out, cudaStream_t s);
/* lane/rawpix (ntloop --obs-tone): n observations of oe bytes each through a tone, in place on s: mode 1 auto (each
 * frame's bytes times 255 over its largest, the camera's auto exposure; integer, exact), mode 2 the 256-byte table lut
 * (device; --obs-tone gamma:G's 255 (x / 255)^(1/G), rounded) */
void neth_tone(uint8_t *obs, int n, size_t oe, int mode, const uint8_t *lut, cudaStream_t s);
/* lane/ntgru (net.h NETH_NET_RNN): n envs' states h [n][NETH_NET_D] into a segment's start states h0, zero where first[i] (NULL:
 * none) says the step starts an episode */
void neth_rnn_h0(const float *h, const uint8_t *first, int n, float *h0, cudaStream_t s);
#ifdef __cplusplus
}
#endif

#endif
