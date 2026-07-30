/* mc_gamerules.h - vanilla GameRules runtime state (world/GameRules.java).
 *
 * CONTRACT HEADER (P0, PORT_MATRIX): the sim-affecting subset of the 22 string-backed
 * rules, as a plain struct so CPU and CUDA read the same bytes. Vanilla stores strings;
 * the sim only ever consumes them via boolean/int getters, so ints are faithful.
 * Defaults match GameRules.java's constructor addGameRule calls exactly.
 *
 * Consumers wire-up and the per-rule unit tests live with the P0 gamerules round;
 * the tick driver (scheduled ticks / tick order) takes `const McGameRules *` and must
 * not mutate it. */
#ifndef MC_GAMERULES_H
#define MC_GAMERULES_H

#include "mc.h"

typedef struct McGameRules {
    int doDaylightCycle;     /* default 1: worldTime advances each tick            */
    int doMobSpawning;       /* default 1: WorldEntitySpawner runs                 */
    int doFireTick;          /* default 1: BlockFire.updateTick acts               */
    int randomTickSpeed;     /* default 3: random block ticks per section per tick */
    int mobGriefing;         /* default 1: creeper/enderman/dragon block damage    */
    int keepInventory;       /* default 0: drop inventory on death                 */
    int doTileDrops;         /* default 1: blocks drop items                       */
    int naturalRegeneration; /* default 1: peaceful/food health regen              */
    int doWeatherCycle;      /* default 1: rain/thunder timers advance             */
    int maxEntityCramming;   /* default 24: cramming damage threshold              */
} McGameRules;

MC_HD static inline McGameRules mc_gamerules_default(void) {
    McGameRules g;
    g.doDaylightCycle     = 1;
    g.doMobSpawning       = 1;
    g.doFireTick          = 1;
    g.randomTickSpeed     = 3;
    g.mobGriefing         = 1;
    g.keepInventory       = 0;
    g.doTileDrops         = 1;
    g.naturalRegeneration = 1;
    g.doWeatherCycle      = 1;
    g.maxEntityCramming   = 24;
    return g;
}

#endif /* MC_GAMERULES_H */
