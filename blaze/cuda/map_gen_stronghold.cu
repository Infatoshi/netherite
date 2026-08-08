#include <cstdio>
#include <cuda_runtime.h>
#include <cstdlib>
#include "../core/map_gen_stronghold.h"

__global__ void run_sh(i64 seed, u16 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ChunkPrimer p; sh_run(&p, seed, 0, 0);
    for (int i = 0; i < 65536; ++i) out[i] = p.data[i];
}

int main(int argc, char **argv) {
    cudaDeviceSetLimit(cudaLimitStackSize, 65536);
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    u16 *d, h[65536];
    cudaMalloc(&d, 65536*sizeof(u16));
    run_sh<<<1,1>>>(seed, d);
    cudaDeviceSynchronize();
    cudaMemcpy(h, d, 65536*sizeof(u16), cudaMemcpyDeviceToHost);
    cudaFree(d);
    for (int i = 0; i < 65536; ++i) printf("%04x\n", (unsigned)h[i]);
    return 0;
}
