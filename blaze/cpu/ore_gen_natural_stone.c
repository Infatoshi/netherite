/* CPU reference: WorldGenMinable (StonePredicate) on caves_real primer for chunk (0,0). Emits
 * ChunkPrimer char[65536] in index order as %04x, one per line. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/ore_gen_natural_stone.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);

    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CvrScratch *ctx = (CvrScratch *)malloc(sizeof(CvrScratch));

    ogns_run(primer, ctx, seed, st);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)primer->data[i]);

    free(ctx);
    free(primer);
    free(st);
    return 0;
}
