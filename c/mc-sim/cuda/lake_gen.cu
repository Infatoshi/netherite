/* CUDA: SAME core/lake_gen.h as the CPU path. WorldGenLakes is a per-chunk SEQUENTIAL feature that
 * reads its own mid-pass writes, so it runs on ONE thread (correct, and keeps CPU==CUDA). */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/lake_gen.h"

#define DIM 32

__global__ void run_lake_gen(i64 seed, u16 *cube) {
    if (threadIdx.x || blockIdx.x) return;
    u16 stone = mc_state(BLK_STONE, 0);
    u16 dirt  = mc_state(BLK_DIRT, 0);
    u16 air   = mc_state(BLK_AIR, 0);
    for (int y = 0; y < DIM; ++y) {
        u16 fill = (y <= 12) ? stone : (y <= 16 ? dirt : air);
        for (int z = 0; z < DIM; ++z)
            for (int x = 0; x < DIM; ++x)
                cube[lk_idx(DIM, x, y, z)] = fill;
    }
    JavaRandom r; jrand_set(&r, seed);
    mc_lake_gen(cube, DIM, &r, 16, 24, 16, mc_state(BLK_WATER, 0), BLK_WATER);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int n = DIM * DIM * DIM;

    u16 *d_cube; cudaMalloc(&d_cube, sizeof(u16) * n);
    run_lake_gen<<<1, 1>>>(seed, d_cube);
    cudaDeviceSynchronize();

    u16 *cube = (u16 *)malloc(sizeof(u16) * n);
    cudaMemcpy(cube, d_cube, sizeof(u16) * n, cudaMemcpyDeviceToHost);

    for (int y = 0; y < DIM; ++y)
        for (int z = 0; z < DIM; ++z)
            for (int x = 0; x < DIM; ++x)
                printf("%04x\n", (unsigned)cube[lk_idx(DIM, x, y, z)]);

    free(cube);
    cudaFree(d_cube);
    return 0;
}
