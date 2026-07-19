#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../core/structures.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    ChunkPrimer *p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    st_run(p, sc, st, seed, 0, 0);
    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)p->data[i]);
    free(sc); free(p); free(st);
    return 0;
}
