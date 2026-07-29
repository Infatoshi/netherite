/* CPU reference: full pop_run with propagated light; dump 262144 block u16 (%04x). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/populate_light_live.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);

    World w;
    w.st = st;
    w.blocks = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    u8 *sky = (u8 *)malloc(W_N);
    u8 *blk = (u8 *)malloc(W_N);
    u8 *tmp_sky = (u8 *)malloc(W_N);
    u8 *tmp_blk = (u8 *)malloc(W_N);
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    FoliageCoord *fol = (FoliageCoord *)malloc(sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);
    JavaRandom r;

    pll_run(&w, sc, primer, &r, fol, seed, sky, blk, tmp_sky, tmp_blk);

    for (int i = 0; i < W_N; ++i)
        printf("%04x\n", (unsigned)w.blocks[i]);

    free(tmp_blk);
    free(tmp_sky);
    free(blk);
    free(sky);
    free(fol);
    free(primer);
    free(sc);
    free(w.blocks);
    free(st);
    return 0;
}
