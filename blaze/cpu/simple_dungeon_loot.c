/* CPU reference: simple_dungeon.json generation plus fillInventory. */
#include <stdio.h>
#include "../core/stronghold_loot.h"

int main(void) {
    u32 out[SHL_DUNGEON_BAT_OUT];
    shl_run_dungeon_battery(out);
    for (int i = 0; i < SHL_DUNGEON_BAT_OUT; ++i)
        printf("%08x\n", (unsigned)out[i]);
    return 0;
}
