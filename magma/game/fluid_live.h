/* game/fluid_live.h - LIVE fluid flow for the streamed world (FLUID module).
 *
 * Vanilla 1.11.2 water/lava flow (BlockDynamicLiquid/BlockStaticLiquid) applied
 * to the live GmWorld using the verified blaze fluid CA (core/fluid_flow.h,
 * ported from the oracle). The CA applies one updateTick-equivalent to every
 * liquid cell in bounded active regions per scheduled step, at the vanilla
 * cadence (water every 5 game ticks; lava every 30 overworld / 10 nether).
 *
 * Activation: gm_fluid_mark() after any block change; it activates only when
 * the changed cell or a face neighbor is water/lava (ids 8..11). Up to
 * GM_FLUID_REGIONS disjoint regions run independently (e.g. a pond being dug
 * while a bucket column still falls elsewhere); a region deactivates at CA
 * fixpoint. Documented deviations from vanilla: cells update synchronously per
 * step instead of per-cell scheduled ticks (flow SHAPE and levels match;
 * multi-cell fronts advance in lockstep), and a mixed water+lava region steps
 * at the water cadence.
 *
 * ALLOCATE-ONCE: the CA grids live inside GmFluidLive; no allocation after init.
 */
#ifndef MAGMA_GAME_FLUID_LIVE_H
#define MAGMA_GAME_FLUID_LIVE_H

#include "game/game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Active-region grid bounds (world-box the CA runs over, incl. write margin). */
#define GM_FLUID_NX 64
#define GM_FLUID_NY 40
#define GM_FLUID_NZ 64
/* Cells this close to the grid boundary are simulated but never written back
 * (their neighborhood is truncated, so their values are not trustworthy). */
#define GM_FLUID_MARGIN 4
/* Independent simultaneously-active flow regions. */
#define GM_FLUID_REGIONS 4

typedef struct {
    int active;                       /* 1 while this region has pending flow */
    int x0, y0, z0, x1, y1, z1;       /* inclusive world AABB of dirty cells  */
    int has_water;                    /* 1 -> step every 5 ticks, else lava   */
    int quiet_steps;                  /* consecutive no-change steps          */
} GmFluidRegion;

typedef struct GmFluidLive {
    int dim;                          /* dimension the regions belong to      */
    GmFluidRegion reg[GM_FLUID_REGIONS];
    unsigned short cur[GM_FLUID_NX * GM_FLUID_NY * GM_FLUID_NZ];
    unsigned short tmp[GM_FLUID_NX * GM_FLUID_NY * GM_FLUID_NZ];
} GmFluidLive;

void gm_fluid_init(GmFluidLive *f);

/* 1 if any region is still flowing (test/introspection helper). */
int  gm_fluid_active(const GmFluidLive *f);

/* Call after ANY world block change at (wx,wy,wz). Activates/extends a region
 * iff the cell or a face neighbor holds water/lava. */
void gm_fluid_mark(GmFluidLive *f, GmWorld *w, int dim, int wx, int wy, int wz);

/* Call once per game tick. Runs one CA step per active region at the vanilla
 * cadence; writes changed cells back through gm_world_set_block_meta.
 * Returns the number of cells changed this tick (0 when idle). */
int gm_fluid_tick(GmFluidLive *f, GmWorld *w, int dim, long long world_time);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_FLUID_LIVE_H */
