/* Internal Metal backend for the Blaze policy.
 * Opaque NnMetal handle. MPSGraph owns forward and update.
 * Not part of the public nn.h ABI. */
#pragma once

#include "nn.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NnMetal NnMetal;

/* Create handle with fixed batch size batch_n.
 * device must be 0. Every forward/update/sample must use exactly batch_n.
 * cfg may be NULL (uses nn_config_default). Returns NULL on failure. */
NnMetal *nn_metal_create(int batch_n, int device_id, const NnConfig *cfg);

void nn_metal_destroy(NnMetal *nn);

/* Replace config (lr, clip, coefs, grad limit, seed). Does not reallocate. */
/* Read the current Adam update count; -1 for NULL. */
int64_t nn_metal_training_steps(const NnMetal *nn);
int nn_metal_set_config(NnMetal *nn, const NnConfig *cfg);

/* Forward: planes [n,18,36,64] uint8 NCHW, scalars [n,27] float32.
 * n must equal the create batch size. logits [n,34], values [n]. */
int nn_metal_forward(NnMetal *nn, const uint8_t *planes, const float *scalars,
                     int n, float *logits, float *values);

/* Sample from packed logits [n,34]. Host-side hash RNG (fixture protocol).
 * mode: NN_SAMPLE_GUMBEL or NN_SAMPLE_GREEDY. */
int nn_metal_sample(NnMetal *nn, const float *logits, int n, int mode,
                    int32_t *acts, float *logp, float *entropy);

/* One clipped PPO step: MPSGraph forward, AD, grad clip, Adam.
 * n must equal create batch size. stats may be NULL. */
int nn_metal_update(NnMetal *nn, const uint8_t *planes, const float *scalars,
                    const int32_t *acts, const float *old_logp,
                    const float *advantages, const float *returns, int n,
                    NnUpdateStats *stats);

/* Schema-1 weights-only checkpoint (shared fixture format). */
int nn_metal_load(NnMetal *nn, const char *path);
int nn_metal_save(const NnMetal *nn, const char *path);

/* Last error message (static buffer). */
const char *nn_metal_last_error(void);

#ifdef __cplusplus
}
#endif
