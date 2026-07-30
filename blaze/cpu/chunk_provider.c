/* CPU reference: the composed overworld pipeline (ChunkProviderOverworld.provideChunk minus
 * structures/Chunk/skylight/populate) for chunk (0,0) at a given world seed. Emits the full
 * resulting ChunkPrimer char[65536] in index order as %04x, one per line, for bitwise diff with
 * the verbatim Java golden and cuda/chunk_provider.cu. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/chunk_provider.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));

    cp_provide_chunk(primer, sc, st, seed, 0, 0);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)primer->data[i]);

    free(sc); free(primer); free(st);
    return 0;
}
