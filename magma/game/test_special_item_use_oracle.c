#include "item_special_place.h"
#include <stdio.h>

int main(void) {
    static const int items[4] = {323, 355, 397, 425};
    for (int at = 0; at < 4; ++at) {
        int item = items[at];
        int variants = item == 397 ? 6 : item == 425 ? 16 : 1;
        for (int item_meta = 0; item_meta < variants; ++item_meta)
            for (int face = 0; face < 6; ++face)
                for (int yaw = 0; yaw < 4; ++yaw) {
                    IspResult result = isp_plan(
                        item, item_meta, face, yaw);
                    printf("S %d %d %d %d %s %d %d %d %d %d\n",
                        item, item_meta, face, yaw,
                        result.accepted ? "SUCCESS" : "FAIL",
                        result.accepted ? 1 : 2,
                        result.block, result.meta,
                        result.tile_kind, result.tile_aux);
                }
    }
    return 0;
}
