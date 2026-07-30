/* CPU reference: post-init TLC slice -> render-opt light_combine_pack per cell; %08x lines. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/render_opt_obs_hook.h"

static void run_seed(u64 seed) {
    Env e;
    TcfAux aux;
    TcfScratch scratch;
    ChunkPrimer primer;
    CpScratch sc;
    McSinTable st;
    i32 packed[ROOH_VOL];
    int i;

    mc_sin_table_init(&st);
    rooh_init_and_snapshot(&e, &aux, seed, &primer, &sc, &st, &scratch, packed);
    for (i = 0; i < ROOH_VOL; ++i)
        printf("%08x\n", (unsigned)(u32)packed[i]);
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
