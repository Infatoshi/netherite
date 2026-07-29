/* CPU reference: WorldGenTrees on sbr_run primer for chunk (0,0), plant column (8,8). Emits
 * ChunkPrimer char[65536] in index order as %04x, one per line. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/tree_gen_oak_real.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));

    tgor_run(primer, sc, seed);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)primer->data[i]);

    free(sc);
    free(primer);
    return 0;
}
