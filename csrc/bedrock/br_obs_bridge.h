#ifndef NETHERITE_BEDROCK_OBS_BRIDGE_H
#define NETHERITE_BEDROCK_OBS_BRIDGE_H

/* Lane D (br-distill-bridge):
 *   1. Exact 128-dim v2 observation extraction from live libnetherite.a struct world
 *      and from brc_seed_entry_t + delta overlay (for CPU/GPU parity cross-checks).
 *   2. CPU BRT1 teacher checkpoint loader & FP32 forward inference producing
 *      exact net.h s1 factored actions + soft head probabilities.
 *   3. Compile-time ABI-verified distill.cu label record emitter (brc_distill_rec_t).
 */

#include <stddef.h>
#include <stdint.h>

#include "br_contract.h"

#ifdef __cplusplus
extern "C" {
#define BRC_STATIC_ASSERT(cond, msg) static_assert((cond), msg)
#else
#define BRC_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#endif

struct world;

/* ============================================================================
 * 1. Observation Extraction (v2 128-dim layout matching br_contract.h)
 * ============================================================================ */

/* Per-block edit overlay entry in local bubble coordinates (0..63, 0..31, 0..63).
 * Applied in order (later entries override earlier entries). */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
    uint8_t block_id;
    uint8_t meta;
} br_delta_entry_t;

/* Complete player & task state required to build the 128-dim v2 observation.
 * Coordinate frame:
 *   - For br_extract_obs_from_world():  (px,py,pz) and (lava_x,lava_y,lava_z) are world coords.
 *   - For br_extract_obs_from_bubble(): (px,py,pz) and (lava_x,lava_y,lava_z) are local bubble coords.
 */
typedef struct {
    double   px, py, pz;             /* player feet position */
    double   vx, vy, vz;             /* player velocity (blocks/tick) */
    float    yaw_deg;                /* Minecraft yaw in degrees (0 = +Z, +90 = -X) */
    float    pitch_deg;              /* Minecraft pitch in degrees (-90 = up, +90 = down) */
    uint8_t  on_ground;              /* 0 or 1 */
    uint8_t  sneaking;               /* 0 or 1 */
    float    hp;                     /* health (0..20) */
    uint16_t hotbar_item[9];         /* Minecraft 1.7.10 item IDs in slots 0..8 */
    uint8_t  hotbar_cnt[9];          /* stack counts in slots 0..8 */
    uint8_t  sel_slot;               /* selected hotbar slot (0..8) */
    double   lava_x, lava_y, lava_z; /* target lava center in same coord frame as player */
    int32_t  obsidian_placed;        /* obsidian blocks cast/placed so far */
    uint8_t  portal_lit;             /* 0 or 1 */
    int32_t  ep_tick;                /* current episode tick */
    int32_t  max_ticks;              /* episode horizon */
} br_obs_state_t;

/* Extract exact 128-dim v2 observation from a live libnetherite.a struct world. */
void br_extract_obs_from_world(struct world *w,
                               const br_obs_state_t *st,
                               float obs[BRC_OBS_DIM]);

/* Extract identical 128-dim v2 observation from a BRS1 seed bubble + delta overlay. */
void br_extract_obs_from_bubble(const brc_seed_entry_t *seed,
                                const br_delta_entry_t *deltas,
                                int n_deltas,
                                const br_obs_state_t *st,
                                float obs[BRC_OBS_DIM]);

/* Exact 3D Amanatides-Woo voxel raycast on a live struct world (for crosshair/USE/MINE).
 * Returns hit block ID (0 if open air within max_dist); fills hit coords & distance if non-NULL. */
uint8_t br_world_raycast(struct world *w,
                         double ox, double oy, double oz,
                         float yaw_deg, float pitch_deg,
                         double max_dist,
                         int *hit_x, int *hit_y, int *hit_z,
                         int *prev_x, int *prev_y, int *prev_z,
                         float *out_dist);

/* ============================================================================
 * 2. CPU BRT1 Teacher Loader & Forward Inference
 * ============================================================================ */

