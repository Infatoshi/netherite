/* IRecipe.getRemainingItems / Item.getContainerItem (MC 1.11.2).
 *
 * ShapedRecipes.getRemainingItems (ShapedRecipes.java:42-52) calls
 * ForgeHooks.getContainerItem (ForgeHooks.java:957-969).
 * SlotCrafting.onTake (SlotCrafting.java:132-166) then
 * CraftingManager.getRemainingItems (CraftingManager.java:342-348).
 * Registered setContainerItem: water/lava/milk buckets -> empty bucket
 * (Item.java:1568-1577); dragon_breath -> glass_bottle (Item.java:1680). */
#ifndef MC_CRAFTING_REMAINING_H
#define MC_CRAFTING_REMAINING_H

#include "mc.h"

#define CRF_BUCKET 325
#define CRF_WATER_BUCKET 326
#define CRF_LAVA_BUCKET 327
#define CRF_MILK_BUCKET 335
#define CRF_GLASS_BOTTLE 374
#define CRF_DRAGON_BREATH 437

MC_HD static inline int crf_container_item(int item) {
    if (item == CRF_WATER_BUCKET || item == CRF_LAVA_BUCKET ||
        item == CRF_MILK_BUCKET)
        return CRF_BUCKET;
    if (item == CRF_DRAGON_BREATH)
        return CRF_GLASS_BOTTLE;
    return 0;
}

MC_HD static inline int crf_has_container_item(int item) {
    return crf_container_item(item) != 0;
}

#endif /* MC_CRAFTING_REMAINING_H */
