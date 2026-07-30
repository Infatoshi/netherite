/* CPU reference: proves the worldgen flywheel via core/region_reproduce.h (rr_run) --
 * idempotence, tiling invariance, origin-shift overlap, and a canonical fingerprint +
 * sampled cells. Same rr_run() runs in cuda/region_reproduce.cu on one thread, so stdout
 * is byte-identical (SPEC internal-consistency contract). Big tensors are heap scratch. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/region_reproduce.h"

int main(void) {
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    RrScratch *s = (RrScratch *)malloc(sizeof(RrScratch));

    rr_run(s, st);

    free(s); free(st);
    return 0;
}
