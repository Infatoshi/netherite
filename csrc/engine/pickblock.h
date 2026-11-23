/* Pick block: the block half of Minecraft.func_147112_ai (Block.getItem and
 * getDamageValue per block, with the ItemBlock's own block answering the
 * damage), which survival.c's middle-click consumer turns into a hotbar
 * selection (InventoryPlayer.func_146030_a). */
#ifndef NETHERITE_PICKBLOCK_H
#define NETHERITE_PICKBLOCK_H

struct world;

/* The item func_147112_ai looks for at the block (x, y, z): 1 with *item,
 * *damage and *has_subtypes (Item.getHasSubtypes) set, 0 when the block is
 * air or its getItem is null. */
int pick_block_item(struct world *w, int x, int y, int z, int *item, int *damage, int *has_subtypes);

#endif
