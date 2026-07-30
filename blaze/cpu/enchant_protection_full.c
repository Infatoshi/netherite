/* CPU reference driver for enchant_protection_full. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/enchant_protection_full.h"

static u32 g_out[EPF_NOUT];

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int k = 0;
    epf_run_battery(g_out, &k);
    for (int i = 0; i < EPF_NOUT; ++i)
        printf("%08x\n", (unsigned)g_out[i]);
    return 0;
}
