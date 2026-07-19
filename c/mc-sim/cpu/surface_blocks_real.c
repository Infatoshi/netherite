/* CPU reference: replaceBiomeBlocks on cpbw_run primer for chunk (0,0). Emits ChunkPrimer
 * char[65536] in index order as %04x, one per line. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/surface_blocks_real.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));

    sbr_run(primer, sc, seed, 0, 0);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)primer->data[i]);

    free(sc);
    free(primer);
    return 0;
}
