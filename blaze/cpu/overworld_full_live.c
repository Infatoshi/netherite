/* CPU reference: overworld_full + light CA populate + fluid CA; emit 262144 x %04x. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/overworld_full_live.h"

static void run_seed(i64 seed, McSinTable *st) {
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
    u16 *mc_cur = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    u16 *mc_tmp = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    u16 *before_ca = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    JavaRandom r;
    int i;

    owfl_run(&w, sc, primer, &r, fol, seed, sky, blk, tmp_sky, tmp_blk, mc_cur, mc_tmp, before_ca);

    for (i = 0; i < W_N; ++i)
        printf("%04x\n", (unsigned)w.blocks[i]);

    free(before_ca);
    free(mc_tmp);
    free(mc_cur);
    free(fol);
    free(primer);
    free(sc);
    free(tmp_blk);
    free(tmp_sky);
    free(blk);
    free(sky);
    free(w.blocks);
}

int main(int argc, char **argv) {
    static const i64 k_seeds[] = {12345LL, 0LL, 7LL};
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);

    if (argc > 1) {
        run_seed(strtoll(argv[1], 0, 10), st);
    } else {
        int i;
        for (i = 0; i < 3; ++i) run_seed(k_seeds[i], st);
    }

    free(st);
    return 0;
}
