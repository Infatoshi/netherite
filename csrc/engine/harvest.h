/* Block breaking speed and harvestability, vanilla 1.7.10: Block.getBlockHardness,
 * Block.getPlayerRelativeBlockHardness, EntityPlayer.canHarvestBlock,
 * EntityPlayer.getCurrentPlayerStrVsBlock (func_146096_a), InventoryPlayer's two
 * lookups, and the item func_150893_a / func_150897_b tables of
 * ItemTool/ItemPickaxe/ItemAxe/ItemSpade/ItemShears/ItemSword.
 *
 * The tool tables live in the generated item registry (items.h) only as far as
 * their material and efficiency go; which blocks each tool class is fast on and
 * which it can harvest is a per-class block set, so it is spelled out here the
 * way the vanilla classes spell it. Block ids named in the sets use the
 * registry's own names through block_id(). */
#ifndef NETHERITE_HARVEST_H
#define NETHERITE_HARVEST_H

#include <stdint.h>

#include "blocks.h"
#include "items.h"

/* What the player carries into one hardness query: the held stack, the ground
 * flag and the two potion amplifiers (0 = the effect is not active). in_water
 * is EntityPlayer.isInsideOfMaterial(water), which only the dig path reaches
 * (the HarvestProbe's reference stands in air); both it and the ground flag
 * divide the speed by 5. */
struct harvest_player {
    int held_item;      /* Item.getIdFromItem(getCurrentItem()), 0 = empty */
    int held_enchant;   /* EnchantmentHelper.getEfficiencyModifier: Efficiency level, 0 = none */
    int haste;          /* Potion.digSpeed amplifier + 1, 0 = not active */
    int fatigue;        /* Potion.digSlowdown amplifier + 1, 0 = not active */
    int on_ground;
    int in_water;
};

/* The registry id of a 1.7.10 block by its registry name, for the tool tables
 * below. Returns -1 when no block holds that name. */
int harvest_block_id(const char *name);

/* Block.getBlockHardness(world, x, y, z) for the generated registry: the
 * stored blockHardness, which no 1.7.10 block overrides. */
float block_hardness(int block_id);

/* Item.func_150893_a(stack, block): the speed multiplier of the held stack
 * (1.0 for a plain item, the tool material's efficiency on the blocks its class
 * is proper on). */
float item_str_vs_block(int item_id, int block_id);

/* Item.func_150897_b(block): the held stack can harvest the block. */
int item_can_harvest(int item_id, int block_id);

/* InventoryPlayer.func_146023_a(block): 1.0 with an empty hand, else the held
 * stack's func_150893_a. */
float inventory_str_vs_block(const struct harvest_player *p, int block_id);

/* InventoryPlayer.func_146025_b(block) = EntityPlayer.canHarvestBlock: true for
 * a material that needs no tool, else the held stack's func_150897_b. */
int can_harvest_block(const struct harvest_player *p, int block_id);

/* EntityPlayer.getCurrentPlayerStrVsBlock(block, false), the argument
 * getPlayerRelativeBlockHardness passes: the inventory base speed, the
 * Efficiency bonus, Haste and Mining Fatigue, then /5 when the player is not on
 * the ground. The /5 for standing in water is not ported; the oracle's
 * reference never enters it (see out/java/harvest's manifest). */
float player_str_vs_block(const struct harvest_player *p, int block_id);

/* Block.getPlayerRelativeBlockHardness(player, world, x, y, z): 0 for an
 * unbreakable block, else the speed over the hardness over 100, or over 30 when
 * the player can harvest it. The one branch that reads the world is the
 * hardness, which no block overrides, so the world position does not matter. */
float player_relative_block_hardness(const struct harvest_player *p, int block_id);

#endif