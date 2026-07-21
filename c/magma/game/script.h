#ifndef MAGMA_GAME_SCRIPT_H
#define MAGMA_GAME_SCRIPT_H

#include "game/config.h"

/* Run the deterministic JSONL harness. Returns 0 success, 2 input/config error,
 * or 1 runtime/I/O failure. This path owns no alternate simulation loop. */
int gm_script_run(const GmConfig *cfg);

#endif
