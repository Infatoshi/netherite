/* CPU reference: build the 2x2-chunk world (st_run x4) then populate(0,0), emit 262144 lines. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/overworld_full.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);

    World w;
    w.st = st;
    w.blocks = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    FoliageCoord *fol = (FoliageCoord *)malloc(sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);
    JavaRandom r;

    owf_run(&w, sc, primer, &r, fol, seed);

    for (int i = 0; i < W_N; ++i)
        printf("%04x\n", (unsigned)w.blocks[i]);

    free(fol); free(primer); free(sc); free(w.blocks); free(st);
    return 0;
}
