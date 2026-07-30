/* CPU reference: build the 2x2-chunk flat world (cpf_provide_chunk x4) then populate(0,0). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/superflat_populate.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);

    World w;
    w.st = st;
    w.blocks = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    CpfPrimer *primer = (CpfPrimer *)malloc(sizeof(CpfPrimer));
    FoliageCoord *fol = (FoliageCoord *)malloc(sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);
    GlArena *arena = (GlArena *)malloc(sizeof(GlArena));
    JavaRandom r;

    sfp_run(&w, primer, &r, fol, seed, NULL, arena);

    for (int i = 0; i < W_N; ++i)
        printf("%04x\n", (unsigned)w.blocks[i]);

    free(arena); free(fol); free(primer); free(w.blocks); free(st);
    return 0;
}
