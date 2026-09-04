#include "train_recipe.h"
#include <stdio.h>
#include <string.h>

static int incompatible(const TrainConfig *a, const TrainConfig *b) {
  return strcmp(a->backend, b->backend) || a->device != b->device ||
      a->n_envs != b->n_envs || a->rollout_steps != b->rollout_steps ||
      a->mb != b->mb || strcmp(a->nn_prec, b->nn_prec) ||
      strcmp(a->tail_mb, b->tail_mb) || a->seed != b->seed ||
      policy_io_fingerprint(&a->policy_io) != policy_io_fingerprint(&b->policy_io) ||
      strcmp(a->init_from, b->init_from) || strcmp(a->eval_conf, b->eval_conf) ||
      strcmp(a->checkpoint, b->checkpoint) ||
      strcmp(a->eval_executable, b->eval_executable) ||
      strcmp(a->checkpoint_metric, b->checkpoint_metric);
}

int tr_recipe_load(const TrainConfig *base, TrainRecipe *r, char *err, size_t cap) {
  char files[TR_CFG_STR_MAX];
  char *next;
  memset(r, 0, sizeof *r);
  if (tr_cfg_validate(base, err, cap)) return -1;
  memcpy(files, base->phase_files, sizeof files);
  next = files;
  do {
    char *end = strchr(next, ',');
    char *trim;
    if (end) *end = 0;
    while (*next == ' ' || *next == '\t') next++;
    trim = next + strlen(next);
    while (trim > next && (trim[-1] == ' ' || trim[-1] == '\t')) *--trim = 0;
    if (r->count == TR_RECIPE_MAX_PHASES || (base->phase_files[0] && !next[0])) {
      snprintf(err, cap, "phase_files needs 1..%d nonempty paths", TR_RECIPE_MAX_PHASES);
      return -1;
    }
    TrainConfig *c = &r->phase[r->count];
    *c = *base;
    c->phase_files[0] = 0;
    if (next[0]) {
      if (tr_cfg_load_file(c, next)) {
        snprintf(err, cap, "cannot load phase %d: %s", r->count, next);
        return -1;
      }
      snprintf(r->source[r->count], TR_CFG_STR_MAX, "%s", next);
    }
    if (c->phase_files[0]) {
      snprintf(err, cap, "phase %d cannot include another phase_files schedule", r->count);
      return -1;
    }
    if (tr_cfg_validate(c, err, cap)) return -1;
    if (r->count && incompatible(&r->phase[0], c)) {
      snprintf(err, cap, "phase %d changes policy/allocation/seed/init/evaluation contract; use a separate run", r->count);
      return -1;
    }
    r->count++;
    if (!end) break;
    next = end + 1;
  } while (1);
  return 0;
}
