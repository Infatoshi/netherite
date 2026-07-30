/* CUDA: same core/chunk_provider_nether.h pipeline, single-thread for CPU==CUDA. */
#include <cstdio>
#include <cstdlib>
#include "../core/chunk_provider_nether.h"

__global__ void run_cpn(i64 seed, const McSinTable *st, CpnPrimer *primer,
        CpnHellScratch *sc, CpnHellNoise *noise) {
    if (threadIdx.x || blockIdx.x) return;
    cpn_provide_chunk(primer, sc, st, noise, seed, 0, 0);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st; cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    CpnPrimer *d_primer; cudaMalloc(&d_primer, sizeof(CpnPrimer));
    CpnHellScratch *d_sc; cudaMalloc(&d_sc, sizeof(CpnHellScratch));
    CpnHellNoise *d_noise; cudaMalloc(&d_noise, sizeof(CpnHellNoise));
    CpnHellNoise *h_noise = (CpnHellNoise *)malloc(sizeof(CpnHellNoise));
    cpn_noise_init(h_noise, seed);
    cudaMemcpy(d_noise, h_noise, sizeof(CpnHellNoise), cudaMemcpyHostToDevice);

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_cpn<<<1, 1>>>(seed, d_st, d_primer, d_sc, d_noise);
    cudaDeviceSynchronize();

    CpnPrimer *h_p = (CpnPrimer *)malloc(sizeof(CpnPrimer));
    cudaMemcpy(h_p, d_primer, sizeof(CpnPrimer), cudaMemcpyDeviceToHost);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)h_p->data[i]);

    free(h_st); free(h_noise); free(h_p);
    cudaFree(d_st); cudaFree(d_primer); cudaFree(d_sc); cudaFree(d_noise);
    return 0;
}
