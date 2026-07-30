/* CPU reference: pop_run then passive spawn cycle records (hex u64 per line). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/mob_spawning_passive.h"

static void run_seed(i64 seed, i64 tick, McSinTable *st) {
    World w;
    MspScene scene;
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    FoliageCoord *fol = (FoliageCoord *)malloc(sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);
    u16 *blocks = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    JavaRandom r;
    int i;

    w.st = st;
    w.blocks = blocks;

    msp_run(&scene, &w, sc, primer, &r, fol, seed, tick);

    for (i = 0; i < scene.n_records; ++i)
        printf("%016llx\n", (unsigned long long)scene.records[i]);

    free(blocks);
    free(fol);
    free(primer);
    free(sc);
}

int main(int argc, char **argv) {
    static const i64 k_seeds[] = {12345LL, 0LL, 7LL};
    i64 tick = (argc > 2) ? (i64)strtoll(argv[2], 0, 10) : 100LL;
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);

    if (argc > 1) {
        run_seed(strtoll(argv[1], 0, 10), tick, st);
    } else {
        int i;
        for (i = 0; i < 3; ++i) run_seed(k_seeds[i], tick, st);
    }

    free(st);
    return 0;
}
