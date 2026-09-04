#ifndef BLAZE_EVAL_CONFIG_H
#define BLAZE_EVAL_CONFIG_H
#include <stdint.h>
#include <stdio.h>
#include "obs_config.h"
enum {
  EVAL_STR_MAX = 1024,
  EVAL_BACKEND_MAX = 16,
  EVAL_MAX_SEEDS = 32,
  EVAL_MAX_TRIES = 16,
  EVAL_TORCH_ITEM = 50,
  EVAL_STAGE_ALL = -1,
  EVAL_STAGE_MAX = 4,
  EVAL_XFER_CLOSED = 0,
  EVAL_XFER_REPLAY = 1
};

typedef struct EvalCfg {
  char backend[EVAL_BACKEND_MAX];
  int device;
  char checkpoint[EVAL_STR_MAX];
  char snaps_dir[EVAL_STR_MAX];
  int seeds[EVAL_MAX_SEEDS];
  int nseeds;
  int tries;
  int ep_ticks;
  int action_repeat;
  int success_item;
  uint64_t seed;
  int metal_max_cells;
  char metallib[EVAL_STR_MAX];
  int ktime;
  int stage_time;
  int legacy_recenter;
  int warp_tick;
  int op_trace;
  int no_ore_xy;
  int stack_kib; /* CUDA per-thread stack limit, KiB (default 128) */
  int stage; /* 0..4, or EVAL_STAGE_ALL */
  int transfer; /* EVAL_XFER_CLOSED | EVAL_XFER_REPLAY */
  char magma_bin[EVAL_STR_MAX];
  char fixture[EVAL_STR_MAX];
  char report[EVAL_STR_MAX];
  int world_size, episode_decisions, deterministic, allow_missing;
  PolicyIoConfig policy;
} EvalCfg;

void eval_cfg_defaults(EvalCfg *c);
int eval_cfg_set(EvalCfg *c, const char *key, const char *value);
int eval_cfg_load(EvalCfg *c, const char *path, char *err, size_t cap);
int eval_cfg_validate(EvalCfg *c, char *err, size_t cap);
void eval_cfg_dump(const EvalCfg *c, FILE *out);
int eval_cfg_parse_argv(EvalCfg *c, int argc, char **argv, int *dump);
void eval_usage(void);
#endif
