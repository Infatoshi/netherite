/* The springs lane's features (csrc/engine/features_springs.c): WorldGenLiquids
 * and the liquid flow its immediate updateTick runs, one table row per row
 * this lane appended to the oracle's FEATURES table, with the same config
 * indices. */
#ifndef NETHERITE_FEATURES_SPRINGS_H
#define NETHERITE_FEATURES_SPRINGS_H

#include "features.h"

/* The rows for one of this lane's feature names, or NULL; *n gets their
 * number. */
const struct feature *features_springs_for(const char *name, int *n);

/* BlockDynamicLiquid.updateTick: the flow step WorldGenLiquids runs at once
 * and the scheduled-update path (ticks.c) runs for a parked tick. id is the
 * dynamic liquid, 8 or 10. */
void liquid_update_tick(struct world *w, int x, int y, int z, int id, jrand *r);

/* BlockStaticLiquid.updateTick: the lava fire spread a consumed pending tick
 * would run (the probe world never consumes one). id is the static liquid,
 * 9 or 11. */
void liquid_static_update_tick(struct world *w, int x, int y, int z, int id, jrand *r);

#endif