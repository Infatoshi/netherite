/* CPU reference: overworld_region (owfl_run generalized to an arbitrary base chunk).
 * Args: [seed [bcx [bcz]]] (defaults 0 0 0). Emits a header then W_N x %04x (index order). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/overworld_region.h"

static void run_region(i64 seed, int bcx, int bcz, McSinTable *st) {
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

    owr_run(&w, sc, primer, &r, fol, seed, sky, blk, tmp_sky, tmp_blk, mc_cur, mc_tmp, before_ca,
            bcx, bcz);

    printf("owr seed=%lld bcx=%d bcz=%d\n", (long long)seed, bcx, bcz);
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
    /* Default (no argv): seeds 0/9/19 - seed 0 alone let a host-only liquid
     * stamp stub slip through; 9 was the first live fail. Argv override keeps
     * single-seed forensics (and optional bcx/bcz). Same pattern as
     * overworld_full_live.c (12345/0/7). */
    static const i64 k_seeds[] = {0LL, 9LL, 19LL};
    int bcx = 0, bcz = 0;
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);

    if (argc > 1) {
        i64 seed = strtoll(argv[1], 0, 10);
        if (argc > 2) bcx = (int)strtol(argv[2], 0, 10);
        if (argc > 3) bcz = (int)strtol(argv[3], 0, 10);
        run_region(seed, bcx, bcz, st);
    } else {
        int i;
        for (i = 0; i < 3; ++i) run_region(k_seeds[i], 0, 0, st);
    }

    free(st);
    return 0;
}
