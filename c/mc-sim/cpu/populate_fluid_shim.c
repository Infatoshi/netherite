/* CPU reference: populate(0,0) then fluid_flow CA; emit block delta (%06x %04x %04x per change). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/populate_fluid_shim.h"

static void emit_delta(int idx, u16 before, u16 after, void *ctx) {
    (void)ctx;
    printf("%06x %04x %04x\n", idx, (unsigned)before, (unsigned)after);
}

static void run_seed(i64 seed, McSinTable *st) {
    PopWorld w;
    w.st = st;
    w.blocks = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    FoliageCoord *fol = (FoliageCoord *)malloc(sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);
    u16 *mc_cur = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    u16 *mc_tmp = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    u16 *before_ca = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    JavaRandom r;

    pfs_run(&w, sc, primer, &r, fol, seed, mc_cur, mc_tmp, before_ca, emit_delta, NULL);

    free(before_ca);
    free(mc_tmp);
    free(mc_cur);
    free(fol);
    free(primer);
    free(sc);
    free(w.blocks);
}

int main(int argc, char **argv) {
    static const i64 k_seeds[] = {12345LL, 0LL, 7LL};
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);

    if (argc > 1) {
        run_seed(strtoll(argv[1], 0, 10), st);
    } else {
        int i;
        for (i = 0; i < 3; ++i) run_seed(k_seeds[i], st);
    }

    free(st);
    return 0;
}
