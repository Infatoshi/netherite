/* CPU reference: abandoned_mineshaft.json generation plus fillInventory. */
#include <stdio.h>
#include "../core/stronghold_loot.h"

int main(void) {
    u32 out[SHL_MINESHAFT_BAT_OUT];
    shl_run_mineshaft_battery(out);
    for (int i = 0; i < SHL_MINESHAFT_BAT_OUT; ++i)
        printf("%u\n", (unsigned)out[i]);
    return 0;
}
