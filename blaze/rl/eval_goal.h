/* Evaluation goals are independent of curriculum milestone reporting. */
#pragma once

typedef struct {
  int index, initial_count, success, first_observed_tick;
} EvalGoal;

static inline int eval_goal_index(int item) {
  const int ids[9] = {17, 5, 280, 4, 58, 270, 274, 263, 50};
  for (int i = 0; i < 9; ++i) if (ids[i] == item) return i;
  return -1;
}

/* Baseline is the first policy observation, after the documented noop burn-in.
 * An item already supplied by a curriculum snapshot is not an achievement. */
static inline void eval_goal_init(EvalGoal *g, int item, const int *counts) {
  g->index = eval_goal_index(item);
  g->initial_count = g->index >= 0 ? counts[g->index] : -1;
  g->success = 0;
  g->first_observed_tick = -1;
}

static inline int eval_goal_observe(EvalGoal *g, const int *counts,
                                     int env_done, int tick) {
  int acquired = g->index >= 0 ? counts[g->index] > g->initial_count
                               : env_done == 1;
  if (!g->success && acquired) {
    g->success = 1;
    g->first_observed_tick = tick;
  }
  return g->success;
}
