/* CPU reference: stronghold loot fillInventory materialization battery. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/stronghold_loot.h"

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    u32 out[SHL_BAT_OUT];
    int i;
    (void)argc;
    (void)argv;
    shl_run_battery(out);
    for (i = 0; i < SHL_BAT_OUT; ++i)
        emit_u32(out[i]);
    return 0;
}
