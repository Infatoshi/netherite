/* CPU reference: enchant_table offer RNG battery. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/enchant_table.h"

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    u32 out[ET_OUT];
    int i;
    (void)argc;
    (void)argv;
    et_run_battery(out);
    for (i = 0; i < ET_OUT; ++i)
        emit_u32(out[i]);
    return 0;
}
