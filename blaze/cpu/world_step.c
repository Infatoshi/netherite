/* CPU reference: one UNIFIED world-step tick loop - env CAs (halo fluid/light/block-tickers) THEN
 * the survival player (physics/break/place/inventory/vitals) over ONE double-buffered multi-chunk
 * world. Per tick dumps {world light hash, PSV_FIELDS player+block-hash state}; then tail evidence
 * (fluid+light crossed the +x chunk boundary) + per-chunk hashes. Same core/world_step.h as the
 * CUDA driver -> CPU==CUDA bitwise (SPEC fidelity contract). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/world_step.h"

static void run_seed(u64 seed, int nticks) {
    TwmWorld    *w      = (TwmWorld *)malloc(sizeof(TwmWorld));
    WsScratch   *s      = (WsScratch *)malloc(sizeof(WsScratch));
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch   *sc     = (CpScratch *)malloc(sizeof(CpScratch));
    McSinTable  *st     = (McSinTable *)malloc(sizeof(McSinTable));
    size_t       nlines = (size_t)nticks * WS_PERTICK + WS_TAIL;
    u64         *out    = (u64 *)malloc(sizeof(u64) * nlines);
    size_t       i;

    mc_sin_table_init(st);
    ws_run(w, s, primer, sc, st, seed, nticks, out);
    for (i = 0; i < nlines; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);

    free(out); free(st); free(sc); free(primer); free(s); free(w);
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    int nticks = (argc > 2) ? atoi(argv[2]) : WS_NTICKS;
    int i;
    if (argc > 1) {
        run_seed(strtoull(argv[1], 0, 10), nticks);
    } else {
        for (i = 0; i < 3; ++i) run_seed(k_seeds[i], nticks);
    }
    return 0;
}
