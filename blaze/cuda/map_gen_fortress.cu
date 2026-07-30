#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/map_gen_fortress.h"

__global__ void run_fort(i64 seed, u16 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ft_run((ChunkPrimer *)out, seed, 0, 0);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    cudaDeviceSetLimit(cudaLimitStackSize, 131072);
    u16 *d, h[65536];
    cudaMalloc(&d, 65536 * sizeof(u16));
    run_fort<<<1, 1>>>(seed, d);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) { fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err)); return 1; }
    cudaMemcpy(h, d, 65536 * sizeof(u16), cudaMemcpyDeviceToHost);
    cudaFree(d);
    for (int i = 0; i < 65536; ++i) printf("%04x\n", (unsigned)h[i]);
    return 0;
}
