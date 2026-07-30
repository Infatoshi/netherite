/* CPU reference: container_click scripted slotClick battery. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/container_click.h"

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    u32 out[CC_OUT];
    int i;
    (void)argc;
    (void)argv;
    cc_run_battery(out);
    for (i = 0; i < CC_OUT; ++i) emit_u32(out[i]);
    return 0;
}
