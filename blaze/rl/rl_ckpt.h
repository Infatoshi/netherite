/* Schema-1 weights-only checkpoint I/O.
 * Single reader for ppo.c and eval.c: nn_load -> nn_fixture_load (BNN1).
 * Do not parse the file in a second place. */
#pragma once

#include "nn.h"
#include "obs_config.h"

static inline int rl_ckpt_load(Nn *nn, const char *path) {
  return nn_load(nn, path);
}

static inline int rl_ckpt_save(const Nn *nn, const char *path) {
  return nn_save(nn, path);
}

static inline int rl_ckpt_load_config(Nn *nn, const char *path,
                                     const PolicyIoConfig *c,
                                     char *err, size_t cap) {
  int rc = policy_io_checkpoint_check(path, c, err, cap);
  if (rc < 0) return -1;
  if (rc == 1)
    fprintf(stderr, "policy: warning: legacy checkpoint has no policy contract; assuming exact default observations/actions\n");
  rc = nn_load(nn, path);
  if (rc && err && cap) snprintf(err, cap, "checkpoint weights: %s", nn_last_error());
  return rc;
}

static inline int rl_ckpt_save_config(const Nn *nn, const char *path,
                                     const PolicyIoConfig *c,
                                     char *err, size_t cap) {
  if (policy_io_checkpoint_can_save(path, c, err, cap)) return -1;
  int rc = nn_save(nn, path);
  if (rc) {
    if (err && cap) snprintf(err, cap, "checkpoint weights: %s", nn_last_error());
    return rc;
  }
  return policy_io_checkpoint_write(path, c, err, cap);
}
