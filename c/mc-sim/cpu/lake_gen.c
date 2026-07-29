/* CPU reference: WorldGenLakes over a synthetic DIM^3 cube. Seeds a JavaRandom from argv[1], builds
 * the deterministic world (STONE y<=12, DIRT 13<=y<=16, AIR y>=17), places a WATER lake at a fixed
 * BlockPos, then prints every cell's packed block-state as %04x in (y,z,x) order so any mismatch is
 * bitwise and localizable. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/lake_gen.h"

#define DIM 32

static void build_world(u16 *cube) {
    u16 stone = mc_state(BLK_STONE, 0);
    u16 dirt  = mc_state(BLK_DIRT, 0);
    u16 air   = mc_state(BLK_AIR, 0);
    for (int y = 0; y < DIM; ++y) {
        u16 fill = (y <= 12) ? stone : (y <= 16 ? dirt : air);
        for (int z = 0; z < DIM; ++z)
            for (int x = 0; x < DIM; ++x)
                cube[lk_idx(DIM, x, y, z)] = fill;
    }
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    u16 *cube = (u16 *)malloc(sizeof(u16) * DIM * DIM * DIM);
    build_world(cube);

    JavaRandom r; jrand_set(&r, seed);
    mc_lake_gen(cube, DIM, &r, 16, 24, 16, mc_state(BLK_WATER, 0), BLK_WATER);

    for (int y = 0; y < DIM; ++y)
        for (int z = 0; z < DIM; ++z)
            for (int x = 0; x < DIM; ++x)
                printf("%04x\n", (unsigned)cube[lk_idx(DIM, x, y, z)]);

    free(cube);
    return 0;
}
