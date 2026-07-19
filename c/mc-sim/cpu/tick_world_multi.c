/* CPU reference: multi-chunk living-world tick loop. Three hex lines per tick
 * (tick, all-chunk blocks hash, all-chunk light hash), then TWM_NCHUNKS per-chunk lines. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/tick_world_multi.h"

static void emit_line(u64 tick_bits, u64 blocks_hash, u64 light_hash, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)tick_bits);
    printf("%016llx\n", (unsigned long long)blocks_hash);
    printf("%016llx\n", (unsigned long long)light_hash);
}

static void run_seed(u64 seed) {
    TwmWorld   *w = (TwmWorld *)malloc(sizeof(TwmWorld));
    TwmScratch *s = (TwmScratch *)malloc(sizeof(TwmScratch));
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch   *sc = (CpScratch *)malloc(sizeof(CpScratch));
    McSinTable  *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    twm_run(w, s, primer, sc, st, seed, emit_line, NULL);
    free(st); free(sc); free(primer); free(s); free(w);
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
