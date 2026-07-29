/* CPU reference: WorldGenDungeons on overworld terrain. Delta (%06x%04x) or full 262144 dump. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/populate_dungeon_golden.h"

static void emit(int idx, u16 block, void *ctx) {
    PdgOutBuf *b = (PdgOutBuf *)ctx;
    if (b->n < W_N) {
        b->idx[b->n] = idx;
        b->blk[b->n] = block;
        b->n++;
    }
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);

    u16 *before = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    u16 *after = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    int *out_idx = (int *)malloc(sizeof(int) * (size_t)W_N);
    u16 *out_blk = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    JavaRandom r;
    PdgOutBuf ob;
    int i;

    ob.n = 0;
    ob.idx = out_idx;
    ob.blk = out_blk;

    pdg_run(seed, emit, &ob, st, before, after, sc, primer, &r);

    if (ob.n == W_N) {
        for (i = 0; i < ob.n; ++i)
            printf("%04x\n", (unsigned)out_blk[i]);
    } else {
        for (i = 0; i < ob.n; ++i)
            printf("%06x%04x\n", out_idx[i], (unsigned)out_blk[i]);
    }

    free(out_blk);
    free(out_idx);
    free(primer);
    free(sc);
    free(after);
    free(before);
    free(st);
    return 0;
}
