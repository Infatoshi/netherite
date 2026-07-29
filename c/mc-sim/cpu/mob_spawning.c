/* CPU reference: flat-chunk hostile spawn cycle -> hex spawn decisions. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/mob_spawning.h"

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    i64 tick = (argc > 2) ? (i64)strtoll(argv[2], 0, 10) : 100LL;
    MsScene scene;
    int i;

    ms_init_flat(&scene, seed);
    ms_run(&scene, tick);

    for (i = 0; i < scene.n_decisions; ++i)
        printf("%016llx\n", (unsigned long long)scene.decisions[i]);
    return 0;
}
