/* Block callbacks World.setBlock runs besides storing the block.
 *
 * Chunk.func_150807_a calls Block.onBlockAdded on every block it actually
 * stores; World.setBlock calls notifyBlockChange for a flag-1 write, which
 * calls Block.onNeighborBlockChange on each of the six neighbors with the
 * replaced block as the argument. Those two hooks are the whole reason a shape
 * scatter can leave a different world than the same setBlock calls with the
 * callbacks dropped: lava next to water turns into obsidian or cobblestone,
 * and the notification that conversion sends can make a trapdoor, a vine or a
 * snow layer drop itself.
 *
 * Only the bodies the collision probes reach are ported, plus the liquids,
 * which the terrain holds anywhere. What each class does is named in
 * blockcb.c; block classes with the inherited empty body are not listed. */
#ifndef NETHERITE_BLOCKCB_H
#define NETHERITE_BLOCKCB_H

#include <stdint.h>

#include "det.h"
#include "jrand.h"

struct world;

/* Environment for the callbacks that construct an entity or draw a random:
 * the torch (and rail) support test can drop the block, which spawns an item
 * entity (an EntityItem's four Det.mathRandom draws plus Det's entity id, rand
 * and UUID) and draws from World.rand (the chance and the position jitter).
 * A structure block half installs one for its run and puts the caller's back
 * after; NULL members keep those bodies from drawing. */
struct spill_item;

struct blockcb_env {
    jrand *world_rand;   /* World.rand, loaded in det_rng_state form */
    det_state *det;      /* the streams the Math.random draws come from */
    int role;            /* which of det's roles those draws mark; OTHER by
                          * default, the world-build lane runs as SERVER */
    /* one item drop, in construction order */
    void (*item_drop)(void *ctx, int entity_id, uint64_t rand_state, int64_t uuid_msb, int64_t uuid_lsb,
                      double x, double y, double z, float yaw, float hover,
                      double motion_x, double motion_z, int item, int damage, int count);
    /* rand_state is the 48-bit state of the EntityItem's own Random, the
     * Det.newRandom the Entity constructor made. */
    void *ctx;
    /* a container's breakBlock spill (spill.h), into the same ctx, its Det
     * draws on spill_role; NULL where no write can replace a container, and
     * then block_on_broken spills nothing */
    void (*item_spill)(void *ctx, const struct spill_item *e);
    int spill_role;
};

/* One Block.dropBlockAsItem_do item into the installed environment's
 * item_drop: the EntityItem's Det draws on its role, then the sink. The
 * shape of a randomtick drop sink, so a drop outside the tick (bone meal on
 * a double plant) reaches the same entity list. */
void blockcb_env_drop_sink(void *ctx, double x, double y, double z, int item, int damage, int count);

/* The callbacks as worklist steps (blockwl.h): each runs the callback's body
 * and emits the nested calls it makes, so only the worklist calls them (the
 * SET_BLOCK and NOTIFY steps in world.c).
 *
 * Block.onBlockAdded. */
void bwl_on_added(struct world *w, int id, int meta, int x, int y, int z);

/* Block.onNeighborBlockChange, one neighbour of a flag-1 write. source is the
 * block that write replaced, the argument World.setBlock passes through
 * notifyBlockChange. */
void bwl_on_neighbor(struct world *w, int id, int meta, int x, int y, int z, int source);

/* Block.breakBlock, run by Chunk.func_150807_a for the block a write replaced
 * (with the new id already stored but the old metadata still in place). */
void bwl_on_broken(struct world *w, int x, int y, int z, int old_id);

/* Block.dropBlockAsItem from outside the callbacks (World.func_147480_a's
 * drop), routed as a callback's pop: the tick's sink or the block env's. */
void blockcb_break_drop(struct world *w, int x, int y, int z, int id, int meta);

/* World.func_147460_e from outside the callbacks: onNeighborBlockChange of
 * the block id with metadata meta at the position, and all it triggers. */
void block_on_neighbor_changed(struct world *w, int id, int meta, int x, int y, int z, int source);

/* BlockTorch.updateTick: the scheduled tick on a torch (or a redstone torch,
 * whose state machine writes nothing in a redstone-off world). */
void block_torch_update_tick(struct world *w, int x, int y, int z);

/* BlockRedstoneDiode.func_149911_e: the faced neighbour's onNeighborBlockChange
 * and that neighbour's other five, source the diode's own block id. */
void blockcb_diode_notify(struct world *w, int x, int y, int z, int source);

/* BlockButton.func_150042_a: the button's own six neighbours, then the
 * attached face's block, source the button's block id. */
void blockcb_button_notify(struct world *w, int x, int y, int z, int facing, int id);

/* BlockBush.func_149854_a through canPlaceBlockAt, the placement's soil test. */
int blockcb_bush_floor_ok(int id, struct world *w, int x, int y, int z);

/* Block.canPlaceBlockAt's stay check (the bushes, the cactus). */
int blockcb_can_stay(int id, struct world *w, int x, int y, int z);

/* BlockTripWire.func_150138_a: the wire's two scan directions and the hook
 * each finds facing back, told the wire's new metadata. */
void blockcb_tripwire_hooks(struct world *w, int x, int y, int z, int meta);

/* BlockTripWireHook.updateTick: func_150136_a(false, meta, true, -1, 0). */
void blockcb_tripwire_hook_tick(struct world *w, int x, int y, int z);

#endif