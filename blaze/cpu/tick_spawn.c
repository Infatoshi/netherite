/* CPU reference: 16-tick spawn compose; three hex lines per tick (tick, spawn hash, cur). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/tick_spawn.h"

static void emit_line(u64 tick_bits, u64 spawn_hash, u64 cur_bits, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)tick_bits);
    printf("%016llx\n", (unsigned long long)spawn_hash);
    printf("%016llx\n", (unsigned long long)cur_bits);
}

static void run_seed(u64 seed) {
    Env e;
    TsAux aux;
    u16 blocks[TS_VOL];
    u8 tmp_sky[TS_VOL];
    u8 tmp_blk[TS_VOL];
    u64 decisions[TS_MAX_DECISIONS];
    ts_run(&e, &aux, seed, blocks, tmp_sky, tmp_blk, decisions, emit_line, NULL);
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
