/* CPU reference: entities in a persistent multi-chunk world (spawn + AI + A* + cross-chunk move +
 * melee). One hex line per u64 in the flat dump buffer; identical formatting to the CUDA driver.
 * Cross-chunk + combat evidence are the last three summary lines per seed (and echoed to stderr). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/entities_world.h"

static void run_seed(u64 seed) {
    EwState     *st = (EwState *)malloc(sizeof(EwState));
    EwScratch   *sc = (EwScratch *)malloc(sizeof(EwScratch));
    TwmWorld    *w  = (TwmWorld *)malloc(sizeof(TwmWorld));
    TwmScratch  *ts = (TwmScratch *)malloc(sizeof(TwmScratch));
    ChunkPrimer *pr = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch   *cs = (CpScratch *)malloc(sizeof(CpScratch));
    McSinTable  *sn = (McSinTable *)malloc(sizeof(McSinTable));
    u64         *out = (u64 *)malloc(sizeof(u64) * EW_NLINES);
    int i;

    mc_sin_table_init(sn);
    ew_run(st, sc, w, ts, pr, cs, sn, seed, out);

    for (i = 0; i < EW_NLINES; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);

    {
        u64 crossings = out[EW_NLINES - 3];
        u64 spawns    = out[EW_NLINES - 2];
        union { u32 u; float f; } dm; dm.u = (u32)out[EW_NLINES - 1];
        fprintf(stderr,
                "seed %llu: cross-chunk crossings=%llu  spawns=%llu  total_damage=%.6f\n",
                (unsigned long long)seed, (unsigned long long)crossings,
                (unsigned long long)spawns, dm.f);
    }

    free(out); free(sn); free(cs); free(pr); free(ts); free(w); free(sc); free(st);
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    int i;
    if (argc > 1) {
        run_seed(strtoull(argv[1], 0, 10));
    } else {
        for (i = 0; i < 3; ++i) run_seed(k_seeds[i]);
    }
    return 0;
}
