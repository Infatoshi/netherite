/* CPU reference: ChunkProviderHell.provideChunk for chunk (0,0), hex dump of ChunkPrimer. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/chunk_provider_nether.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    CpnPrimer *primer = (CpnPrimer *)malloc(sizeof(CpnPrimer));
    CpnHellScratch *sc = (CpnHellScratch *)malloc(sizeof(CpnHellScratch));
    CpnHellNoise *noise = (CpnHellNoise *)malloc(sizeof(CpnHellNoise));
    cpn_noise_init(noise, seed);

    cpn_provide_chunk(primer, sc, st, noise, seed, 0, 0);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)primer->data[i]);

    free(noise); free(sc); free(primer); free(st);
    return 0;
}
