/* CPU reference: WorldGenTrees (standard oak) over a 32^3 synthetic floor cube. Seeds a JavaRandom
 * from argv[1], builds the floor (dirt/grass/air), grows a tree at the fixed plant pos, then prints
 * every cell's packed block-state as %04x in (y,z,x) order so any mismatch is bitwise/localizable. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/tree_gen.h"

#define DIM TG_DIM

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    u16 *cube = (u16 *)malloc(sizeof(u16) * DIM * DIM * DIM);
    tg_build_world(cube, DIM);

    JavaRandom r; jrand_set(&r, seed);
    mc_tree_gen(cube, DIM, &r, TG_PLANT_X, TG_FLOOR_Y + 1, TG_PLANT_Z);

    for (int y = 0; y < DIM; ++y)
        for (int z = 0; z < DIM; ++z)
            for (int x = 0; x < DIM; ++x) {
                u16 s = cube[tg_idx(DIM, x, y, z)];
                printf("%04x\n", (unsigned)s);
            }

    free(cube);
    return 0;
}
