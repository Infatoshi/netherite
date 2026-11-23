/* ColorizerFoliage's fixed colours and Item.getColorFromItemStack for the
 * items whose colour is their block's render colour. */
#ifndef NETHERITE_ITEM_COLOR_H
#define NETHERITE_ITEM_COLOR_H

#define FOLIAGE_PINE 6396257  /* ColorizerFoliage.getFoliageColorPine */
#define FOLIAGE_BIRCH 8431445 /* ColorizerFoliage.getFoliageColorBirch */
#define FOLIAGE_BASIC 4764952 /* ColorizerFoliage.getFoliageColorBasic */

/* getColorFromItemStack(stack, 0) of leaves (ItemLeaves: BlockOldLeaf's
 * spruce and birch colours by meta, else basic; the new leaves are basic),
 * vines and lily pads (ItemColored) and tall grass (its meta 0 white, the
 * rest GRASS_COLOR, ColorizerGrass.getGrassColor(0.5, 1.0)); -1 for any
 * other item. */
int item_block_color(int id, int meta, int grass_color);

#endif
