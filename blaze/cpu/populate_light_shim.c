/* CPU reference: mushroom placement scene with w_light stub vs light_propagation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/populate_light_shim.h"

static void run_seed(i64 seed, McSinTable *st) {
    u16 *blocks_a = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    u16 *blocks_b = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    u8 *sky = (u8 *)malloc(W_N);
    u8 *blk = (u8 *)malloc(W_N);
    u8 *tmp_sky = (u8 *)malloc(W_N);
    u8 *tmp_blk = (u8 *)malloc(W_N);
    int *out_idx = (int *)malloc(sizeof(int) * (size_t)W_N);
    u16 *out_blk = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    FoliageCoord *fol = (FoliageCoord *)malloc(sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);
    JavaRandom r;
    PlsOutBuf ob;
    int i;

    ob.n = 0;
    ob.idx = out_idx;
    ob.blk = out_blk;

    pls_run_mushroom_scene(seed, pls_emit_buf, &ob, st, blocks_a, blocks_b,
                           sky, blk, tmp_sky, tmp_blk, sc, primer, fol, &r);

    for (i = 0; i < ob.n; ++i)
        printf("%06x%04x\n", out_idx[i], (unsigned)out_blk[i]);

    free(out_blk);
    free(out_idx);
    free(fol);
    free(primer);
    free(sc);
    free(tmp_blk);
    free(tmp_sky);
    free(blk);
    free(sky);
    free(blocks_b);
    free(blocks_a);
}

int main(int argc, char **argv) {
    /* Seeds that place mushrooms under the stale light stub but not under the
     * fixpoint CA (or vice versa). Default seed 12345 is vacuous (0 diffs). */
    static const i64 k_seeds[] = {9LL, 19LL};
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);

    if (argc > 1) {
        run_seed(strtoll(argv[1], 0, 10), st);
    } else {
        int i;
        for (i = 0; i < 2; ++i) run_seed(k_seeds[i], st);
    }

    free(st);
    return 0;
}
