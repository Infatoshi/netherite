/* CUDA: SAME core/tree_gen.h as the CPU path. WorldGenTrees is a per-chunk SEQUENTIAL feature that
 * reads its own mid-loop writes, so it runs on ONE thread (correct, and keeps CPU==CUDA). */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/tree_gen.h"

#define DIM TG_DIM

__global__ void run_tree_gen(i64 seed, u16 *cube) {
    if (threadIdx.x || blockIdx.x) return;
    tg_build_world(cube, DIM);
    JavaRandom r; jrand_set(&r, seed);
    mc_tree_gen(cube, DIM, &r, TG_PLANT_X, TG_FLOOR_Y + 1, TG_PLANT_Z);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int n = DIM * DIM * DIM;

    u16 *d_cube; cudaMalloc(&d_cube, sizeof(u16) * n);
    run_tree_gen<<<1, 1>>>(seed, d_cube);
    cudaDeviceSynchronize();

    u16 *cube = (u16 *)malloc(sizeof(u16) * n);
    cudaMemcpy(cube, d_cube, sizeof(u16) * n, cudaMemcpyDeviceToHost);

    for (int y = 0; y < DIM; ++y)
        for (int z = 0; z < DIM; ++z)
            for (int x = 0; x < DIM; ++x) {
                u16 s = cube[tg_idx(DIM, x, y, z)];
                printf("%04x\n", (unsigned)s);
            }

    free(cube);
    cudaFree(d_cube);
    return 0;
}
