/* BlockRedstoneComparator's container input, which config.yaml redstone = off
 * leaves live: func_149903_h reads the block behind the comparator (or the
 * one behind a normal cube there) through getComparatorInputOverride, not
 * through World's power queries (World.isBlockProvidingPowerTo and
 * getIndirectPowerLevelTo answer 0), so a comparator behind a filled chest,
 * furnace, dispenser, dropper, hopper, brewing stand, cauldron, jukebox or
 * end portal frame stores an output signal and sets its powered bit. And
 * World.func_147453_f, the call a container's change makes to wake the
 * comparators around it. */
#ifndef NETHERITE_COMPARATOR_H
#define NETHERITE_COMPARATOR_H

struct world;
struct bwl_frame;

/* The unpowered (149) and powered (150) comparator. */
int comparator_is(int id);

/* Block.hasComparatorInputOverride. */
int comparator_has_override(int id);

/* Block.getComparatorInputOverride of the block at the position (0 for a
 * block without one). */
int comparator_override(struct world *w, int x, int y, int z);

/* World.func_147453_f(x, y, z, block): the comparator beside the position,
 * or one past a normal cube there, in each horizontal direction, gets its
 * onNeighborBlockChange. comparator_notify runs it from outside the block
 * worklist (TileEntity.onInventoryChanged, a furnace's smelt);
 * comparator_notify_emit from inside a worklist step (World.setBlock's
 * flag-1 tail, a container's breakBlock). */
void comparator_notify(struct world *w, int x, int y, int z, int block);
void comparator_notify_emit(struct world *w, int x, int y, int z, int block);
int comparator_notify_step(struct bwl_frame *f);

/* BlockRedstoneComparator.func_149897_b, the tail of
 * BlockRedstoneDiode.onNeighborBlockChange once canBlockStay holds: a tick
 * two from now (priority -1 when another diode faces it from the front)
 * when the input no longer matches the stored output or the powered bit. */
void comparator_on_neighbor(struct world *w, int x, int y, int z, int id);

/* BlockRedstoneDiode.func_149900_a for a comparator placed by
 * onBlockPlacedBy at metadata meta: 1 when it should be powered (the caller
 * then schedules the one-tick update). */
int comparator_should_power(struct world *w, int x, int y, int z, int meta);

/* BlockRedstoneComparator.func_149970_j at metadata meta: the input, less
 * the side input in subtract mode. */
int comparator_strength(struct world *w, int x, int y, int z, int meta);

/* BlockRedstoneComparator.func_149972_c for the block id that ran it (its
 * field_149914_a): the output stored in the TileEntityComparator, the
 * powered bit written (flag 2) and func_149911_e's sweep when either moved,
 * or in compare mode. */
void comparator_refresh(struct world *w, int x, int y, int z, int id);

/* BlockRedstoneComparator.updateTick: the powered form turns back into the
 * unpowered one (flag 4) first, then func_149972_c. */
void comparator_update_tick(struct world *w, int x, int y, int z, int id);

#endif
