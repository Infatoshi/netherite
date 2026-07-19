/* CPU reference: 16-tick compose (copy + block tickers + fluid CA); 3 hex lines per tick. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/tick_compose_1.h"

static void emit_line(u64 tick_bits, u64 block_hash, u64 cur_bits, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)tick_bits);
    printf("%016llx\n", (unsigned long long)block_hash);
    printf("%016llx\n", (unsigned long long)cur_bits);
}

static void run_seed(u64 seed) {
    Env e;
    TrbAux aux;
    u16 cur[TFC_SLICE_VOL];
    u16 tmp[TFC_SLICE_VOL];
    tc1_run(&e, &aux, seed, cur, tmp, emit_line, NULL);
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