typedef struct {
    brc_teacher_hdr_t hdr;
    float *weights;   /* single contiguous allocation of all FP32 tensors */
    size_t n_weights;
    /* Pointers into weights[] in exact BRT1 file order */
    float *w1;        /* [hidden * obs_dim] */
    float *w2;        /* [hidden * hidden] */
    float *w_cam;     /* [(2 * cam_bins) * hidden] */
    float *b_cam;     /* [2 * cam_bins] */
    float *w_keys;    /* [n_keys * hidden] */
    float *b_keys;    /* [n_keys] */
    float *w_hot;     /* [n_hotbar * hidden] */
    float *b_hot;     /* [n_hotbar] */
    float *w_val;     /* [1 * hidden] */
    float *b_val;     /* [1] */
    /* Preallocated hidden scratch buffers (no heap alloc during inference) */
    float *h1;        /* [hidden] */
    float *h2;        /* [hidden] */
} br_teacher_t;

typedef struct {
    float cam_logits[2 * BRC_CAM_BINS];
    float key_logits[BRC_KEYS];
    float hot_logits[BRC_HOTBAR_CATS];
    float pitch_probs[BRC_CAM_BINS];
    float yaw_probs[BRC_CAM_BINS];
    float key_probs[BRC_KEYS];
    float hot_probs[BRC_HOTBAR_CATS];
    float value;
} br_teacher_out_t;

/* Compute exact number of FP32 elements in a BRT1 checkpoint for given header dims. */
size_t br_teacher_num_floats(const brc_teacher_hdr_t *hdr);

/* Allocate an empty zeroed BRT1 teacher with given hidden width (e.g. 1024 or 2048). */
br_teacher_t *br_teacher_alloc(uint32_t hidden);

/* Load a BRT1 checkpoint from disk. Returns NULL on error (message written to err if non-NULL). */
br_teacher_t *br_teacher_load(const char *path, char *err, size_t err_sz);

/* Save a BRT1 checkpoint to disk. Returns 0 on success, -1 on error. */
int br_teacher_save(const br_teacher_t *t, const char *path, char *err, size_t err_sz);

/* Free a BRT1 teacher and its buffers. */
void br_teacher_free(br_teacher_t *t);

/* Run exact FP32 forward pass: obs[128] -> ReLU(W1*obs) -> ReLU(W2*h1) -> heads.
 *   temperature <= 0.0f : greedy argmax (cam/hot) and >= 0.5 threshold (keys); probs at T=1.0.
 *   temperature >  0.0f : stochastic sampling using *rng_state; probs at T=temperature.
 *   out_act and out_probs may be NULL independently if caller only needs one. */
void br_teacher_infer(br_teacher_t *t,
                      const float obs[BRC_OBS_DIM],
                      float temperature,
                      uint64_t *rng_state,
                      brc_s1_action_t *out_act,
                      br_teacher_out_t *out_probs);

/* ============================================================================
 * 3. distill.cu Record Adapter (ABI-verified against csrc/runtime/train/distill.cu)
 * ============================================================================ */

/* Binary-identical layout to struct neth_distill_rec in csrc/runtime/train/distill.cu
 * when NETH_NET_HOTBAR == 1 (NETH_NET_ACTION == 17, sizeof == 100 bytes). */
typedef struct {
    int32_t a[BRC_S1_ACT_DIM]; /* [0]=pitch_bin, [1]=yaw_bin, [2..15]=keys 0/1, [16]=hotbar 0..9 */
    int32_t p[4];              /* prev pitch_bin, prev yaw_bin, prev keys bitmask, last macro kind */
    float   w;                 /* label weight: 1.0 trained, 0.0 masked */
    int32_t valid;             /* decision step: value is trained */
    int32_t soft;              /* label is teacher action (--soft target) */
    int32_t hold;              /* removed macro's act holding attack (--hold-weight) */
} brc_distill_rec_t;

/* Binary-identical layout to struct neth_distill_rec when NETH_NET_HOTBAR == 0
 * (NETH_NET_ACTION == 16, sizeof == 96 bytes). */
typedef struct {
    int32_t a[BRC_S1_ACT_DIM - 1]; /* [0]=pitch_bin, [1]=yaw_bin, [2..15]=keys 0/1 */
    int32_t p[4];
    float   w;
    int32_t valid;
    int32_t soft;
    int32_t hold;
} brc_distill_rec_nohot_t;

