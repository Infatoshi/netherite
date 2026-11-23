/* The container blocks' breakBlock inventory spill, vanilla 1.7.10:
 * BlockChest, BlockFurnace, BlockDispenser (and BlockDropper, its subclass),
 * BlockHopper and BlockBrewingStand all empty their tile entity the same way,
 * each from its own Block singleton's Random (born at bootstrap from the
 * seeder, drawn by nothing but this spill). Chunk.func_150807_a runs
 * breakBlock for every write that replaces the block, so the spill belongs to
 * the dig, an explosion and any other block write alike. */
#ifndef NETHERITE_SPILL_H
#define NETHERITE_SPILL_H

#include <stdint.h>

#include "det.h"

struct world;

/* The block Randoms, one per Block singleton, in det.nbt's order: the key,
 * then the blocks under it. */
enum {
    BR_FURNACE, BR_LIT_FURNACE,        /* "furnace": BlockFurnace.field_149933_a */
    BR_CHEST, BR_TRAPPED_CHEST,        /* "chest": BlockChest.field_149955_b */
    BR_DISPENSER, BR_DROPPER,          /* "dispenser": BlockDispenser.field_149942_b */
    BR_HOPPER,                         /* "hopper": BlockHopper.field_149922_a */
    BR_BREWING_STAND,                  /* "brewing": BlockBrewingStand.field_149961_a */
    BR_N
};

/* The current states are the environment's (block_rand, env.h): the born
 * states until a snapshot load sets them. block_rand_born writes the born
 * states into r. */
void block_rand_born(det_rng *r);

/* Resets every block Random to its born state (a snapshot without det.nbt's
 * block keys was recorded before any spill). */
void block_rand_reset(void);

/* The det.nbt key and the first index of each group, for the loader. */
struct block_rand_group {
    const char *key;
    int first, n;
};
extern const struct block_rand_group block_rand_groups[5];

/* The block Random a block id's breakBlock spills from, -1 for none. */
int block_rand_index(int block);

/* One spilled EntityItem, all its construction draws spent: the position the
 * block Random picked, the constructor's Math.random boilerplate (hover and
 * yaw; its motion is overwritten), the Det identity, and the Gaussian motion.
 * delayBeforeCanPickup stays 0. */
struct spill_item {
    int entity_id;
    uint64_t rand_state;
    int64_t uuid_msb, uuid_lsb;
    double x, y, z, mx, my, mz;
    float yaw, hover;
    int item, damage, count;
    int tag;           /* the slot's tag, copied onto the entity's stack */
};

typedef void (*spill_sink)(void *ctx, const struct spill_item *e);

/* breakBlock's spill for the block `block` at (x, y, z), whose tile entity is
 * still in place: every non-empty slot in order, its stack emptied in place
 * as Java's stackSize -= n does. r is the Random to draw (NULL: the block's
 * own block_rand). The Det draws run on det's role `role`. Returns the number
 * of items spilled; a block that is not a container, or whose tile entity is
 * missing or of another kind, draws nothing. */
int container_spill(struct world *w, int x, int y, int z, int block, det_rng *r, det_state *det, int role,
                    spill_sink sink, void *ctx);

#endif
