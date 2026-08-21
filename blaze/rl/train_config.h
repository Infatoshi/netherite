/* train_config.h - flat key=value config for the native C11 trainer.
 *
 * Load order: compiled defaults -> --conf PATH (optional) -> --set key=value.
 * Unknown keys and bad values fail. No environment variables.
 */
#pragma once

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TR_CFG_STR_MAX 1024
#define TR_CFG_BACKEND_MAX 16

typedef struct TrainConfig {
  char backend[TR_CFG_BACKEND_MAX];
  int device;
  int n_envs;
  char fixture[TR_CFG_STR_MAX];
  int rollout_steps;
  int action_repeat;
  float lr;
  float ppo_clip;
  float value_coef;
  float entropy_coef;
  float grad_limit;
  float gamma;
  float lam;
  int epochs;
  int mb; /* 0 = one update of the full n*T batch */
  int max_chunks;
  int64_t max_ticks; /* 0 = no tick cap */
  float max_wall;    /* seconds; 0 = no wall cap */
  int success_item;  /* 0 = never (trainer-side term only) */
  float t0_share;
  int cap_refresh;
  char train_seeds[TR_CFG_STR_MAX]; /* "fixture" or "2,3,10" */
  char snaps_dir[TR_CFG_STR_MAX];
  int stage_snaps; /* 0/1: pre-seed curriculum from s{seed}_stg{k}.bsnp */
  float lr_floor;
  int64_t lr_decay_ticks;
  int ep_dec;
  int64_t ckpt_ticks;
  uint64_t seed;
  char checkpoint[TR_CFG_STR_MAX];
  char init_from[TR_CFG_STR_MAX]; /* empty = off; schema-1 .bin warm-start */
  /* Metal observation (used when backend=metal; present on every platform) */
  int metal_max_cells;
  char metallib[TR_CFG_STR_MAX]; /* "auto" = owned default path */
  /* blaze_create opts (0/1 bools except warp_tick) */
  int ktime;
  int stage_time;
  int legacy_recenter;
  int warp_tick;
  int op_trace;
  int no_ore_xy;
} TrainConfig;

/* Compiled defaults. */
void tr_cfg_defaults(TrainConfig *c);

/* Set one key. Returns 0 ok, -1 unknown key, -2 bad value. */
int tr_cfg_set(TrainConfig *c, const char *key, const char *val);

/* Apply a conf file on top of *c. Explicit path is required: a missing file
 * fails (returns -3). Unknown key or bad value returns -1 / -2 and leaves
 * *c partially applied. path NULL or empty returns -3. */
int tr_cfg_load_file(TrainConfig *c, const char *path);

/* Parse argv. Compiled defaults apply only when --conf is absent.
 * An explicit --conf PATH must name an existing file (missing fails).
 * --conf rejects a missing path and option tokens as the path.
 * On success returns 0 (defaults + optional conf + --set).
 * If --dump-config is present after a valid parse, dumps and returns 1.
 * Returns -1 / -2 / -3 on usage / bad value / missing conf (stderr message). */
int tr_cfg_parse_argv(TrainConfig *c, int argc, char **argv);

/* Print every key and its effective value. */
void tr_cfg_dump(const TrainConfig *c, FILE *out);

#ifdef __cplusplus
}
#endif
