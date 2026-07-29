/* CPU reference: boat_control thrust/momentum battery. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/boat_control.h"

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    static McSinTable st;
    u32 out[BC_OUT];
    int i;
    (void)argc;
    (void)argv;
    mc_sin_table_init(&st);
    bc_run_battery(&st, out);
    for (i = 0; i < BC_OUT; ++i)
        emit_u32(out[i]);
    return 0;
}
