/* CUDA structures integration: same st_run as CPU. Primer + CpScratch on device heap;
 * stack/heap limits match chunk_provider (genlayer malloc + cave recursion). */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/structures.h"

__global__ void run_st(i64 seed, const McSinTable *st, ChunkPrimer *primer, CpScratch *sc) {
    if (threadIdx.x || blockIdx.x) return;
    st_run(primer, sc, st, seed, 0, 0);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st;
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    ChunkPrimer *d_primer;
    CpScratch *d_sc;
    cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    cudaMalloc(&d_sc, sizeof(CpScratch));

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_st<<<1, 1>>>(seed, d_st, d_primer, d_sc);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
        return 1;
    }

    ChunkPrimer *h_p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    cudaMemcpy(h_p, d_primer, sizeof(ChunkPrimer), cudaMemcpyDeviceToHost);
    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)h_p->data[i]);

    free(h_st);
    free(h_p);
    cudaFree(d_st);
    cudaFree(d_primer);
    cudaFree(d_sc);
    return 0;
}
