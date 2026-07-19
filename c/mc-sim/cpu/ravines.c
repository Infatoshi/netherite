/* CPU reference: MapGenRavine over a synthetic ChunkPrimer for chunk (0,0). Seeds from argv[1],
 * runs the full MapGenBase.generate range-neighbor loop (range 8), then prints all 65536 primer
 * cells as %04x in raw index order (x<<12|z<<8|y) so any mismatch is bitwise and localizable.
 *
 * Synthetic primer: STONE for y in [1,127], AIR elsewhere. Fixed Plains biome (top=grass,
 * filler=dirt, not an exception). See core/ravines.h for the documented substitution. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/ravines.h"

static void build_primer(RavinePrimer *p) {
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 256; ++y)
                p->data[mc_ravine_idx(x, y, z)] = (y >= 1 && y <= 127) ? RV_STONE : RV_AIR;
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);

    RavinePrimer *primer = (RavinePrimer *)malloc(sizeof(RavinePrimer));
    build_primer(primer);

    MapGenRavine mg;
    mg.worldSeed = seed;
    mg.range = 8;
    mg.st = st;
    mc_ravine_generate(&mg, primer, 0, 0);

    for (int idx = 0; idx < 65536; ++idx)
        printf("%04x\n", (unsigned)primer->data[idx]);

    free(primer); free(st);
    return 0;
}
