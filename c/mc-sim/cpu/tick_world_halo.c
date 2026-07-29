/* CPU reference: multi-chunk living-world tick loop with CROSS-CHUNK fluid + light.
 * Three hex lines per emit: (tick|chunk-idx|evidence-marker, blocks/hash/packed, light/hash/packed).
 * Per tick: (tick, all-chunk blocks hash, all-chunk light hash). Then TWM_NCHUNKS per-chunk
 * combined hashes. Then 4 boundary-evidence lines (fluid id+meta and block light at the +x
 * neighbour chunk edge) proving the CAs crossed the chunk boundary. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/tick_world_halo.h"

static void emit_line(u64 a, u64 b, u64 c, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)a);
    printf("%016llx\n", (unsigned long long)b);
    printf("%016llx\n", (unsigned long long)c);
}

static void run_seed(u64 seed) {
    TwmWorld    *w = (TwmWorld *)malloc(sizeof(TwmWorld));
    TwhScratch  *s = (TwhScratch *)malloc(sizeof(TwhScratch));
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch   *sc = (CpScratch *)malloc(sizeof(CpScratch));
    McSinTable  *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    twh_run(w, s, primer, sc, st, seed, emit_line, NULL);
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
