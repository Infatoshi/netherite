/* CUDA driver for player_physics_full - same core/ header as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/player_physics_full.h"

__global__ void run_ppf(i64 seed, int nticks, const McSinTable *st,
                        ChunkPrimer *primer, CpScratch *sc, double *out) {
    if (threadIdx.x || blockIdx.x) return;
    ppf_run(primer, sc, st, seed, nticks, out);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int nticks = (argc > 2) ? atoi(argv[2]) : PPF_NUM_TICKS;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st;
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    ChunkPrimer *d_primer;
    CpScratch *d_sc;
    cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    cudaMalloc(&d_sc, sizeof(CpScratch));

    double *d_out;
    cudaMalloc(&d_out, sizeof(double) * (size_t)nticks * 6);

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_ppf<<<1, 1>>>(seed, nticks, d_st, d_primer, d_sc, d_out);
    cudaDeviceSynchronize();

    double *h_out = (double *)malloc(sizeof(double) * (size_t)nticks * 6);
    cudaMemcpy(h_out, d_out, sizeof(double) * (size_t)nticks * 6, cudaMemcpyDeviceToHost);

    for (int i = 0; i < nticks * 6; ++i) {
        u64 bits;
        memcpy(&bits, &h_out[i], 8);
        printf("%016llx\n", (unsigned long long)bits);
    }

    free(h_st);
    free(h_out);
    cudaFree(d_st);
    cudaFree(d_primer);
    cudaFree(d_sc);
    cudaFree(d_out);
    return 0;
}
