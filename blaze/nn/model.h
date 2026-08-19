/* Blaze policy model declaration: one tensor order, one set of shapes.
 * CPU, CUDA, and Metal backends share this contract. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- geometry ---- */
enum {
  NN_CAM_H = 36,
  NN_CAM_W = 64,
  NN_N_PLANES = 9,
  NN_STACK = 2,
  NN_N_CH = 18, /* NN_N_PLANES * NN_STACK */
  NN_N_SCAL = 27,
  NN_N_HEAD = 9,
  NN_W_MAX = 10,
  NN_C_OUT1 = 32,
  NN_C_OUT2 = 64,
  NN_K1 = 5,
  NN_K2 = 3,
  NN_S1 = 2,
  NN_S2 = 2,
  NN_H1 = 16, /* (36-5)/2+1 */
  NN_W1 = 30, /* (64-5)/2+1 */
  NN_H2 = 7,  /* (16-3)/2+1 */
  NN_W2 = 14, /* (30-3)/2+1 */
  NN_FLAT = 6272, /* 64*7*14 */
  NN_FC_IN = 6299, /* 6272+27 */
  NN_FC_OUT = 256,
  NN_N_LOGITS = 34 /* sum of head widths */
};

/* Categorical head widths, in order. */
static const int NN_HEAD_WIDTHS[NN_N_HEAD] = {3, 3, 3, 2, 2, 2, 7, 2, 10};

/* Prefix offsets into the packed-34 logit vector. */
static const int NN_HEAD_OFF[NN_N_HEAD] = {0, 3, 6, 9, 11, 13, 15, 22, 24};

/* Depth plane indices scaled by 1/255 (matches cpolicy_fwd). */
static const int NN_DEPTH_CH0 = 7;
static const int NN_DEPTH_CH1 = 16;

/* ---- tensor table (checkpoint / param order) ---- */
typedef enum NnTensorId {
  NN_T_CONV1_W = 0,
  NN_T_CONV1_B,
  NN_T_CONV2_W,
  NN_T_CONV2_B,
  NN_T_FC_W,
  NN_T_FC_B,
  NN_T_HEADS_W,
  NN_T_HEADS_B,
  NN_T_VALUE_W,
  NN_T_VALUE_B,
  NN_T_COUNT
} NnTensorId;

/* Element counts per tensor (float32). */
static const size_t NN_TENSOR_FLOATS[NN_T_COUNT] = {
    /* conv1_w [32,18,5,5] */ (size_t)NN_C_OUT1 * NN_N_CH * NN_K1 * NN_K1,
    /* conv1_b [32]        */ (size_t)NN_C_OUT1,
    /* conv2_w [64,32,3,3] */ (size_t)NN_C_OUT2 * NN_C_OUT1 * NN_K2 * NN_K2,
    /* conv2_b [64]        */ (size_t)NN_C_OUT2,
    /* fc_w    [256,6299]  */ (size_t)NN_FC_OUT * NN_FC_IN,
    /* fc_b    [256]       */ (size_t)NN_FC_OUT,
    /* heads_w [34,256]    */ (size_t)NN_N_LOGITS * NN_FC_OUT,
    /* heads_b [34]        */ (size_t)NN_N_LOGITS,
    /* value_w [1,256]     */ (size_t)NN_FC_OUT,
    /* value_b [1]         */ (size_t)1};

static const char *const NN_TENSOR_NAMES[NN_T_COUNT] = {
    "conv1_w", "conv1_b", "conv2_w", "conv2_b", "fc_w",
    "fc_b",    "heads_w", "heads_b", "value_w", "value_b"};

/* Rank and shape (row-major). Unused dims are 0. */
static const int NN_TENSOR_NDIM[NN_T_COUNT] = {4, 1, 4, 1, 2, 1, 2, 1, 2, 1};

static const int32_t NN_TENSOR_SHAPE[NN_T_COUNT][4] = {
    {NN_C_OUT1, NN_N_CH, NN_K1, NN_K1},
    {NN_C_OUT1, 0, 0, 0},
    {NN_C_OUT2, NN_C_OUT1, NN_K2, NN_K2},
    {NN_C_OUT2, 0, 0, 0},
    {NN_FC_OUT, NN_FC_IN, 0, 0},
    {NN_FC_OUT, 0, 0, 0},
    {NN_N_LOGITS, NN_FC_OUT, 0, 0},
    {NN_N_LOGITS, 0, 0, 0},
    {1, NN_FC_OUT, 0, 0},
    {1, 0, 0, 0}};

/* Total float parameters. */
static inline size_t nn_model_param_floats(void) {
  size_t n = 0;
  for (int i = 0; i < NN_T_COUNT; ++i)
    n += NN_TENSOR_FLOATS[i];
  return n;
}

/* Architecture hash over fixed shapes (stable across runs). */
static inline uint64_t nn_model_hash(void) {
  uint64_t h = 0xcbf29ce484222325ULL;
  const uint64_t prime = 0x100000001b3ULL;
  const int vals[] = {NN_CAM_H,   NN_CAM_W,  NN_N_CH,   NN_N_SCAL, NN_C_OUT1,
                      NN_C_OUT2,  NN_K1,     NN_K2,     NN_S1,     NN_S2,
                      NN_H1,      NN_W1,     NN_H2,     NN_W2,     NN_FLAT,
                      NN_FC_IN,   NN_FC_OUT, NN_N_HEAD, NN_N_LOGITS};
  for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); ++i) {
    h ^= (uint64_t)(uint32_t)vals[i];
    h *= prime;
  }
  for (int i = 0; i < NN_N_HEAD; ++i) {
    h ^= (uint64_t)(uint32_t)NN_HEAD_WIDTHS[i];
    h *= prime;
  }
  for (int t = 0; t < NN_T_COUNT; ++t) {
    for (const char *p = NN_TENSOR_NAMES[t]; *p; ++p) {
      h ^= (uint64_t)(uint8_t)*p;
      h *= prime;
    }
    h ^= (uint64_t)NN_TENSOR_FLOATS[t];
    h *= prime;
  }
  return h;
}

#ifdef __cplusplus
}
#endif
