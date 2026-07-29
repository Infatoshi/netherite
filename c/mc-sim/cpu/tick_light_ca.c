/* CPU reference: 16-tick light CA on chunk slice; three hex lines per tick (tick, light hash, cur). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/tick_light_ca.h"

static void emit_line(u64 tick_bits, u64 light_hash, u64 cur_bits, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)tick_bits);
    printf("%016llx\n", (unsigned long long)light_hash);
    printf("%016llx\n", (unsigned long long)cur_bits);
}

static void run_seed(u64 seed) {
    Env e;
    u16 blocks[TLC_SLICE_VOL];
    u8 sky[TLC_SLICE_VOL];
    u8 blk[TLC_SLICE_VOL];
    u8 tmp_sky[TLC_SLICE_VOL];
    u8 tmp_blk[TLC_SLICE_VOL];
    tlc_run(&e, seed, blocks, sky, blk, tmp_sky, tmp_blk, emit_line, NULL);
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
