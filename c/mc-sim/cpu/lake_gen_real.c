/* CPU reference: WorldGenLakes on sbr_run primer + genlayer biomes for chunk (0,0). Emits
 * ChunkPrimer char[65536] in index order as %04x, one per line. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/lake_gen_real.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    LgrCtx *ctx = (LgrCtx *)malloc(sizeof(LgrCtx));

    lgr_run(primer, ctx, seed, 0, 0);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)primer->data[i]);

    free(ctx);
    free(primer);
    return 0;
}
