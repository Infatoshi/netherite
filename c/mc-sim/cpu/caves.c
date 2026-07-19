/* CPU reference: MapGenCaves over a synthetic all-STONE ChunkPrimer for chunk (0,0). Seeds from
 * argv[1], runs MapGenBase.generate (range-8 neighborhood) into the primer, then prints every one
 * of the 65536 cells as %04x in raw array (index = x<<12 | z<<8 | y) order so any mismatch is
 * bitwise and localizable. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/caves.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    ChunkPrimer *p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    for (int i = 0; i < 65536; ++i) p->data[i] = CV_STONE;

    cv_generate(p, seed, 0, 0, st);

    for (int idx = 0; idx < 65536; ++idx)
        printf("%04x\n", (unsigned)p->data[idx]);

    free(p); free(st);
    return 0;
}
