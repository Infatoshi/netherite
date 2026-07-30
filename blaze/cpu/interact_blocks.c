/* CPU reference: interact_blocks pure meta SM battery. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/interact_blocks.h"

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    u32 out[IB_OUT];
    int i;
    (void)argc;
    (void)argv;
    ib_run_battery(out);
    for (i = 0; i < IB_OUT; ++i) emit_u32(out[i]);
    return 0;
}
