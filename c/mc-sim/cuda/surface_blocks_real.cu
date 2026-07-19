/* CUDA: same core/surface_blocks_real.h as CPU; single-thread worldgen on device. */
#include <cstdio>
#include <cstdlib>
#include "../core/surface_blocks_real.h"

__global__ void run_sbr(i64 seed, ChunkPrimer *primer, CpScratch *sc) {
    if (threadIdx.x || blockIdx.x) return;
    sbr_run(primer, sc, seed, 0, 0);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    ChunkPrimer *d_primer;
    CpScratch *d_sc;
    cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    cudaMalloc(&d_sc, sizeof(CpScratch));

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_sbr<<<1, 1>>>(seed, d_primer, d_sc);
    cudaDeviceSynchronize();

    ChunkPrimer *h_p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    cudaMemcpy(h_p, d_primer, sizeof(ChunkPrimer), cudaMemcpyDeviceToHost);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)h_p->data[i]);

    free(h_p);
    cudaFree(d_primer);
    cudaFree(d_sc);
    return 0;
}
