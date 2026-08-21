/* Schema-1 weights-only checkpoint I/O.
 * Single reader for ppo.c and eval.c: nn_load -> nn_fixture_load (BNN1).
 * Do not parse the file in a second place. */
#pragma once

#include "nn.h"

static inline int rl_ckpt_load(Nn *nn, const char *path) {
  return nn_load(nn, path);
}

static inline int rl_ckpt_save(const Nn *nn, const char *path) {
  return nn_save(nn, path);
}
