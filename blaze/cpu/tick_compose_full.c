/* CPU reference: 64-tick full compose; three hex lines per tick (tick, combined hash, cur). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/tick_compose_full.h"

static void emit_line(u64 tick_bits, u64 combined_hash, u64 cur_bits, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)tick_bits);
    printf("%016llx\n", (unsigned long long)combined_hash);
    printf("%016llx\n", (unsigned long long)cur_bits);
}

static void run_seed(u64 seed) {
    Env e;
    TcfAux aux;
    TcfScratch scratch;
    ChunkPrimer primer;
    CpScratch sc;
    PfWork work;
    McSinTable st;

    mc_sin_table_init(&st);
    tcf_run(&e, &aux, seed, &primer, &sc, &st, &work, &scratch, emit_line, NULL);
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
