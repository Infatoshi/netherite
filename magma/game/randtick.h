/* game/randtick.h - live/window random block ticks (WorldServer.updateBlocks subset).
 *
 * LIVE/WINDOW path only. Tape replay must leave this off: the oracle world RNG is
 * unseedable and snapshots already carry the resulting terrain. Callers gate via
 * GmRuntime.randtick_enabled (script replay sets it 0; interactive/RL leave 1).
 *
 * Semantics:
 *   - randomTickSpeed N: N random cells per 16^3 section per tick (vanilla default 3)
 *   - randomTickSpeed 0: engine no-op
 *   - Position: World.updateLCG (World.java:95-97). Behavior RNG: World.rand.
 *   - Behaviors: BlockGrass, BlockLeaves decay, BlockFire (doFireTick-gated),
 *     BlockCrops (wheat/carrots/potatoes), BlockSapling STAGE flip,
 *     BlockFarmland, BlockIce, BlockSnow, BlockMycelium. Tree gen and
 *     lightning stay out. Beetroot deferred (max age 3).
 */
#ifndef MAGMA_GAME_RANDTICK_H
#define MAGMA_GAME_RANDTICK_H

#include "game/game.h"
#include "mc_gamerules.h"
#include "mc_rng.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One random-tick pass over every section of every chunk in the Chebyshev square
 * [ccx-radius .. ccx+radius] x [ccz-radius .. ccz+radius], cx-outer cz-inner
 * (PlayerChunkMap.addPlayer insertion). No-op when gr->randomTickSpeed <= 0.
 * raining/thundering gate the thunder nextInt(100000); iceandsnow nextInt(16)
 * always consumes. */
void gm_randtick_pass(GmWorld *w, JavaRandom *world_rand, i32 *update_lcg,
                      int raining, int thundering,
                      int ccx, int ccz, int radius, const McGameRules *gr);

/* Force one block's updateTick at (wx,wy,wz). Used by unit tests for exact
 * outcomes without waiting for the sparse random schedule. */
void gm_randtick_block(GmWorld *w, int wx, int wy, int wz,
                       JavaRandom *world_rand, const McGameRules *gr);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_RANDTICK_H */
