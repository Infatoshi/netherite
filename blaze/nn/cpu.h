/* Internal CPU backend for the Blaze policy.
 * Not part of the public nn.h ABI. */
#pragma once

#include "nn.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NnCpu NnCpu;

/* device_id must be 0. cfg may be NULL (uses nn_config_default). */
NnCpu *nn_cpu_create(int max_n, int device_id, const NnConfig *cfg);
void nn_cpu_destroy(NnCpu *nn);

/* Read the current Adam update count; -1 for NULL. */
int64_t nn_cpu_training_steps(const NnCpu *nn);
int nn_cpu_set_config(NnCpu *nn, const NnConfig *cfg);

int nn_cpu_forward(NnCpu *nn, const uint8_t *planes, const float *scalars,
                   int n, float *logits, float *values);

int nn_cpu_sample(NnCpu *nn, const float *logits, int n, int mode,
                  int32_t *acts, float *logp, float *entropy);

int nn_cpu_update(NnCpu *nn, const uint8_t *planes, const float *scalars,
                  const int32_t *acts, const float *old_logp,
                  const float *advantages, const float *returns, int n,
                  NnUpdateStats *stats);

int nn_cpu_load(NnCpu *nn, const char *path);
int nn_cpu_save(const NnCpu *nn, const char *path);

const char *nn_cpu_last_error(void);

/* Fixture helpers (CPU-only internals; public entry dispatches in nn.c). */
int nn_cpu_fixture_get_params(const NnCpu *nn, float *dst, size_t count);
int nn_cpu_fixture_set_params(NnCpu *nn, const float *src, size_t count);
int nn_cpu_fixture_get_grads(const NnCpu *nn, float *dst, size_t count);
size_t nn_cpu_fixture_copy_layer(const NnCpu *nn, int layer, int sample,
                                 float *dst, size_t max_n);

#ifdef __cplusplus
}
#endif
