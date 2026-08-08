/* CPU reference: nether terrain + fortress placement for chunk (0,0), hex dump of CpnPrimer. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/nether_full.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    if (nf_to_vanilla(CPN_LAVA) != 11 ||
        nf_to_vanilla(CPN_FLOWING_LAVA) != 10) {
        fprintf(stderr, "nether lava registry mapping regression\n");
        return 2;
    }

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    CpnPrimer *primer = (CpnPrimer *)malloc(sizeof(CpnPrimer));
    CpnHellScratch *sc = (CpnHellScratch *)malloc(sizeof(CpnHellScratch));
    CpnHellNoise *noise = (CpnHellNoise *)malloc(sizeof(CpnHellNoise));
    cpn_noise_init(noise, seed);

    nf_run(primer, sc, st, noise, seed, 0, 0);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)primer->data[i]);

    free(noise); free(sc); free(primer); free(st);
    return 0;
}
