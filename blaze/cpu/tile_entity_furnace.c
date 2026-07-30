/* CPU reference: furnace smelt dump at tick marks. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/tile_entity_furnace.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    TeFurnace f;
    u64 out[TE_OUT];
    te_run_dump(&f, out);
    for (int i = 0; i < TE_OUT; ++i) printf("%016llx\n", (unsigned long long)out[i]);
    return 0;
}
