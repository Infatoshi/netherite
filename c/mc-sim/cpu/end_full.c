/* CPU reference: End chunk (0,0) terrain + portal frame eye-insertion scenario. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/end_full.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    CpePrimer *primer = (CpePrimer *)malloc(sizeof(CpePrimer));
    CpeScratch *sc = (CpeScratch *)malloc(sizeof(CpeScratch));
    EpWorld ep;

    ef_run(primer, sc, &ep, seed);

    for (int i = 0; i < EF_CHUNK_N; ++i)
        printf("%04x\n", (unsigned)primer->data[i]);

    u64 out[EP_NOUT];
    ep_dump(&ep, out);
    for (int i = 0; i < EP_NOUT; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);

    free(sc); free(primer);
    return 0;
}
