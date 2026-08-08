/* CUDA: same core/nether_full.h pipeline, single-thread for CPU==CUDA. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/nether_full.h"

__global__ void run_nf(i64 seed, const McSinTable *st, CpnPrimer *primer,
        CpnHellScratch *sc, CpnHellNoise *noise) {
    if (threadIdx.x || blockIdx.x) return;
    nf_run(primer, sc, st, noise, seed, 0, 0);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    if (nf_to_vanilla(CPN_LAVA) != 11 ||
        nf_to_vanilla(CPN_FLOWING_LAVA) != 10) {
        fprintf(stderr, "nether lava registry mapping regression\n");
        return 2;
    }

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st;
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    CpnPrimer *d_primer;
    CpnHellScratch *d_sc;
    CpnHellNoise *d_noise;
    cudaMalloc(&d_primer, sizeof(CpnPrimer));
    cudaMalloc(&d_sc, sizeof(CpnHellScratch));
    cudaMalloc(&d_noise, sizeof(CpnHellNoise));
    CpnHellNoise *h_noise = (CpnHellNoise *)malloc(sizeof(CpnHellNoise));
    cpn_noise_init(h_noise, seed);
    cudaMemcpy(d_noise, h_noise, sizeof(CpnHellNoise), cudaMemcpyHostToDevice);

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_nf<<<1, 1>>>(seed, d_st, d_primer, d_sc, d_noise);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
        return 1;
    }

    CpnPrimer *h_p = (CpnPrimer *)malloc(sizeof(CpnPrimer));
    cudaMemcpy(h_p, d_primer, sizeof(CpnPrimer), cudaMemcpyDeviceToHost);
    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)h_p->data[i]);

    free(h_st); free(h_noise); free(h_p);
    cudaFree(d_st); cudaFree(d_primer); cudaFree(d_sc); cudaFree(d_noise);
    return 0;
}
