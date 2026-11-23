/* ColorizerFoliage and the block-coloured items; see item_color.h. */
#include "item_color.h"

int item_block_color(int id, int meta, int grass_color)
{
    switch (id)
    {
    case 18: /* BlockOldLeaf.getRenderColor */
        return (meta & 3) == 1 ? FOLIAGE_PINE : (meta & 3) == 2 ? FOLIAGE_BIRCH : FOLIAGE_BASIC;
    case 161: /* BlockLeaves.getRenderColor */
    case 106: /* BlockVine.getRenderColor */
        return FOLIAGE_BASIC;
    case 111: /* BlockLilyPad.getRenderColor */
        return 2129968;
    case 31: /* BlockTallGrass.getRenderColor */
        return meta == 0 ? 16777215 : grass_color;
    default:
        return -1;
    }
}
