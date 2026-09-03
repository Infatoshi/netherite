/* Blaze policy CUDA backend: opaque C ABI.
 * Operations: create, forward, sample, update, load, save, destroy.
 * Backend uses CUDA Runtime, cuDNN, and cuBLAS. No Python/LibTorch. */
#pragma once

#include "nn.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NnCuda NnCuda;

/* Create handle with fixed arenas for batches up to max_n on CUDA device.
 * device is the CUDA ordinal (fixed for the handle lifetime).
 * cfg may be NULL (uses nn_config_default). Returns NULL on failure. */
NnCuda *nn_cuda_create(const NnCreate *desc);

void nn_cuda_destroy(NnCuda *nn);

/* Build and time every cuDNN plan for bucket_n(n) now. n in [1, max_n].
 * create already prepares max_n; call this once per other n the run uses. */
int nn_cuda_prepare_n(NnCuda *nn, int n);

/* Freeze the prepared bucket set. After the seal a batch size fails with
 * "nn: unprepared bucket ... conv=... lt=..." instead of racing cuDNN engines
 * mid-run. The guard checks the conv bucket AND an exact-n cuBLASLt plan, so
 * an n that shares a bucket with a prepared n is rejected too. Irreversible
 * for the handle. */
int nn_cuda_seal(NnCuda *nn);

/* Replace config (lr, clip, coefs, grad limit, seed). Does not reallocate. */
int nn_cuda_set_config(NnCuda *nn, const NnConfig *cfg);

/* Forward: planes [n,18,36,64] uint8 NCHW, scalars [n,27] float32 (host).
 * logits [n,34] float32, values [n] float32 (host). n in [1, max_n].
 * Returns 0 on success. Synchronizes before return. */
int nn_cuda_forward(NnCuda *nn, const uint8_t *planes, const float *scalars,
                    int n, float *logits, float *values);

/* Sample from packed logits [n,34] (host).
 * mode: NN_SAMPLE_GUMBEL or NN_SAMPLE_GREEDY.
 * acts [n,9] int32, logp [n], entropy [n] (entropy may be NULL).
 * Uses nn config rng_seed for Gumbel. Returns 0 on success. */
int nn_cuda_sample(NnCuda *nn, const float *logits, int n, int mode,
                   int32_t *acts, float *logp, float *entropy);

/* One clipped PPO step with fixed actions, full backprop, grad clip, Adam.
 * Host pointers; acts [n,9]; old_logp/advantages/returns [n].
 * stats may be NULL. Returns 0 on success. Synchronizes before return. */
int nn_cuda_update(NnCuda *nn, const uint8_t *planes, const float *scalars,
                   const int32_t *acts, const float *old_logp,
                   const float *advantages, const float *returns, int n,
                   NnUpdateStats *stats);

/* Schema-1 weights-only checkpoint (no optimizer state). Returns 0 on success.
 * Load resets Adam step and moments. */
int nn_cuda_load(NnCuda *nn, const char *path);
int nn_cuda_save(const NnCuda *nn, const char *path);

/* Last error message (static buffer). */
const char *nn_cuda_last_error(void);

#ifdef __cplusplus
}
#endif
