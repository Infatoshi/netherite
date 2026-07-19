/* CPU reference: 27-slot chest insert/extract battery dump. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/tile_entity_chest.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u64 out[TEC_OUT];
    tec_run_battery(out);
    for (int i = 0; i < TEC_OUT; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    return 0;
}
