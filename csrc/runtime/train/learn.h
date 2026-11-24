/* The native trainer's learner (SPEC.md "The environment as configuration", item 6b; lane/ntlearn): GAE over a
 * segment, one minibatch's forward, PPO loss and backward into a gradient buffer, and the clipped Adam step, as CUDA
 * kernels over device buffers. mcsr-speedrun's fastppo.py Learner is the reference (its _gae, _part and _step, at
 * the brute recipe: T 64, a group of 128, one minibatch of 8192, 3 epochs); learn_check holds these kernels to the
 * fixture scripts/ntref.py's `learner` writes (~/.cache/netherite-native/learn-a05-070), fp32 within a stated
 * tolerance.
 *
 * A segment is fastppo's Seg, T-major (sample i = t * gs + env), on the device:
 *   obs  u8  [N][S][S][3]     the cast's RGB bytes        sc    f32 [N][155]   the scalars
 *   cam  i32 [N][2]           the camera bins drawn       keys  f32 [N][14]    the keys drawn, 0 or 1
 *   logp f32 [N]              the actor's log-probability val   f32 [N]        the actor's value
 *   rew  f32 [N]  done u8 [N]  drv u8 [N] (nonzero: a macro drove the step, masked)   boot f32 [gs]
 *   term u8 [N]   lane/audit-fixes: where done, 1 the task ended the episode (bootstrap 0), 0 the clock did (bootstrap
 *                 from the row's own value; k_gae's comment); term = done is the learner without the distinction
 *
 * The parameters are one fp32 buffer of NETH_LEARN_NPARAM in this file's layout (NETH_LEARN_LAYOUT: net.h's tensors with the three
 * heads' weights side by side, then their biases, so the heads are one [97][D] matrix); gradients and Adam's two
 * moments have the same layout. neth_learn_from_ntw and neth_learn_to_ntw move net.h's blob order to and from it.
 *
 * net.h's variant (NETH_NET_WM, NETH_NET_STEM; lane/ntwidth) is the build's: S the cast's side (16, or 64 with the stem), D the
 * MLP's width (256 wm). Each variant's fixture is ntref.py learner's for that net (learn-fresh-V). */
#ifndef NETH_NET_LEARN_H
#define NETH_NET_LEARN_H

#include <stddef.h>
#include <stdint.h>
#include "net.h"

#define NETH_LEARN_HOFF (NETH_NET_CAM * NETH_NET_BINS + NETH_NET_KEYS)           /* NETH_NET_HOTBAR: the hotbar's 10 logits, after the keys' */
#define NETH_LEARN_HEADS (NETH_LEARN_HOFF + NETH_NET_HOTBAR * NETH_NET_HOT + 1)   /* 97: the camera's 82 logits, the keys' 14, the value (107
                                                         * with the hotbar's 10 before it) */
#define NETH_LEARN_NBOOK 7                                     /* loss pg vl ent kl clipfrac ref_kl: fastppo's acc */

/* the layout: X(name, net.h's index, elements) in buffer order (the stem's two tensors first when the net has one) */
#define NETH_LEARN_I0 (2 * NETH_NET_STEM)                            /* net.h's index of conv1_w */
#if NETH_NET_STEM
#define NETH_LEARN_LAYOUT_STEM(X)                              \
    X(stem_w, 0, NETH_NET_C0 * NETH_NET_C * 16)                     \
    X(stem_b, 1, NETH_NET_C0)
#else
#define NETH_LEARN_LAYOUT_STEM(X)
#endif
/* NETH_NET_HOTBAR: the hotbar's head between the keys' and the value's (the blob has it last, net.h's index I0 + 16, 17) */
#if NETH_NET_HOTBAR
#define NETH_LEARN_LAYOUT_HOTW(X) X(hot_w, NETH_LEARN_I0 + 16, NETH_NET_HOT * NETH_NET_D)
#define NETH_LEARN_LAYOUT_HOTB(X) X(hot_b, NETH_LEARN_I0 + 17, NETH_NET_HOT)
#else
#define NETH_LEARN_LAYOUT_HOTW(X)
#define NETH_LEARN_LAYOUT_HOTB(X)
#endif
#if NETH_NET_RNN     /* lane/ntgru: the minGRU's, last in both orders (lane/ladder8: after the hotbar's with both) */
#define NETH_LEARN_LAYOUT_RNN(X)                               \
    X(rnn_w, NETH_LEARN_I0 + 16 + 2 * NETH_NET_HOTBAR, 2 * NETH_NET_D * NETH_NET_D) \
    X(rnn_b, NETH_LEARN_I0 + 17 + 2 * NETH_NET_HOTBAR, 2 * NETH_NET_D)
