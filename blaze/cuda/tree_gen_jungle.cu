/* CUDA: same core/tree_gen_jungle.h as CPU; single-thread worldgen on device. */
#include <cstdio>
#include <cstdlib>
#include "../core/tree_gen_jungle.h"

__global__ void run_tgj(i64 seed, ChunkPrimer *primer, CpScratch *sc, McSinTable *st) {
    if (threadIdx.x || blockIdx.x) return;
    tgj_run(primer, sc, st, seed);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    ChunkPrimer *d_primer;
    CpScratch *d_sc;
    McSinTable *d_st;
    cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    cudaMalloc(&d_sc, sizeof(CpScratch));
    cudaMalloc(&d_st, sizeof(McSinTable));

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    free(h_st);

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_tgj<<<1, 1>>>(seed, d_primer, d_sc, d_st);
    cudaDeviceSynchronize();

    ChunkPrimer *h_p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    cudaMemcpy(h_p, d_primer, sizeof(ChunkPrimer), cudaMemcpyDeviceToHost);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)h_p->data[i]);

    free(h_p);
    cudaFree(d_primer);
    cudaFree(d_sc);
    cudaFree(d_st);
    return 0;
}
