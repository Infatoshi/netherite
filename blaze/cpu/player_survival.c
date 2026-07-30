/* CPU reference: generate a multi-chunk region, spawn a survival player, run the deterministic
 * action-tape tick loop (physics + break/place/inventory + vitals), dump per-tick state as raw hex
 * (%016llx). Same core/ header as the CUDA driver -> CPU==CUDA bitwise (SPEC fidelity contract). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/player_survival.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int nticks = (argc > 2) ? atoi(argv[2]) : PSV_NTICKS;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    Chunk *a = (Chunk *)malloc(sizeof(Chunk) * PSV_NCHUNKS);
    Chunk *b = (Chunk *)malloc(sizeof(Chunk) * PSV_NCHUNKS);
    u64 *out = (u64 *)malloc(sizeof(u64) * (size_t)nticks * PSV_FIELDS);

    psv_run(a, b, primer, sc, st, seed, nticks, out);

    for (int i = 0; i < nticks * PSV_FIELDS; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);

    free(out); free(b); free(a); free(sc); free(primer); free(st);
    return 0;
}
