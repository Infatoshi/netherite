/* CPU reference: WorldGenMinable over a 48^3 all-stone cube. Seeds a JavaRandom from argv[1],
 * places a diamond-ore blob of 33 blocks centered in the cube, then prints every cell's packed
 * block-state as %016llx in (y,z,x) order so any mismatch is bitwise and localizable. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/ore_gen.h"

#define DIM 48

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    u16 *cube = (u16 *)malloc(sizeof(u16) * DIM * DIM * DIM);
    u16 stone = mc_state(BLK_STONE, 0);
    u16 ore = mc_state(BLK_DIAMOND_ORE, 0);
    for (int i = 0; i < DIM * DIM * DIM; ++i) cube[i] = stone;

    JavaRandom r; jrand_set(&r, seed);
    mc_ore_gen(cube, DIM, st, &r, 16, 24, 16, 33, ore, stone);

    for (int y = 0; y < DIM; ++y)
        for (int z = 0; z < DIM; ++z)
            for (int x = 0; x < DIM; ++x) {
                u16 s = cube[mc_ore_idx(DIM, x, y, z)];
                printf("%016llx\n", (unsigned long long)s);
            }

    free(cube); free(st);
    return 0;
}