/* Compile-time ABI checks against csrc/runtime/train/distill.cu::struct neth_distill_rec */
BRC_STATIC_ASSERT(sizeof(brc_distill_rec_t) == 100,
                  "brc_distill_rec_t must be 100 bytes (distill.cu NETH_NET_HOTBAR=1)");
BRC_STATIC_ASSERT(offsetof(brc_distill_rec_t, a) == 0,
                  "brc_distill_rec_t.a offset mismatch");
BRC_STATIC_ASSERT(offsetof(brc_distill_rec_t, p) == 68,
                  "brc_distill_rec_t.p offset mismatch");
BRC_STATIC_ASSERT(offsetof(brc_distill_rec_t, w) == 84,
                  "brc_distill_rec_t.w offset mismatch");
BRC_STATIC_ASSERT(offsetof(brc_distill_rec_t, valid) == 88,
                  "brc_distill_rec_t.valid offset mismatch");
BRC_STATIC_ASSERT(offsetof(brc_distill_rec_t, soft) == 92,
                  "brc_distill_rec_t.soft offset mismatch");
BRC_STATIC_ASSERT(offsetof(brc_distill_rec_t, hold) == 96,
                  "brc_distill_rec_t.hold offset mismatch");

BRC_STATIC_ASSERT(sizeof(brc_distill_rec_nohot_t) == 96,
                  "brc_distill_rec_nohot_t must be 96 bytes (distill.cu NETH_NET_HOTBAR=0)");
BRC_STATIC_ASSERT(offsetof(brc_distill_rec_nohot_t, a) == 0,
                  "brc_distill_rec_nohot_t.a offset mismatch");
BRC_STATIC_ASSERT(offsetof(brc_distill_rec_nohot_t, p) == 64,
                  "brc_distill_rec_nohot_t.p offset mismatch");
BRC_STATIC_ASSERT(offsetof(brc_distill_rec_nohot_t, w) == 80,
                  "brc_distill_rec_nohot_t.w offset mismatch");
BRC_STATIC_ASSERT(offsetof(brc_distill_rec_nohot_t, valid) == 84,
                  "brc_distill_rec_nohot_t.valid offset mismatch");
BRC_STATIC_ASSERT(offsetof(brc_distill_rec_nohot_t, soft) == 88,
                  "brc_distill_rec_nohot_t.soft offset mismatch");
BRC_STATIC_ASSERT(offsetof(brc_distill_rec_nohot_t, hold) == 92,
                  "brc_distill_rec_nohot_t.hold offset mismatch");

/* Emit a distill.cu-compatible label record (NETH_NET_HOTBAR=1, 100 bytes).
 * prev_act may be NULL on step 0 (defaults to pitch=20, yaw=20, keys_mask=0). */
void br_emit_distill_rec(brc_distill_rec_t *rec,
                         const brc_s1_action_t *act,
                         const brc_s1_action_t *prev_act,
                         int32_t prev_macro_kind,
                         float weight,
                         int32_t valid,
                         int32_t soft,
                         int32_t hold);

/* Emit a distill.cu-compatible label record (NETH_NET_HOTBAR=0, 96 bytes). */
void br_emit_distill_rec_nohot(brc_distill_rec_nohot_t *rec,
                               const brc_s1_action_t *act,
                               const brc_s1_action_t *prev_act,
                               int32_t prev_macro_kind,
                               float weight,
                               int32_t valid,
                               int32_t soft,
                               int32_t hold);

/* ============================================================================
 * 4. BRS1 Seed Pack Loader
 * ============================================================================ */

/* Load a BRS1 seed pack from disk. Caller frees *out_seeds with free().
 * Returns 0 on success, -1 on error. */
int br_load_seed_pack(const char *path,
                      brc_seed_entry_t **out_seeds,
                      uint32_t *out_count,
                      char *err,
                      size_t err_sz);

#ifdef __cplusplus
}
#endif

#endif /* NETHERITE_BEDROCK_OBS_BRIDGE_H */
