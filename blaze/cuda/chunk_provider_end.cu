/* CUDA: same core/chunk_provider_end.h pipeline, single-thread (sequential worldgen),
 * EndNoise heap-allocated inside cpe_provide_chunk (same pattern as chunk_provider.cu). */
#include <cstdio>
#include <cstdlib>
#include "../core/chunk_provider_end.h"

__global__ void run_cpe(i64 seed, CpePrimer *primer, CpeScratch *sc) {
    if (threadIdx.x || blockIdx.x) return;
    cpe_provide_chunk(primer, sc, seed, 0, 0);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    CpePrimer *d_primer; cudaMalloc(&d_primer, sizeof(CpePrimer));
    CpeScratch *d_sc; cudaMalloc(&d_sc, sizeof(CpeScratch));

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_cpe<<<1, 1>>>(seed, d_primer, d_sc);
    cudaDeviceSynchronize();

    CpePrimer *h_p = (CpePrimer *)malloc(sizeof(CpePrimer));
    cudaMemcpy(h_p, d_primer, sizeof(CpePrimer), cudaMemcpyDeviceToHost);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)h_p->data[i]);

    free(h_p);
    cudaFree(d_primer); cudaFree(d_sc);
    return 0;
}
