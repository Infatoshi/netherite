/* CUDA batch driver for the smoke kernel - same core/smoke_core.h as the CPU path.
 * One thread computes the whole stream (this is a determinism smoke test, not a perf test);
 * the batched megakernel proper arrives with oracle #1. Output format matches cpu/smoke.c. */
#include <cstdio>
#include <cstdlib>
#include "../core/smoke_core.h"

__global__ void smoke(u64 seed, u64 *out, int n) {
    if (threadIdx.x == 0 && blockIdx.x == 0) smoke_kernel(seed, out, n);
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : 12345ULL;
    int n    = (argc > 2) ? atoi(argv[2]) : 256;
    u64 *d_out; cudaMalloc(&d_out, sizeof(u64) * n);
    smoke<<<1, 32>>>(seed, d_out, n);
    cudaDeviceSynchronize();
    u64 *out = (u64 *)malloc(sizeof(u64) * n);
    cudaMemcpy(out, d_out, sizeof(u64) * n, cudaMemcpyDeviceToHost);
    for (int i = 0; i < n; i++) printf("%016llx\n", (unsigned long long)out[i]);
    free(out); cudaFree(d_out);
    return 0;
}
