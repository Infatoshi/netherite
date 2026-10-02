/* BlockFire: the callbacks' placement tests and the fire spread (fire.c). The
 * tick dispatch (randomtick.c) and the scheduled dispatch (servertick.c) call
 * fire_update_tick; blockcb.c's onBlockAdded/onNeighborBlockChange cases read
 * fire_can_place. */
#ifndef NETHERITE_FIRE_H
#define NETHERITE_FIRE_H

#include "jrand.h"
#include "world.h"

/* Blocks.fire. */
enum { FIRE_BLOCK = 51 };

/* World.isRaining() (rainingStrength > 0.2, as 0/1) and
 * difficultySetting.getDifficultyId(): the two tick scalars the spread reads.
 * The server tick sets them once per tick, after the weather update. */
void fire_set_weather(int raining, int difficulty);

/* World.canLightningStrikeAt over the tick's fire_set_weather rain flag
 * (BlockFarmland.updateTick reads it too). */
int fire_can_lightning_strike_at(struct world *w, int x, int y, int z);

/* BlockFire.canPlaceBlockAt: a solid top surface below, or a flammable
 * neighbour. */
int fire_can_place(struct world *w, int x, int y, int z);

/* World.doesBlockHaveSolidTopSurface (BlockPortal.updateTick's floor search
 * reads it too). */
int fire_solid_top_surface(struct world *w, int x, int y, int z);

/* BlockFire.updateTick, with wr as World.rand. */
void fire_update_tick(struct world *w, jrand *wr, int x, int y, int z);

/* Blocks.tnt.onBlockDestroyedByPlayer(world, x, y, z, 1) after the spread
 * burns a TNT block: the caller constructs the primed entity (placed by
 * nobody). The server tick installs it for its own body; NULL skips it. */

#endif