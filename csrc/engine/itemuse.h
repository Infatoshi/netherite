/* Item use: ItemInWorldManager.tryUseItem and activateBlockOrUseItem's item
 * half, and the onItemUse / onItemRightClick bodies of the items that change
 * the world (ItemBucket, ItemDye, ItemFlintAndSteel, ItemFireball,
 * ItemLilyPad, ItemGlassBottle, ItemEnderEye).
 *
 * The port is one function per Java method, in the same order, reading the
 * same fields, so a divergence can be found by reading the two side by side:
 *
 *   Item.getMovingObjectPositionFromPlayer  look_ray
 *   ItemBucket.onItemRightClick / func_150910_a / tryPlaceContainedLiquid
 *                                          bucket_rclick / fill_result /
 *                                          try_place_contained_liquid
 *   ItemDye.onItemUse / func_150919_a      dye_use / bonemeal (randomtick.c)
 *   ItemFlintAndSteel.onItemUse            flint_use
 *   ItemFireball.onItemUse                 fireball_use
 *   ItemLilyPad.onItemRightClick           lily_rclick
 *   ItemGlassBottle.onItemRightClick       bottle_rclick
 *   ItemEnderEye.onItemUse                 eye_use
 *   ItemInWorldManager.tryUseItem          itemuse_try_use_item
 *   ItemInWorldManager.activateBlockOrUseItem
 *                                          itemuse_activate_block_or_use_item
 *
 * What is deliberately not here, because the reference probe does not reach it
 * and another lane owns it:
 *
 *   - the inventory: a bucket or bottle filled from a stack of more than one
 *     hands the filled item back as *give (itemuse_try_use_item), which the
 *     caller adds to the inventory or drops (survival.c); a broken tool's
 *     drop is not here.
 *   - ItemEnderEye.onItemRightClick's stronghold search and EntityEnderEye.
 *     Only the end portal frame use (onItemUse) is carried.
 *   - Item.itemRand: the flint's and the fire charge's sound pitch and the end
 *     eye's smoke particles are client side and move no world state.
 *   - Block.onBlockActivated. No block a probe scene holds activates, so the
 *     item half always runs; a scene with a chest or a door would need it.
 *   - ItemStack.damageItem's Unbreaking roll: no stack here carries a tag, so
 *     EnchantmentHelper gives level 0 and the roll never happens.
 *
 * World.rand is the caller's stream (netherite.oracle.ItemUseProbe records its
 * 48-bit state after the use), so the draw sequence is checked, not just the
 * writes.
 */
#ifndef NETHERITE_ITEMUSE_H
#define NETHERITE_ITEMUSE_H

#include "jrand.h"
#include "det.h"
#include "world.h"

/* The player a use is made by: the fields getMovingObjectPositionFromPlayer,
 * canPlayerEdit and the item bodies read. yOffset is Entity.yOffset (0 for a
 * standing player), allow_edit is capabilities.allowEdit and creative is
 * capabilities.isCreativeMode. */
struct iu_player {
    double pos_x, pos_y, pos_z;
    double y_offset;
    float yaw, pitch;
    int allow_edit;
    int creative;
};

/* One ItemStack: item is a registry id (0 = the empty slot). */
struct iu_stack {
    int item, count, damage;
};

/* ItemInWorldManager.tryUseItem(player, world, held): ItemStack.useItemRightClick
 * then the manager's inventory update, exactly the Java body. held is the
 * slot's stack, mutated in place (item 0 when the slot empties); the return is
 * the Java boolean. wr is World.rand. give (may be NULL) receives the stack
 * the body passes to inventory.addItemStackToInventory, falling back to
 * dropPlayerItemWithRandomChoice (ItemBucket.func_150910_a, ItemGlassBottle),
 * item 0 when none. */
int itemuse_try_use_item(struct world *w, const struct iu_player *p, struct iu_stack *held, jrand *wr,
                         struct iu_stack *give);

/* ItemInWorldManager.activateBlockOrUseItem(player, world, held, x, y, z, side,
 * hx, hy, hz): the block activation first (false for every block a scene
 * holds), then ItemStack.tryPlaceItemIntoWorld, with the creative branch's
 * damage and count restore. held is mutated in place. wr is World.rand. */
int itemuse_activate_block_or_use_item(struct world *w, const struct iu_player *p, struct iu_stack *held,
                                       int x, int y, int z, int side, float hx, float hy, float hz,
                                       jrand *wr);

/* Item.getMovingObjectPositionFromPlayer(world, player, false): 1 with the
 * block cell when the look ray lands on a block. */
int itemuse_look_block(struct world *w, const struct iu_player *p, int *x, int *y, int *z);

/* Live client/server calls carry Item.itemRand's per-role static stream. */
void itemuse_set_live_det(det_state *det, int role);

#endif
