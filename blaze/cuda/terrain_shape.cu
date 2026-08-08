/* CUDA: identical terrain density field from the same core/terrain_shape.h. Determinism smoke
 * (1 thread); the batched per-env worldgen kernel comes after the math is proven bit-exact. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/terrain_shape.h"

__global__ void gen(i64 seed, double *out, int chunkX, int chunkZ) {
    if (threadIdx.x || blockIdx.x) return;
    TerrainNoise t;
    terrain_noise_init(&t, seed);
    terrain_generate_heightmap(&t, out, chunkX * 4, 0, chunkZ * 4);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int chunkX = 0, chunkZ = 0, n = 825;
    double *d_out; cudaMalloc(&d_out, sizeof(double) * n);
    gen<<<1, 1>>>(seed, d_out, chunkX, chunkZ);
    cudaDeviceSynchronize();
    double *out = (double *)malloc(sizeof(double) * n);
    cudaMemcpy(out, d_out, sizeof(double) * n, cudaMemcpyDeviceToHost);
    for (int i = 0; i < n; i++) {
        u64 bits; memcpy(&bits, &out[i], 8);
        printf("%016llx\n", (unsigned long long)bits);
    }
    free(out); cudaFree(d_out);
    return 0;
}
