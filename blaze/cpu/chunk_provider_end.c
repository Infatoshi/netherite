/* CPU reference: ChunkProviderEnd.provideChunk minus structures/Chunk/skylight/populate
 * for chunk (0,0) at a given world seed. Emits the full ChunkPrimer char[65536] as %04x lines. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/chunk_provider_end.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    CpePrimer *primer = (CpePrimer *)malloc(sizeof(CpePrimer));
    CpeScratch *sc = (CpeScratch *)malloc(sizeof(CpeScratch));

    cpe_provide_chunk(primer, sc, seed, 0, 0);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)primer->data[i]);

    free(sc); free(primer);
    return 0;
}
