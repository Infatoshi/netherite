/* CPU reference: ChunkProviderOverworld.setBlocksInChunk for chunk (0,0) with REAL genlayer biomes
 * and biome_props_full heightmap blend. Emits ChunkPrimer char[65536] as %04x, one per line. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/chunk_provider_biome_wired.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));

    cpbw_run(primer, sc, seed, 0, 0);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)primer->data[i]);

    free(sc); free(primer);
    return 0;
}
