/* Checked binary fixture and checkpoint I/O for Blaze policy weights.
 * Explicit little-endian fields. No native struct writes.
 * Schema 1: weights-only fixture checkpoint (no optimizer state). */
#pragma once

#include "model.h"
#include "nn.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Checkpoint magic "BNN1" little-endian and schema. */
enum {
  NN_CKPT_MAGIC = 0x314E4E42u, /* 'B''N''N''1' */
  NN_CKPT_SCHEMA = 1u,         /* weights only; not an optimizer resume image */
  NN_DTYPE_F32 = 1u
};

/* FNV-1a 64-bit over bytes. */
uint64_t nn_fnv1a64(const void *data, size_t n);

/* Hash RNG protocol (same mix as blaze/rl/cpolicy/cpolicy_fwd.cu u01). */
float nn_hash_u01(uint64_t seed, uint32_t a, uint32_t b, uint32_t c);
float nn_gumbel0(float u);

/* Save/load weight tensors only (schema 1: no Adam / optimizer state).
 * Each FP32 payload value is stored as IEEE-754 bits in little-endian order.
 * Validates magic, schema, model hash, table, lengths, and payload hash. */
int nn_fixture_save(const char *path, const float *const *tensors);
int nn_fixture_load(const char *path, float **tensors);

/* Convenience: pack/unpack contiguous param blob in model order. */
size_t nn_fixture_param_count(void);
void nn_fixture_pack_params(const float *const *tensors, float *dst);
void nn_fixture_unpack_params(const float *src, float **tensors);

/* Parameter blob get/set on a live handle (for tests and FD checks). */
int nn_fixture_get_params(const Nn *nn, float *dst, size_t count);
int nn_fixture_set_params(Nn *nn, const float *src, size_t count);
int nn_fixture_get_grads(const Nn *nn, float *dst, size_t count);

/* Layer activation snapshots after the last forward/update (host, n from call).
 * layer: 0=conv1, 1=conv2, 2=fc_in, 3=hidden, 4=logits, 5=value.
 * Returns element count written, or 0 on error. */
size_t nn_fixture_copy_layer(const Nn *nn, int layer, int sample,
                             float *dst, size_t max_n);

/* Deterministic weight init for fixtures (hash of index + seed). */
void nn_fixture_init_weights(float **tensors, uint64_t seed);

#ifdef __cplusplus
}
#endif
