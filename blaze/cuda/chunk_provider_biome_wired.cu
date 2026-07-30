/* CUDA: same core/chunk_provider_biome_wired.h as CPU; single-thread worldgen on device. */
#include <cstdio>
#include <cstdlib>
#include "../core/chunk_provider_biome_wired.h"

__global__ void run_cpbw(i64 seed, ChunkPrimer *primer, CpScratch *sc) {
    if (threadIdx.x || blockIdx.x) return;
    cpbw_run(primer, sc, seed, 0, 0);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    ChunkPrimer *d_primer; cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    CpScratch *d_sc; cudaMalloc(&d_sc, sizeof(CpScratch));

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_cpbw<<<1, 1>>>(seed, d_primer, d_sc);
    cudaDeviceSynchronize();

    ChunkPrimer *h_p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    cudaMemcpy(h_p, d_primer, sizeof(ChunkPrimer), cudaMemcpyDeviceToHost);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)h_p->data[i]);

    free(h_p);
    cudaFree(d_primer); cudaFree(d_sc);
    return 0;
}
