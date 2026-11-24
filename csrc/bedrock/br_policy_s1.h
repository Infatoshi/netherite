#ifndef NETHERITE_BEDROCK_POLICY_S1_H
#define NETHERITE_BEDROCK_POLICY_S1_H

/* Netherite Bedrock Lane C: NVFP4 Fwd / Selectable (FP16 | FP8 | NVFP4-SR) Bwd
 * PPO Policy with exact csrc/runtime/train/net.h factored s1 action heads + BRT1 I/O.
 *
 * Pure C11 header (no CUDA includes required by callers).
 */

#include "br_contract.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Packed head row layout inside [BR_S1_HEAD_PAD, H] FP16/FP32 weight & [BR_S1_HEAD_PAD] bias:
 *   0..40   : pitch bin logits (41)
 *   41..81  : yaw bin logits   (41)
 *   82..95  : Bernoulli keys   (14)
 *   96..105 : hotbar category  (10)
 *   106     : scalar critic V(s) (1)
 *   107..111: zero padding for 16-aligned Tensor Core GEMMs
 */
#define BR_S1_HEAD_CAM_OFF   0
#define BR_S1_HEAD_CAM_DIM   (2 * BRC_CAM_BINS)   /* 82 */
#define BR_S1_HEAD_KEY_OFF   82
#define BR_S1_HEAD_KEY_DIM   BRC_KEYS             /* 14 */
#define BR_S1_HEAD_HOT_OFF   96
#define BR_S1_HEAD_HOT_DIM   BRC_HOTBAR_CATS      /* 10 */
#define BR_S1_HEAD_VAL_OFF   106
#define BR_S1_HEAD_DIM       107                  /* 82 + 14 + 10 + 1 */
#define BR_S1_HEAD_PAD       112                  /* 16-aligned packed rows */

/* Backward pass precision route for square trunk W2 (W1 and output heads stay FP16/FP32) */
enum br_s1_bwd_prec {
    BR_S1_BWD_FP16  = 0,
    BR_S1_BWD_FP8   = 1,
    BR_S1_BWD_NVFP4 = 2
};

typedef struct {
    int      batch_q;           /* Minibatch / rollout chunk size Q (multiple of 64, default 8192) */
    int      hidden;            /* Trunk width H (multiple of 128, default 1024) */
    int      use_nvfp4_fwd;     /* 1 = Square-NVFP4 + fused ReLU forward, 0 = FP16 forward */
    int      bwd_prec;          /* enum br_s1_bwd_prec: FP16 | FP8 | NVFP4-SR on W2 */
    float    lr;                /* AdamW learning rate (default 3e-4) */
    float    weight_decay;      /* AdamW decoupled weight decay (default 0.0) */
    float    grad_clip;         /* Elementwise grad clip (default 1.0, <=0 disables) */
    float    clip_eps;          /* PPO clip epsilon (default 0.2) */
    float    vf_coef;           /* Value loss coefficient (default 0.5) */
    float    ent_coef;          /* Entropy bonus coefficient (default 0.01) */
    float    grad_loss_scale;   /* Backward loss scale (default 16.0 for NVFP4 UE4M3) */
    float    hot_keep_bias;     /* Initial b_hot[0] bias (default 0.0; 4.0 matches fresh net.h) */
    uint64_t seed;              /* Weight init + Philox RNG base seed */
} br_policy_s1_cfg_t;

typedef struct {
    double policy_loss;
    double value_loss;
    double entropy;
    double approx_kl;
    double clip_frac;
    double update_ms;
} br_policy_s1_stats_t;

typedef struct br_policy_s1 br_policy_s1_t;

static inline const char *br_s1_bwd_name(int bwd_prec) {
    static const char *names[3] = {"fp16", "fp8", "nvfp4"};
    return (bwd_prec >= 0 && bwd_prec < 3) ? names[bwd_prec] : "?";
}

/* Create / destroy policy instance (allocates GPU weights, AdamW state, cuBLASLt plans) */
br_policy_s1_t *br_policy_s1_create(const br_policy_s1_cfg_t *cfg);
void            br_policy_s1_destroy(br_policy_s1_t *pol);

/* Access / override CUDA stream (opaque cudaStream_t) */
void *br_policy_s1_stream(const br_policy_s1_t *pol);
void  br_policy_s1_set_stream(br_policy_s1_t *pol, void *cuda_stream);

/* Device pointer to latest forward output: [batch_q, BR_S1_HEAD_PAD] FP32 */
const float *br_policy_s1_head_out_dev(const br_policy_s1_t *pol);

/* Forward pass only: d_obs_f16 is [batch_q, BRC_OBS_DIM] FP16 on device. */
void br_policy_s1_forward(br_policy_s1_t *pol, const void *d_obs_f16);

/* Forward + exact net.h factored s1 sampling on device:
 *   d_obs_f16  : [batch_q, BRC_OBS_DIM] __half
 *   d_act_out  : [batch_q] brc_s1_action_t
 *   d_logp_out : [batch_q] float
 *   d_val_out  : [batch_q] float
 *   step_ctr   : Philox step counter
 *   greedy     : 0 = stochastic Gumbel-max + Bernoulli, 1 = deterministic argmax/threshold */
void br_policy_s1_act(br_policy_s1_t *pol,
                      const void *d_obs_f16,
                      brc_s1_action_t *d_act_out,
                      float *d_logp_out,
                      float *d_val_out,
                      uint64_t step_ctr,
                      int greedy);

/* Compute advantage sum and sum-of-squares on device into d_adv_stats_2f[2] */
void br_policy_s1_adv_stats(br_policy_s1_t *pol,
                            const float *d_adv,
                            float *d_adv_stats_2f,
                            int total_samples);

/* One minibatch (batch_q samples) PPO forward + factored loss/grad + backward + AdamW + requant.
 * If d_adv_stats_2f != NULL, advantages are normalized with (d_adv_stats_2f, total_samples);
 * otherwise d_adv is used directly.
 * If d_loss_acc_5f != NULL, accumulates [pg, vl, ent, kl, clip_cnt] on device. */
void br_policy_s1_ppo_minibatch(br_policy_s1_t *pol,
                                const void *d_obs_f16,
                                const brc_s1_action_t *d_acts,
                                const float *d_old_logp,
                                const float *d_adv,
                                const float *d_ret,
                                const float *d_adv_stats_2f,
                                int total_samples,
                                uint32_t sr_seed,
                                float *d_loss_acc_5f);

/* Full multi-epoch PPO update over contiguous device rollout buffers (total_samples = k * batch_q). */
void br_policy_s1_ppo_update(br_policy_s1_t *pol,
                             const void *d_obs_f16,
                             const brc_s1_action_t *d_acts,
                             const float *d_old_logp,
                             const float *d_adv,
                             const float *d_ret,
                             int total_samples,
                             int ppo_epochs,
                             uint32_t iter_seed,
                             br_policy_s1_stats_t *out_stats);

/* Re-sync FP16 + swizzled NVFP4 device weights from FP32 master weights */
void br_policy_s1_sync_weights(br_policy_s1_t *pol);

/* BRT1 Teacher Checkpoint Save & Load (exact br_contract.h format; load auto-syncs device weights).
 * Returns 0 on success, non-zero on error. */
int br_policy_s1_save(const br_policy_s1_t *pol, const char *path);
int br_policy_s1_load(br_policy_s1_t *pol, const char *path);

/* Synchronize policy CUDA stream */
void br_policy_s1_sync(br_policy_s1_t *pol);

#ifdef __cplusplus
}
#endif

#endif /* NETHERITE_BEDROCK_POLICY_S1_H */
