/* CUDA: same core/ore_gen_natural_stone.h as CPU; single-thread worldgen on device. */
#include <cstdio>
#include <cstdlib>
#include "../core/ore_gen_natural_stone.h"

__global__ void run_ogns(i64 seed, const McSinTable *st, ChunkPrimer *primer, CvrScratch *ctx) {
    if (threadIdx.x || blockIdx.x) return;
    ogns_run(primer, ctx, seed, st);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st;
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    ChunkPrimer *d_primer;
    CvrScratch *d_ctx;
    cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    cudaMalloc(&d_ctx, sizeof(CvrScratch));

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_ogns<<<1, 1>>>(seed, d_st, d_primer, d_ctx);
    cudaDeviceSynchronize();

    ChunkPrimer *h_p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    cudaMemcpy(h_p, d_primer, sizeof(ChunkPrimer), cudaMemcpyDeviceToHost);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)h_p->data[i]);

    free(h_st);
    free(h_p);
    cudaFree(d_st);
    cudaFree(d_primer);
    cudaFree(d_ctx);
    return 0;
}
