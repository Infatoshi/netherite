/* CUDA trunk smoke: same trunk_core.h. Chunk is ~192KB so it lives in device global memory (not
 * thread stack). Proves the trunk headers are device-clean and CPU==CUDA deterministic. */
#include <cstdio>
#include <cstdlib>
#include "../core/trunk_core.h"

__global__ void fill(u64 seed, Chunk *c, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    *out = mc_trunk_fill(c, seed);
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : 12345ULL;
    Chunk *d_c; u64 *d_out;
    cudaMalloc(&d_c, sizeof(Chunk));
    cudaMalloc(&d_out, sizeof(u64));
    fill<<<1, 1>>>(seed, d_c, d_out);
    cudaDeviceSynchronize();
    u64 h; cudaMemcpy(&h, d_out, sizeof(u64), cudaMemcpyDeviceToHost);
    printf("%016llx\n", (unsigned long long)h);
    cudaFree(d_c); cudaFree(d_out);
    return 0;
}
