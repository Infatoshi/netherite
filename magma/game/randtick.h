/* game/randtick.h - live/window random block ticks (WorldServer.updateBlocks subset).
 *
 * LIVE/WINDOW path only. Tape replay must leave this off: the oracle world RNG is
 * unseedable and snapshots already carry the resulting terrain. Callers gate via
 * GmRuntime.randtick_enabled (script replay sets it 0; interactive/RL leave 1).
 *
 * Semantics:
 *   - randomTickSpeed N: N random cells per 16^3 section per tick (vanilla default 3)
 *   - randomTickSpeed 0: engine no-op
 *   - Position + behavior RNG: mc_hash_seed / mc_hash_bound (magma runtime convention)
 *   - Behaviors: BlockGrass, BlockLeaves decay, BlockFire (doFireTick-gated),
 *     BlockCrops family (wheat / carrots / potatoes). Beetroot deferred (max age 3).
 */
#ifndef MAGMA_GAME_RANDTICK_H
#define MAGMA_GAME_RANDTICK_H

#include "game/game.h"
#include "mc_gamerules.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One random-tick pass over every section of every chunk in the Chebyshev square
 * [ccx-radius .. ccx+radius] x [ccz-radius .. ccz+radius]. No-op when
 * gr->randomTickSpeed <= 0. */
void gm_randtick_pass(GmWorld *w, long long seed, long long tick,
                      int ccx, int ccz, int radius, const McGameRules *gr);

/* Force one block's updateTick at (wx,wy,wz). Used by unit tests for exact
 * outcomes without waiting for the sparse random schedule. */
void gm_randtick_block(GmWorld *w, int wx, int wy, int wz,
                       long long seed, long long tick, const McGameRules *gr);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_RANDTICK_H */