#else
#define NETH_LEARN_LAYOUT_RNN(X)
#endif
/* lane/audit-feat: fc1's columns are the run's (net.h NETH_NET_NS): the enums below are the layout at no scalar
 * (NETH_LEARN_LNS 0 while they are made), NETH_LEARN_OFF and NETH_LEARN_NPARAM add fc1's D columns a scalar to every
 * offset after it; NETH_LEARN_N_name is a tensor's count, fc1_w's at no scalar (NETH_LEARN_NFC1 the run's) */
#define NETH_LEARN_LNS NETH_NET_NS
#define NETH_LEARN_LAYOUT(X)                                   \
    NETH_LEARN_LAYOUT_STEM(X)                                  \
    X(conv1_w, NETH_LEARN_I0 + 0, NETH_NET_C1 * NETH_NET_CI * 16)          \
    X(conv1_b, NETH_LEARN_I0 + 1, NETH_NET_C1)                       \
    X(conv2_w, NETH_LEARN_I0 + 2, NETH_NET_C2 * NETH_NET_C1 * 16)          \
    X(conv2_b, NETH_LEARN_I0 + 3, NETH_NET_C2)                       \
    X(conv3_w, NETH_LEARN_I0 + 4, NETH_NET_C3 * NETH_NET_C2 * 9)           \
    X(conv3_b, NETH_LEARN_I0 + 5, NETH_NET_C3)                       \
    X(fc1_w, NETH_LEARN_I0 + 6, NETH_NET_D * (NETH_NET_FLAT0 + NETH_LEARN_LNS)) \
    X(fc1_b, NETH_LEARN_I0 + 7, NETH_NET_D)                          \
    X(fc2_w, NETH_LEARN_I0 + 8, NETH_NET_D * NETH_NET_D)                   \
    X(fc2_b, NETH_LEARN_I0 + 9, NETH_NET_D)                          \
    X(cam_w, NETH_LEARN_I0 + 10, NETH_NET_CAM * NETH_NET_BINS * NETH_NET_D)      \
    X(keys_w, NETH_LEARN_I0 + 12, NETH_NET_KEYS * NETH_NET_D)              \
    NETH_LEARN_LAYOUT_HOTW(X)                                  \
    X(v_w, NETH_LEARN_I0 + 14, NETH_NET_D)                           \
    X(cam_b, NETH_LEARN_I0 + 11, NETH_NET_CAM * NETH_NET_BINS)             \
    X(keys_b, NETH_LEARN_I0 + 13, NETH_NET_KEYS)                     \
    NETH_LEARN_LAYOUT_HOTB(X)                                  \
    X(v_b, NETH_LEARN_I0 + 15, 1)                              \
    NETH_LEARN_LAYOUT_RNN(X)

#undef NETH_LEARN_LNS
#define NETH_LEARN_LNS 0
enum {
#define NETH_LEARN_E(n, i, k) NETH_LEARN_N_##n = (k),
    NETH_LEARN_LAYOUT(NETH_LEARN_E)
#undef NETH_LEARN_E
};
enum {
#define NETH_LEARN_O(n, i, k) NETH_LEARN_O_##n, NETH_LEARN_O_##n##_end = NETH_LEARN_O_##n + (k) - 1,
    NETH_LEARN_O_start_ = -1,
    NETH_LEARN_LAYOUT(NETH_LEARN_O)
#undef NETH_LEARN_O
    NETH_LEARN_NPARAM0
};
#undef NETH_LEARN_LNS
#define NETH_LEARN_LNS NETH_NET_NS
/* NETH_LEARN_OFF(name): the tensor's element offset in the layout (the run's: past fc1_w, D a scalar more) */
#define NETH_LEARN_OFF(n) (NETH_LEARN_O_##n + (NETH_LEARN_O_##n > NETH_LEARN_O_fc1_w ? NETH_NET_D * NETH_NET_NS : 0))
#define NETH_LEARN_NPARAM (NETH_LEARN_NPARAM0 + NETH_NET_D * NETH_NET_NS)
#define NETH_LEARN_NFC1 (NETH_NET_D * NETH_NET_FLAT)    /* fc1_w's count, the run's */

