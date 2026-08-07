#include <stdio.h>
#include <stdlib.h>
#include "../core/structures_placement.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int cx = (argc > 2) ? atoi(argv[2]) : 0;
    int cz = (argc > 3) ? atoi(argv[3]) : 0;
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    ChunkPrimer *p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    stp_run(p, sc, st, seed, cx, cz);
    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)p->data[i]);
    free(sc);
    free(p);
    free(st);
    return 0;
}
