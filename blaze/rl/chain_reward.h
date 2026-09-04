/* Spawn-to-torch chain reward + GAE. Port of blaze/env/reward_chain.py.
 * Host scalar loops. No heap in cr_step / cr_gae. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  CR_IX_LOG = 0,
  CR_IX_PLANK = 1,
  CR_IX_STICK = 2,
  CR_IX_COBBLE = 3,
  CR_IX_TABLE = 4,
  CR_IX_WPICK = 5,
  CR_IX_SPICK = 6,
  CR_IX_COAL = 7,
  CR_IX_TORCH = 8,
  CR_N_INV = 9,
  CR_ST_HELD = 10,
  CR_ST_CONT = 11,
  CR_ST_DIGP = 12,
  CR_CAM_W = 64,
  CR_CAM_H = 36,
  CR_CX = 32,
  CR_CY = 18,
  CR_NPIX = CR_CAM_W * CR_CAM_H
};

typedef struct CrSpec {
  float shaping_scale; /* Dense shaping only; milestones and penalties stay unscaled. */
  float time_cost;
  float death_penalty;
  float w_log_per;
  float log_clamp;
  float w_plank_first;
  float w_stick_first;
  float w_table_first;
  float w_container_open;
  float w_wpick_first;
  float w_cobble_per;
  float cobble_clamp;
  float w_spick_first;
  float w_coal_first;
  float w_torch_first;
  float chop_dist_coef;
  float chop_dist_clamp;
  float chop_crosshair;
  float dig_descend_coef;
  float dig_stone_atk;
  float dig_hold_pick;
  float digprog_coef;
  float coal_dist_coef;
  float coal_dist_clamp;
  float coal_crosshair;
  float coal_crosshair_maxd;
  float coal_hold_pick;
  float coal_chew;
  float hunt_desc;
  float w_furnace_first;
  float w_furnace_open;
  float w_ironore_per;
  float ironore_clamp;
  float w_ingot_first;
  float w_ipick_first;
} CrSpec;

void cr_spec_default(CrSpec *s);
/* Full reward.<field> keys. 0 success, -1 unknown key, -2 invalid value.
 * Every coefficient is a finite nonnegative magnitude, at most 1e6. */
int cr_spec_set(CrSpec *s, const char *key, const char *value);
int cr_spec_validate(const CrSpec *s);
void cr_spec_dump(FILE *f, const CrSpec *s);

typedef struct CrState {
  int n;
  int *best;        /* [n][9] */
  uint8_t *flag_cont;
  float *prev_logd;
  float *prev_coald;
  float *prev_y;
  float *prev_digp;
  float *min_y;
  int *best_iron;   /* [n][4] */
  uint8_t *flag_furn;
  int iron_on;
  CrSpec spec;
} CrState;

int cr_state_init(CrState *st, int n, const CrSpec *spec);
void cr_state_free(CrState *st);
/* Update weights without resetting inventory high-water marks. */
int cr_state_set_spec(CrState *st, const CrSpec *spec);
void cr_reset_lane(CrState *st, int i);
void cr_reset_mask(CrState *st, const uint8_t *mask, int n);
/* Copy inventory/flags from a 17-int status row so the next cr_step does
 * not pay first-time bonuses for items already held at reset. */
void cr_seed_lane(CrState *st, int i, const int *status);

/* status [n*17], cam [n*NPIX] u16, acts [n*9] i32, pose [n*5],
 * scal [n*6], done [n] u8, lane_seed [n], logs [nseeds*lmax*3] pad 1e9.
 * writes r [n]. */
void cr_step(CrState *st, const int *status, const unsigned short *cam,
             const int32_t *acts, const float *pose, const float *scal,
             const unsigned char *done, const int *lane_seed,
             const float *logs, int nseeds, int lmax, float *r);

int cr_logs_from_bsnp(const char *path, float *xyz, int cap, int *n_out);

/* GAE. rew/term/cut/val/cut_val are T*N row-major (t, then env).
 * next_val[N] is the post-rollout value for continuing lanes. For each
 * cut&&!term row, cut_val holds V(final_observation) evaluated BEFORE reset.
 * All other cut_val entries are ignored. Cuts stop advantage propagation;
 * only true terminals suppress the value bootstrap. */
void cr_gae(const float *rew, const unsigned char *term,
            const unsigned char *cut, const float *val, const float *next_val,
            const float *cut_val,
            float gamma, float lam, int T, int N, float *adv, float *ret);

#ifdef __cplusplus
}
#endif