typedef struct neth_learn_hyper {
    double gamma, lam;          /* GAE's: 0.998, 0.95 (doubles: fastppo's gamma * lam is Python's, rounded once) */
    float clip, vf, ent;        /* the loss's: 0.2, 0.5, 0.003 */
    float lr, beta1, beta2, eps, max_norm;  /* Adam's (torch's fused Adam) and the clip: 3e-4, 0.9, 0.999, 1e-5, 0.5 */
    /* lane/ntppo, both off (0) by default:
     * ref_kl: fastppo's --ref-kl, the policy's KL to the reference weights (neth_learn_set_ref: the start's actor, frozen) in
     *   the loss at this weight a masked policy step: loss += ref_kl * sum(KL(pi || ref) m) / nm (fastppo's
     *   policy_kl: the two camera heads' categoricals and the 14 keys' Bernoullis), the ref_kl book its mean;
     * vonly: the value warm-up's update: the policy's terms (pg, the entropy, ref_kl) give no gradient and neth_learn_update's
     *   Adam steps move the value head (v.weight, v.bias) alone, every other weight and moment untouched */
    float ref_kl;
    int vonly;
    /* lane/ntbc: a loss in PPO's place (NULL: PPO's; train/distill.cu's cross-entropy): given the forward's heads hd
     * [b][NETH_LEARN_HSTRIDE] of the b samples idx names, it writes their gradient over them (fp32) and, dhb not NULL (the
     * 16-bit path), the same times NETH_NET_LSCALE in the 16-bit type [b][NETH_LEARN_HSTRIDE], on stream (a cudaStream_t) */
    void (*loss)(void *ctx, int b, const int32_t *idx, float *hd, void *dhb, void *stream);
    void *lctx;
    /* lane/acrl, both off (0) by default:
     * kl_stop: an update's epochs stopped at the first minibatch whose approx KL (the kl book: mean logp0 - logp over
     *   its masked steps, before its step) passes it; that minibatch's Adam step and every later one skipped on the
     *   device (a flag k_adam reads: no host sync), their forward and backward still run (the books keep counting
     *   them); neth_learn_kstat counts the steps taken and the updates stopped;
     * vclip: the value term clipped as PPO2's: max((v - R)^2, (v0 + clamp(v - v0, -vclip, vclip) - R)^2), v0 the
     *   actor's value of the sample (neth_learn_update's val) */
    float kl_stop, vclip;
} neth_learn_hyper;

typedef struct neth_learn neth_learn;     /* a learner: a cuBLAS handle, its stream, its precision, scratch for minibatches up to mb */

