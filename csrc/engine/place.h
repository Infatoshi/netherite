/* The placement port: ItemStack.tryPlaceItemIntoWorld and every onItemUse
 * that stores a block, exact against the oracle's PlaceProbe dump
 * (oracle/harness/netherite/oracle/PlaceProbe.java).
 *
 * The entry is place_try: the placer's pose, the stack, the target cell, the
 * side and the hit vector, and the stack mutated the way the item's onItemUse
 * does. The item dispatch is by the registry's item class (items.h); the
 * block rules (onBlockPlaced, onBlockPlacedBy, onPostBlockPlaced,
 * canPlaceBlockAt, canPlaceBlockOnSide) dispatch by the block registry's
 * class name, and the callbacks the stored block runs (onBlockAdded, the
 * neighbour notification) stay in blockcb.c.
 *
 * What is not here: entities. The probe world holds no entities and the
 * placer is not one of them, so every World.checkNoEntityCollision answers
 * yes and the collision box of the placed block is never consulted (a box is
 * only ever asked about entities; its null-ness never reaches an outcome
 * here). The drop paths a pure placement can fire are BlockReed's; they go
 * through place_block_drop against the streams place_set_case owns. */
#ifndef NETHERITE_PLACE_H
#define NETHERITE_PLACE_H

#include <stdint.h>

#include "det.h"
#include "world.h"

/* World.checkNoEntityCollision for canPlaceEntityOnSide: 1 when a live entity
 * with preventEntitySpawning set (a living, the player, a falling block, primed
 * TNT) has a bounding box meeting box. skip_placer: the placing player is the
 * excluded entity (ItemBlock.onItemUse passes it; func_150936_a and ItemReed
 * pass null). The replay installs the hook for its worlds; NULL (the probes)
 * means nothing is ever in the way. */
struct aabb;
typedef int (*place_entity_hook)(void *ctx, struct world *w, const struct aabb *box, int skip_placer);
void place_set_entity_hook(place_entity_hook hook, void *ctx);
place_entity_hook place_get_entity_hook(void **ctx);

/* The placer's pose: the EntityPlayerMP fields the placement paths read. */
struct placer {
    double px, py, pz;   /* posX, posY, posZ */
    float yaw, pitch;    /* rotationYaw, rotationPitch */
    double y_offset;     /* Entity.yOffset, func_150071_a reads it */
};

/* The placed stack: registry item id, size, damage. place_try mutates it the
 * way the item does (the size drops when a block is stored, the hoe's damage
 * grows by one). */
struct place_stack {
    int item, count, damage;
};

/* ItemStack.tryPlaceItemIntoWorld: the item's onItemUse return value. The
 * use statistic it adds on success moves no world state. */
int place_try(struct world *w, const struct placer *p, struct place_stack *stack,
              int x, int y, int z, int side, float hx, float hy, float hz);

/* What tryPlaceItemIntoWorld would answer, without the placement: 0 false,
 * 1 true with the stack size unchanged, 2 true with a block placed (the
 * stack shrinks), -1 when the item's onItemUse is not one of place_try's.
 * The client's func_147121_ag swing reads it; the world is not written. */
int place_would_use(struct world *w, const struct placer *p, const struct place_stack *stack,
                    int x, int y, int z, int side);

/* ItemBlock.func_150936_a (ItemSlab's override for the slabs): whether the
 * client's onPlayerRightClick sends the C08 for a held item; 1 for an item
 * that is no ItemBlock. The entity hook in place is the world's. */
int place_item_block_precheck(struct world *w, const struct place_stack *stack, int x, int y, int z, int side);

/* The per-case streams the self-drops draw from: the Det state (whose seeder
 * each spawned entity's rand and UUID spend), the world Random (a plain Random
 * the probe reseeds per case), the shared Math.random stream and the shared
 * Item.itemRand split. */
void place_set_case(int64_t case_seed, det_state *det, det_rng *math, det_split *item_rand);

/* The case Random's state after the last call, for the tape replay's live
 * World.rand continuation (place_set_case seeds it per case). */
uint64_t place_world_state(void);
/* the case's World.rand itself (the callbacks a placement sets off draw it) */
jrand *place_case_rand(void);

/* BlockLiquid.func_149799_m: the fizz's sound and particle draws. */
void place_lava_fizz(void);

/* World.doesBlockHaveSolidTopSurface. */
int place_solid_top_surface(struct world *w, int x, int y, int z);

/* BlockFire.func_149847_e: a flammable block in one of the six neighbours. */
int place_fire_flammable_near(struct world *w, int x, int y, int z);

/* BlockFire.onBlockAdded's scheduled tick delay: the world Random's
 * nextInt(10) part. */
int place_fire_tick_draw(void);

/* The dispenser tile entity's Det.newRandom(), one seeder long at creation. */
void place_dispenser_seeder(void);

/* Swap the case's det (the one place_dispenser_seeder spends on the OTHER
 * role) and hand the old one back: a caller that spends the dispenser draw
 * on its own role (dev.c's setblock, a server write) sets NULL around its
 * writes and restores the old one. */
det_state *place_case_det_swap(det_state *det);

/* BlockReed's dropBlockAsItem, the drop path a pure placement can fire. */
void place_block_drop(struct world *w, int x, int y, int z, int block, int meta);

/* BlockSkull.breakBlock: the skull item at the tile entity's type drops
 * directly through dropBlockAsItem_do against the case's world Random. */
void place_skull_break_drop(struct world *w, int x, int y, int z, int type);

/* One item entity for a caller that already knows the item and the damage. */
void place_item_drop(struct world *w, int x, int y, int z, int item, int damage);

/* The rails' onBlockAdded and onNeighborBlockChange, which blockcb.c
 * dispatches to; the rail-shape refresh lives with the placement port. */
void place_rail_on_added(struct world *w, int id, int x, int y, int z);
void place_rail_on_neighbor(struct world *w, int x, int y, int z, int source);

/* BlockTripWire.onBlockAdded, which blockcb.c dispatches to. */
void place_tripwire_on_added(struct world *w, int x, int y, int z);

/* BlockTorch.func_150107_m, the support a wall torch or the torch's own
 * onBlockAdded asks for. */
int place_torch_can_support(struct world *w, int x, int y, int z);

/* BlockTorch.canPlaceBlockAt and World.isBlockNormalCubeDefault, the torch's
 * neighbour-changed support checks. */
int place_torch_can_stay(struct world *w, int x, int y, int z);
int place_torch_wall_ok(struct world *w, int x, int y, int z);

/* BlockPortal.onNeighborBlockChange's frame scan: 1 when the portal block at
 * the cell survives (its meta gives the axis). */
int place_portal_frame_ok(struct world *w, int x, int y, int z, int meta);

#endif