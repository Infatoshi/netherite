/* Blaze policy: opaque C ABI.
 * Operations: create, forward, sample, update, load, save, destroy.
 * Backend details stay behind this interface. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Nn Nn;

/* Linked backends. Selected unavailable backend fails; never falls back. */
typedef enum NnBackend {
  NN_BACKEND_CPU = 0,
  NN_BACKEND_CUDA = 1,
  NN_BACKEND_METAL = 2
} NnBackend;

/* Training / sampling knobs. No environment variables. */
typedef struct NnConfig {
  float lr;            /* Adam learning rate */
  float ppo_clip;      /* PPO ratio clip epsilon */
  float value_coef;    /* coefficient on mean squared value error */
  float entropy_coef;  /* coefficient on mean entropy (subtracted) */
  float grad_limit;    /* global L2 gradient norm clip */
  uint64_t rng_seed;   /* base hash-RNG seed for Gumbel sample */
} NnConfig;

typedef enum NnPrec {
  NN_PREC_FAST = 0,
  NN_PREC_F32 = 1
} NnPrec;

/* Create descriptor. backend, device, max_n, and training config are required.
 * device: CPU and Metal accept 0 only; CUDA uses a device ordinal.
 * max_n: CPU/CUDA allow n in [1,max_n]; Metal fixes the batch at max_n. */
typedef struct NnCreate {
  NnBackend backend;
  int device;
  int max_n;
  NnConfig config;
  NnPrec prec;
} NnCreate;

typedef struct NnCreate NnDesc;

/* Optional metrics from one PPO update.
 * approx_kl / clipfrac are diagnostic only: they must not enter the loss
 * or change gradients, weights, RNG, or Adam. */
typedef struct NnUpdateStats {
  float policy_loss;
  float value_loss;
  float entropy_mean;
  float total_loss;
  float grad_norm; /* pre-clip L2 norm */
  float approx_kl; /* mean(ratio - 1 - log(ratio)); k1 */
  float clipfrac;  /* mean(|ratio - 1| > ppo_clip) */
} NnUpdateStats;

/* Sample mode */
enum {
  NN_SAMPLE_GUMBEL = 0,
  NN_SAMPLE_GREEDY = 1
};

/* Default config (matches chain trainer historical defaults). */
static inline NnConfig nn_config_default(void) {
  NnConfig c;
  c.lr = 3e-4f;
  c.ppo_clip = 0.2f;
  c.value_coef = 0.5f;
  c.entropy_coef = 0.01f;
  c.grad_limit = 0.5f;
  c.rng_seed = 0;
  return c;
}

/* Create handle from descriptor. desc must be non-NULL.
 * Returns NULL on failure (invalid desc, unavailable backend, OOM, ...). */
Nn *nn_create(const NnCreate *desc);

void nn_destroy(Nn *nn);

/* Replace config (lr, clip, coefs, grad limit, seed). Does not reallocate. */
int nn_set_config(Nn *nn, const NnConfig *cfg);

/* Current Adam update count. Starts at zero; set_config preserves it.
 * A successful weights-only load is a warm start: moments and count reset.
 * Read-only; returns -1 for a null handle or unavailable backend. */
int64_t nn_training_steps(const Nn *nn);


/* Build every backend plan for batch size n up front. n in [1, max_n].
 * CUDA: races cuDNN engines for the n bucket now and shrinks the workspace
 * arena to what the chosen plans need. create already prepares max_n, so call
 * this once for each other n the run will pass to forward or update.
 * CPU and Metal: no-op. Returns 0 on success. */
int nn_prepare_n(Nn *nn, int n);

/* Freeze the prepared set. After the seal a batch size whose plans were not
 * prepared fails with "nn: unprepared bucket n=... conv=... lt=..." instead of
 * building and timing plans mid-run. The check covers both plan sets: conv
 * plans are bucketed, dense plans are keyed by exact n. CPU and Metal: no-op. */
int nn_seal(Nn *nn);

/* Forward: planes [n,18,36,64] uint8 NCHW, scalars [n,27] float32.
 * logits [n,34] float32, values [n] float32.
 * CPU/CUDA: n in [1, max_n]. Metal: n must equal max_n.
 * Returns 0 on success. */
int nn_forward(Nn *nn, const uint8_t *planes, const float *scalars, int n,
               float *logits, float *values);

/* Sample from packed logits [n,34].
 * mode: NN_SAMPLE_GUMBEL or NN_SAMPLE_GREEDY.
 * acts [n,9] int32, logp [n], entropy [n] (entropy may be NULL).
 * Gumbel mixes rng_seed with a per-handle sample_step; each sample
 * consumes one step. sample_step resets on create or rng_seed change.
 * Returns 0 on success. */
int nn_sample(Nn *nn, const float *logits, int n, int mode, int32_t *acts,
              float *logp, float *entropy);

/* One clipped PPO step with fixed actions, full backprop, grad clip, Adam.
 * planes/scalars as forward; acts [n,9]; old_logp/advantages/returns [n].
 * stats may be NULL. Returns 0 on success. */
int nn_update(Nn *nn, const uint8_t *planes, const float *scalars,
              const int32_t *acts, const float *old_logp,
              const float *advantages, const float *returns, int n,
              NnUpdateStats *stats);

/* Schema-1 weights-only checkpoint (no optimizer state). Returns 0 on success. */
int nn_load(Nn *nn, const char *path);
int nn_save(const Nn *nn, const char *path);

/* Last error message (static buffer). */
const char *nn_last_error(void);

#ifdef __cplusplus
}
#endif