#ifdef __cplusplus
extern "C" {
#endif

/* the brute recipe's settings (configs/tests/brute.yaml's ppo) */
neth_learn_hyper neth_learn_hyper_default(void);

/* the GEMMs' precision: NETH_LEARN_FP32 fp32 throughout (learn_check's gate); NETH_LEARN_TF32 the same buffers, the GEMMs on the
 * tensor cores in TF32; NETH_LEARN_BF16 the activations, their gradients and a copy of the weights in bf16, every GEMM on
 * the tensor cores accumulating in fp32 (fastppo's --amp bf16), the loss, the weights' gradients and Adam in fp32 */
enum { NETH_LEARN_FP32, NETH_LEARN_TF32, NETH_LEARN_BF16 };

/* a learner for segments of at most nmax samples and minibatches of at most mb, on stream (a cudaStream_t; NULL
 * the default), the scratch allocated once; NULL on failure (a message on stderr) */
neth_learn *neth_learn_new(int nmax, int mb, int prec, void *stream);
void neth_learn_free(neth_learn *L);

/* net.h's blob order (16 tensors, fp32, host) to and from the layout (host buffers of NETH_LEARN_NPARAM) */
void neth_learn_from_ntw(const float *const ntw[NETH_NET_TENSORS], float *flat);
void neth_learn_to_ntw(const float *flat, float *const ntw[NETH_NET_TENSORS]);

/* GAE over a segment of T steps of gs envs (device buffers): rew / scale, the discounted advantages and returns, the
 * mask (1 where the policy drove), and the advantages normalised over the masked steps as fastppo's _gae does */
void neth_learn_gae(neth_learn *L, const neth_learn_hyper *h, int T, int gs, float scale, const float *rew, const uint8_t *done,
             const uint8_t *term, const uint8_t *drv, const float *val, const float *boot, float *adv, float *ret,
             float *mask);

/* the forward over the b samples idx names (idx NULL: 0..b-1): heads [b][NETH_LEARN_HSTRIDE], the first 97 of a row the
 * camera's two heads' 41 logits, the keys' 14, the value, in the learner's scratch, its address returned (valid until
 * the next call; NETH_NET_HOTBAR: the hotbar's 10 logits before the value, NETH_LEARN_HEADS 107) */
#define NETH_LEARN_HSTRIDE (NETH_NET_HOTBAR ? 112 : 104)
const float *neth_learn_forward(neth_learn *L, const float *w, int b, const int32_t *idx, const uint8_t *obs, const neth_sc_t *sc);

/* one minibatch (b samples, idx into the segment): the forward, fastppo's loss (the clipped policy term and the
 * entropy over the masked steps, the value term over all) and its gradient into grad (assigned, not added); books
 * [NETH_LEARN_NBOOK] on the device (fastppo's _part's stack). hot [N] (NETH_NET_HOTBAR: the hotbar's draws, 0 .. 9; NULL
 * otherwise): its log-softmax term in logp after the keys' and its entropy in the entropy's */
void neth_learn_grad(neth_learn *L, const neth_learn_hyper *h, float entc, const float *w, int b, const int32_t *idx, const uint8_t *obs,
              const neth_sc_t *sc, const int32_t *cam, const float *keys, const int32_t *hot, const float *logp0,
              const float *adv, const float *ret, const float *mask, float *grad, float *books);

#if NETH_NET_RNN
/* the recurrent net's segment (net.h NETH_NET_RNN; lane/ntgru, train/rnn.cuh): the scan's length T, the segment's ends done
 * [T][gs] and each env's state at its start h0 [gs][NETH_NET_D] (fp32, device; zero where its first step starts an episode).
 * Every forward and neth_learn_grad after it reads its b samples as b / T whole env columns, T-major: row t * (b / T) + k is
 * column k's step t, idx[row] its sample t * gs + env (idx NULL: the row itself, the segment's own order). neth_learn_update
 * sets T and done itself and keeps h0: call this first with the segment's h0, and give it each epoch's order in
 * that form, mb a multiple of T */
void neth_learn_seq(neth_learn *L, int T, const uint8_t *done, const float *h0);
#endif

/* an epoch's order of the gs envs (env[k]) as neth_learn_update reads it for the recurrent net: minibatches of be envs, each
 * T-major (out[i0 + t * b + k] = t * gs + env[k0 + k], b the minibatch's envs); N = T * gs entries (host) */
static inline void neth_learn_cols(const int32_t *env, int T, int gs, int be, int32_t *out) {
    for (int k0 = 0; k0 < gs; k0 += be) {
        int b = gs - k0 < be ? gs - k0 : be;
        for (int t = 0; t < T; t++)
            for (int k = 0; k < b; k++) out[(long)k0 * T + (long)t * b + k] = t * gs + env[k0 + k];
    }
}

/* lane/ntppo: the reference weights (device, the layout, NETH_LEARN_NPARAM; NULL none) h->ref_kl measures the policy against:
 * neth_learn_update runs their forward over each segment once (in chunks of the learner's minibatch) and keeps the camera's
 * and keys' logits, [nmax][NETH_LEARN_RSTRIDE] floats allocated here; neth_learn_grad reads them by sample. 0 on success */
#define NETH_LEARN_RSTRIDE (NETH_NET_CAM * NETH_NET_BINS + NETH_NET_KEYS)    /* 96 */
int neth_learn_set_ref(neth_learn *L, const float *wref);
/* the reference's logits for the n samples of the segment from sample i0 (obs and sc the segment's), into its rows
 * i0 to i0 + n (neth_learn_update calls it; learn_check's) */
void neth_learn_ref_forward(neth_learn *L, int i0, int n, const uint8_t *obs, const neth_sc_t *sc);

/* lane/ntppo's value warm-up step: clip_grad_norm_ over the value head's gradient alone, then torch's fused Adam on
 * v.weight and v.bias (step: their own count, 1 the first); nothing else read or written */
void neth_learn_adam_value(neth_learn *L, const neth_learn_hyper *h, int step, float *w, float *grad, float *m, float *v, float *gnorm);

/* clip_grad_norm_(max_norm) and torch's fused Adam step `step` (1 the first) on w, its moments m and v; the norm
 * before the clip to *gnorm (device, may be NULL) */
void neth_learn_adam(neth_learn *L, const neth_learn_hyper *h, int step, float *w, float *grad, float *m, float *v, float *gnorm);

/* fastppo's Learner.train for a feed-forward net: GAE, then epochs x the minibatches of each epoch's order perm
 * [epochs][N] (device), each neth_learn_grad then neth_learn_adam; *step counts Adam's steps; books summed over the steps */
void neth_learn_update(neth_learn *L, const neth_learn_hyper *h, float entc, int T, int gs, float scale, int epochs, int mb,
                const int32_t *perm, const uint8_t *obs, const neth_sc_t *sc, const int32_t *cam, const float *keys,
                const int32_t *hot, const float *logp, const float *val, const float *rew, const uint8_t *done,
                const uint8_t *term, const uint8_t *drv, const float *boot, float *w, float *grad, float *m, float *v,
                int *step, float *books);
/* lane/xhigh-patch: the same over K segments of T x gs laid one after another ([K][T][gs], boot [K][gs], the learner
 * made with nmax at least K * T * gs; perm [epochs][K * T * gs]): each segment's GAE with its own scale[k] and
 * bootstrap, the advantages normalised over all K, then the epochs' minibatches over the whole batch (the feed-forward
 * net only). K 1 is neth_learn_update, kernel for kernel */
void neth_learn_update_k(neth_learn *L, const neth_learn_hyper *h, float entc, int T, int gs, int K, const float *scale,
                int epochs, int mb, const int32_t *perm, const uint8_t *obs, const neth_sc_t *sc, const int32_t *cam,
                const float *keys, const int32_t *hot, const float *logp, const float *val, const float *rew,
                const uint8_t *done, const uint8_t *term, const uint8_t *drv, const float *boot, float *w, float *grad,
                float *m, float *v, int *step, float *books);

/* ---- lane/acrl: the privileged critic (train/critic.cuh) ----
 * An MLP of hidden width H (two layers, relu) over a sample's whole scalar row as the env wrote it, before --zero-sc
 * (the privileged scalars the actor never reads), its value added to the trunk's value head: in the actor's sample
 * values (neth_critic_act after the policy's launch) and in the learner's value term (neth_learn_set_critic). Its own parameters and
 * Adam (the learner's lr, its own norm clip at max_norm), stepped with every learner step (also the value warm-up's) */
typedef struct neth_critic neth_critic;
neth_critic *neth_critic_new(int H, int nmax, uint64_t seed);
void neth_critic_free(neth_critic *C);
int neth_critic_params(const neth_critic *C);
void neth_critic_set_mode(neth_critic *C, int bf16);
/* the actor's side: value[n] += v_c(x[n]) for n < b, x [b][NETH_NET_NS] (device), the actor's image of the weights */
void neth_critic_act(neth_critic *C, int b, const neth_sc_t *x, float *value, void *stream);
/* the actor's other image from the learner's weights (on the learner's stream, after its update), then the switch
 * (when that update is done), as neth_policy_actor_refresh and _switch */
void neth_critic_refresh(neth_critic *C, void *stream);
void neth_critic_switch(neth_critic *C);
/* the image neth_critic_act reads now (0 or 1; lane/cuda-graph keys its captured steps on it) */
int neth_critic_cur(const neth_critic *C);
int neth_critic_save(const neth_critic *C, const char *path);
int neth_critic_load(neth_critic *C, const char *path);
/* the learner reads C with each segment's full scalars psc [N][NETH_NET_NS] (device; set before neth_learn_update; NULL C: off) */
void neth_learn_set_critic(neth_learn *L, neth_critic *C, const neth_sc_t *psc);
/* kl_stop's counters since the last call (device to host, synchronous on the learner's stream): the Adam steps taken,
 * the updates stopped early */
void neth_learn_kstat(neth_learn *L, long long *taken, long long *stopped);
/* training telemetry since the last call (device to host, synchronous on the learner's stream):
 * out[0] mean grad norm (pre-clip), out[1] max grad norm, out[2] NaN/inf skip count, out[3] mean explained variance */
void neth_learn_tstat(neth_learn *L, double out[4]);

#ifdef __cplusplus
}
#endif

#endif
