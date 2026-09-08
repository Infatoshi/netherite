#include "eval_goal.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
  int inv[9] = {0}; EvalGoal g;
  eval_goal_init(&g, 270, inv);
  assert(g.initial_count == 0 && !eval_goal_observe(&g, inv, 0, 8));
  inv[8] = 1; /* Torch must not impersonate a wooden pickaxe. */
  assert(!eval_goal_observe(&g, inv, 1, 12));
  inv[5] = 1;
  assert(eval_goal_observe(&g, inv, 0, 16));
  assert(g.first_observed_tick == 16);
  inv[5] = 0; eval_goal_observe(&g, inv, 0, 20);
  assert(g.first_observed_tick == 16 && g.success);
  inv[5] = 1; eval_goal_init(&g, 270, inv);
  assert(g.initial_count == 1 && !eval_goal_observe(&g, inv, 0, 8));
  inv[5] = 0; assert(!eval_goal_observe(&g, inv, 0, 12));
  inv[5] = 1; assert(!eval_goal_observe(&g, inv, 0, 16));
  inv[5] = 2; assert(eval_goal_observe(&g, inv, 0, 20));
  inv[8] = 0; eval_goal_init(&g, 50, inv);
  assert(!eval_goal_observe(&g, inv, 0, 8));
  inv[8] = 1; assert(eval_goal_observe(&g, inv, 1, 12));
  eval_goal_init(&g, 0, inv);
  assert(!eval_goal_observe(&g, inv, 0, 8));
  const int ids[9] = {17,5,280,4,58,270,274,263,50};
  for (int i=0;i<9;++i) assert(eval_goal_index(ids[i]) == i);
  puts("eval_goal: empty, supplied, acquisition, torch independence and 9 inventory goals passed");
  return 0;
}
