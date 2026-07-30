#ifndef MAGMA_GAME_RL_MODE_H
#define MAGMA_GAME_RL_MODE_H

#include "game/config.h"

/* Interactive RL step loop (--rl): one JSON action per stdin line -> one
 * gm_runtime_tick -> one JSON obs line on stdout. Reward and featurization
 * live on the Python side. Returns process exit code. */
int gm_rl_run(const GmConfig *cfg);

#endif
