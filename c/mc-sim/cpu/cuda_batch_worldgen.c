/* CPU reference: CBW_N cp_provide_chunk calls; 4 hex lines per env (seed,cx,cz,hash). */
#include <stdio.h>
#include "../core/cuda_batch_worldgen.h"

int main(void) {
    McSinTable st;
    int e;

    mc_sin_table_init(&st);
    for (e = 0; e < CBW_N; ++e) {
        ChunkPrimer primer;
        CpScratch sc;

        cbw_provide_one(&primer, &sc, &st, &CBW_ENVS[e]);
        printf("%016llx\n", (unsigned long long)CBW_ENVS[e].seed);
        printf("%016llx\n", (unsigned long long)(u64)(u32)CBW_ENVS[e].cx);
        printf("%016llx\n", (unsigned long long)(u64)(u32)CBW_ENVS[e].cz);
        printf("%016llx\n", (unsigned long long)cbw_primer_hash(&primer));
    }
    return 0;
}
