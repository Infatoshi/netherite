#define _POSIX_C_SOURCE 200809L
#include "eval_config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
  EvalCfg a, b;
  char err[2048], path[] = "/tmp/netherite-eval-config-XXXXXX";
  int fd = mkstemp(path), dump;
  assert(fd >= 0);
  FILE *f = fdopen(fd, "w");
  assert(f);
  fputs("seeds = 10,11\nepisodes_per_seed = 2\nepisode_decisions = 3\naction_repeat = 2\nworld_size = 64\ndeterministic = 1\nobs_depth = 0\n", f);
  assert(fclose(f) == 0);
  char *argv[] = {"eval", "--set", "episode_decisions=4", "--conf", path,
                  "--set", "report=out/verify/eval-config.json", "--dump-config"};
  assert(eval_cfg_parse_argv(&a, 8, argv, &dump) == 0 && dump);
  assert(eval_cfg_validate(&a, err, sizeof err) == 0);
  assert(a.nseeds == 2 && a.tries == 2 && a.ep_ticks == 8 && a.deterministic && !a.policy.obs_depth);
  assert(eval_cfg_set(&a, "typo", "1") == -1);
  assert(eval_cfg_set(&a, "seeds", "10,,11") == -2);
  assert(eval_cfg_set(&a, "seeds", "10,") == -2);
  assert(eval_cfg_set(&a, "seeds", "10,11") == 0);
  assert(eval_cfg_set(&a, "obs_history", "3") == -2);
  assert(eval_cfg_set(&a, "episodes_per_seed", "0") == -2);
  assert(eval_cfg_set(&a, "world_size", "65") == -2);
  assert(eval_cfg_set(&a, "deterministic", "true") == -2);
  f = fopen(path, "w"); assert(f); eval_cfg_dump(&a, f); assert(fclose(f) == 0);
  eval_cfg_defaults(&b);
  assert(eval_cfg_load(&b, path, err, sizeof err) == 0);
  assert(eval_cfg_validate(&b, err, sizeof err) == 0);
  assert(a.ep_ticks == b.ep_ticks && a.nseeds == b.nseeds && a.tries == b.tries);
  assert(policy_io_fingerprint(&a.policy) == policy_io_fingerprint(&b.policy));
  assert(eval_cfg_set(&b, "seeds", "10,10") == 0);
  assert(eval_cfg_validate(&b, err, sizeof err) == -1);
  f = fopen(path, "w"); assert(f); fputs("unexpected=1\n", f); fclose(f);
  assert(eval_cfg_load(&a, path, err, sizeof err) == -1);
  f = fopen(path, "w"); assert(f); fputs("no equals sign\n", f); fclose(f);
  assert(eval_cfg_load(&a, path, err, sizeof err) == -1);
  eval_cfg_defaults(&a);
  assert(a.nseeds == 13 && a.tries == 5 && a.ep_ticks == 6000 && !a.allow_missing);
  assert(eval_cfg_set(&a, "fixture", "start.bsnp") == 0);
  assert(eval_cfg_validate(&a, err, sizeof err) == -1);
  assert(eval_cfg_set(&a, "heldout_seeds", "10") == 0);
  assert(eval_cfg_validate(&a, err, sizeof err) == 0);
  assert(unlink(path) == 0);
  puts("test_eval_config: PASS (strict files, overrides, aliases, policy, dump round trip, sample counts)");
  return 0;
}
