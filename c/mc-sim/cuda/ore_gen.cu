/* CUDA: SAME core/ore_gen.h as the CPU path. WorldGenMinable is a per-chunk SEQUENTIAL feature and
 * reads its own mid-loop writes, so it runs on ONE thread (correct, and keeps CPU==CUDA). The
 * SIN_TABLE is built on the host and copied to the device so its bytes match the CPU table. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/ore_gen.h"

#define DIM 48

__global__ void run_ore_gen(i64 seed, const McSinTable *st, u16 *cube, u16 ore, u16 stone) {
    if (threadIdx.x || blockIdx.x) return;
    for (int i = 0; i < DIM * DIM * DIM; ++i) cube[i] = stone;
    JavaRandom r; jrand_set(&r, seed);
    mc_ore_gen(cube, DIM, st, &r, 16, 24, 16, 33, ore, stone);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int n = DIM * DIM * DIM;
    u16 stone = mc_state(BLK_STONE, 0);
    u16 ore = mc_state(BLK_DIAMOND_ORE, 0);

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st; cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    u16 *d_cube; cudaMalloc(&d_cube, sizeof(u16) * n);
    run_ore_gen<<<1, 1>>>(seed, d_st, d_cube, ore, stone);
    cudaDeviceSynchronize();

    u16 *cube = (u16 *)malloc(sizeof(u16) * n);
    cudaMemcpy(cube, d_cube, sizeof(u16) * n, cudaMemcpyDeviceToHost);

    for (int y = 0; y < DIM; ++y)
        for (int z = 0; z < DIM; ++z)
            for (int x = 0; x < DIM; ++x) {
                u16 s = cube[mc_ore_idx(DIM, x, y, z)];
                printf("%016llx\n", (unsigned long long)s);
            }

    free(h_st); free(cube);
    cudaFree(d_st); cudaFree(d_cube);
    return 0;
}
