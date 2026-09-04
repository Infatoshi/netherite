/* Fixed-shape policy input/output contract shared by training and evaluation.
 * The network remains 18x36x64 planes, 27 scalars, and nine action heads.
 * pixel_stride performs coarse nearest-sample expansion, not resolution change.
 * RGB inputs, recurrent state and other network shapes are unsupported. */
#ifndef BLAZE_OBS_CONFIG_H
#define BLAZE_OBS_CONFIG_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct PolicyIoConfig {
    int obs_history;             /* 1 duplicates current; 2 uses prior/current */
    int obs_semantic_mask;       /* bits: log, leaves, coal, stone, dirt, table, occupied */
    int obs_depth, obs_edges;
    int obs_base_scalars;        /* existing scalars 0..5 */
    int obs_inventory;           /* counts, container flag, held item: 6..24 */
    int obs_pose;                /* existing normalized height: 25 */
    int obs_clock;               /* episode decision fraction: 26 */
    int obs_pixel_stride;        /* 1, 2, 4; tensor remains 64x36 */
    double action_yaw_degrees, action_pitch_degrees;
    int action_heads;            /* yaw,pitch,forward,jump,attack,use,craft,interact,hotbar */
} PolicyIoConfig;

void policy_io_default(PolicyIoConfig *c);
/* 0 accepted; 1 unknown key (caller must reject or dispatch elsewhere);
 * -1 invalid value. On failure the configuration is unchanged. */
int policy_io_set(PolicyIoConfig *c, const char *key, const char *value,
                  char *err, size_t cap);
int policy_io_validate(const PolicyIoConfig *c, char *err, size_t cap);
void policy_io_dump(const PolicyIoConfig *c, FILE *out);
uint64_t policy_io_fingerprint(const PolicyIoConfig *c);
int policy_io_is_default(const PolicyIoConfig *c);

/* Sidecar is CHECKPOINT.policy.conf. Check before nn_load:
 * 0 compatible, 1 absent but default legacy input/output allowed (warn user),
 * -1 incompatible, malformed, or nondefault configuration without metadata. */
int policy_io_checkpoint_check(const char *path, const PolicyIoConfig *c,
                               char *err, size_t cap);
/* Validate an intended save before touching weights. Existing weights with
 * no contract may only be overwritten under the exact default contract. */
int policy_io_checkpoint_can_save(const char *path, const PolicyIoConfig *c,
                                  char *err, size_t cap);
/* Publish complete metadata atomically, never replace an incompatible or
 * malformed existing sidecar. Concurrent identical publishers are harmless. */
int policy_io_checkpoint_write(const char *path, const PolicyIoConfig *c,
                               char *err, size_t cap);
#endif
