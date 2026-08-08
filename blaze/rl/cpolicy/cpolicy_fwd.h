/* cpolicy_fwd: fused C/CUDA rollout policy forward.
 *
 * uint8 obs -> conv -> conv -> fc -> heads -> Gumbel/greedy sample
 * -> actions + logp + value + entropy. Weights uploaded from torch once
 * per chunk. Rollout only; the PPO update stays in torch.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CPolicy CPolicy;

/* Create a handle. device = CUDA device ordinal. max_n = max batch size
 * for workspace allocation (e.g. N_ENVS). Returns NULL on failure. */
CPolicy *cpolicy_create(int device, int max_n);

void cpolicy_destroy(CPolicy *h);

/* Upload all weight blobs (host or device; flags say which). Layout matches
 * torch ChainPolicy state_dict order:
 *   conv1_w [32,18,5,5]  conv1_b [32]
 *   conv2_w [64,32,3,3]  conv2_b [64]
 *   fc_w    [256,6299]   fc_b    [256]
 *   heads_w packed [34,256] row-major per head in order (3,3,3,2,2,2,7,2,10)
 *   heads_b packed [34]
 *   value_w [1,256]      value_b [1]
 * on_device != 0 means each pointer is a device pointer (same device as
 * handle); otherwise host pointers (H2D copy). */
int cpolicy_upload_weights(CPolicy *h,
                           const float *conv1_w, const float *conv1_b,
                           const float *conv2_w, const float *conv2_b,
                           const float *fc_w, const float *fc_b,
                           const float *heads_w, const float *heads_b,
                           const float *value_w, const float *value_b,
                           int on_device);

/* Forward + sample. All device pointers on handle's device.
 * obs_u8  [n,18,36,64] uint8 NCHW
 * scal    [n,27] float32
 * burnin  [n] uint8 (0/1) - if null, treated as all false
 * noop    [9] int64 no-op action row for burn-in lanes
 * mode    0 = Gumbel sample, 1 = greedy argmax
 * stream  cudaStream_t as void* (0 = default stream)
 * Outputs (device, caller-owned):
 *   acts     [n,9] int64
 *   logp     [n]   float32 joint log-prob of acts
 *   value    [n]   float32
 *   entropy  [n]   float32 summed head entropy (null ok)
 *   logits   [n,34] float32 packed head logits (null ok; for equiv gate)
 * Returns 0 on success. */
int cpolicy_forward_sample(CPolicy *h,
                           const uint8_t *obs_u8,
                           const float *scal,
                           const uint8_t *burnin,
                           const int64_t *noop,
                           int n,
                           int mode,
                           uint64_t rng_seed,
                           void *stream,
                           int64_t *acts,
                           float *logp,
                           float *value,
                           float *entropy,
                           float *logits);

/* Last error message (static buffer). */
const char *cpolicy_last_error(void);

#ifdef __cplusplus
}
#endif
