/* The player-dependent half of Minecraft 1.7.10 block harvesting, checked
 * against the oracle's HarvestDropsProbe dump: Block.harvestBlock and its seven
 * overrides (BlockDeadBush, BlockDoublePlant, BlockIce, BlockLeaves, BlockSnow,
 * BlockTallGrass, BlockVine), Block.canSilkHarvest / createStackedBlock with
 * their overrides, EnchantmentHelper.getSilkTouchModifier / getFortuneModifier,
 * the onBlockHarvested overrides, and ItemInWorldManager.tryHarvestBlock's
 * order in survival mode: removeBlock's onBlockHarvested, the world.setBlockToAir
 * that runs the removed block's breakBlock and then notifies the six neighbours,
 * onBlockDestroyedByPlayer, canHarvestBlock, the held stack's onBlockDestroyed
 * and its damage, and finally harvestBlock.
 *
 * One call is one break of one block. The world Random is seeded to the case's
 * opseed and the shared Math.random stream is advanced exactly as the EntityItem
 * constructor does, so the items, the experience, where each entity lands and
 * how it moves come out bit for bit. The block's own dropBlockAsItem call goes
 * through drops.c, whose whole per-block table is reused; the state is handed
 * over as a seed because that call is always the last thing a case draws from
 * the world Random (see harvestdrops.c).
 *
 * The world is modelled as the 3x3x3 cells around the block, which is all a
 * harvest override reads (BlockIce the cell below, BlockDoublePlant the cell
 * above or below, BlockLeaves and BlockLog the leaves in a one-wide cube). A
 * cell outside that cube is air. The one breakBlock that reaches further is
 * BlockTripWireHook's, a walk of up to 41 blocks along its facing; the probe
 * loads three chunks around the block so the walk meets nothing but air and
 * stone, and this port treats its result as no change.
 *
 * Creative mode is out of scope: the probe always breaks as a survival player,
 * and with capabilities.isCreativeMode false every onBlockHarvested override
 * but BlockDoublePlant's is a no-op.
 *
 * Player-dependent paths the probe does not reach are spelled the way vanilla
 * spells them and documented in out/java/harvestdrops/<name>/manifest.json.
 */
#ifndef NETHERITE_HARVESTDROPS_H
#define NETHERITE_HARVESTDROPS_H

#include <stdint.h>

#include "drops.h"
#include "jrand.h"

/* The 3x3x3 context. Index ((dx+1)*3 + (dy+1))*3 + (dz+1), offsets in -1..1. */
#define HD_CELLS 27

/* A break makes at most a few dozen entities; the same slack as drops.c. */
#define HD_MAX_ENTS DROPS_MAX_ENTS

struct hd_cell {
    int id, meta;
};

/* One break. `cells` is the context before it, `math` the shared Math.random
 * stream, `opseed` the world Random's seed for the break. */
struct hd_case {
    int x, y, z;                     /* the world position of the broken block */
    struct hd_cell cells[HD_CELLS];
    int held_item;                   /* Item.getIdFromItem(getCurrentItem()), 0 = empty */
    int held_damage;                 /* the held stack's itemDamage before */
    int held_count;                  /* the held stack's stackSize before */
    int silk;                        /* EnchantmentHelper.getSilkTouchModifier level */
    int fortune;                     /* EnchantmentHelper.getFortuneModifier */
    float exhaustion;                /* FoodStats.foodExhaustionLevel before */
    int64_t opseed;
    jrand *math;
};

struct hd_result {
    struct hd_cell cells[HD_CELLS];  /* the context after the break */
    int held_item;                   /* the hotbar slot after, 0 = empty */
    int held_damage;
    int held_count;
    float exhaustion;                /* after, before the 40.0F cap is applied elsewhere */
    int stat_mine;                   /* +1 when the mineBlock stat moved */
    int stat_use;                    /* +1 when the useItem stat moved */
    int stat_break;                  /* +1 when the breakItem stat moved */
    int nents;
    struct drop_ent ents[HD_MAX_ENTS];
    int bad;                         /* 1: a drop path this port does not have */
};

/* Runs one break. Returns 0, or -1 when the case hit a path this port lacks. */
int harvestdrops_break(const struct hd_case *cs, struct hd_result *out);

/* The stat a vanilla mineBlockStatArray[block] would hold: StatList only fills
 * the array for a block with an item whose enableStats is on, and Block.java
 * turns the flag off for these ids. */
int harvestdrops_has_mine_stat(int block_id);

/* Block.createStackedBlock and its overrides (the silk-touch stack), for the
 * world-backed harvest in dig.c. */
void harvestdrops_create_stacked_block(int block, int meta, int *item, int *count, int *damage);

#endif