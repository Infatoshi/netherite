/* CPU reference: chunk_provider terrain + full collision player physics, pos+vel hex dump. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/player_physics_full.h"

static void emit_double(double v) {
    u64 bits;
    memcpy(&bits, &v, 8);
    printf("%016llx\n", (unsigned long long)bits);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int nticks = (argc > 2) ? atoi(argv[2]) : PPF_NUM_TICKS;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    double *out = (double *)malloc(sizeof(double) * (size_t)nticks * 6);

    ppf_run(primer, sc, st, seed, nticks, out);

    for (int i = 0; i < nticks * 6; ++i)
        emit_double(out[i]);

    free(out);
    free(sc);
    free(primer);
    free(st);
    return 0;
}
