/* CPU reference: ChunkProviderOverworld.replaceBiomeBlocks for chunk (0,0) over a synthetic Plains
 * primer at a given world seed. Emits the full ChunkPrimer char[65536] in index order as %04x,
 * one per line, for bitwise diff. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/surface_blocks.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    sb_run(primer, seed);

    for (int i = 0; i < 65536; i++) {
        printf("%04x\n", (unsigned)primer->data[i]);
    }
    free(primer);
    return 0;
}
