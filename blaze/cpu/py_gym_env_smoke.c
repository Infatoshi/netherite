/* CPU driver: seed 12345, fixed replay -> obs hash hex lines (reset + 16 steps = 17 lines). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/py_gym_env_smoke.h"

static void emit_hash(u64 obs_hash, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)obs_hash);
}

static void run_seed(u64 seed) {
    PgesEnv g;
    pges_run_replay(&g, seed, emit_hash, NULL);
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : 12345ULL;
    run_seed(seed);
    return 0;
}
