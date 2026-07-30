/* CPU reference driver for enchant_damage_full. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/enchant_damage_full.h"

static u32 g_out[EDF_NOUT];

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int k = 0;
    edf_run_battery(g_out, &k);
    for (int i = 0; i < EDF_NOUT; ++i)
        printf("%08x\n", (unsigned)g_out[i]);
    return 0;
}
