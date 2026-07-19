/* CUDA: same owf_run as CPU. Device heap for large structs; stack/heap limits match populate. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/overworld_full.h"

__global__ void run_owf(i64 seed, const McSinTable *st, World *w, u16 *blocks,
                        CpScratch *sc, ChunkPrimer *primer, FoliageCoord *fol) {
    if (threadIdx.x || blockIdx.x) return;
    JavaRandom r;
    w->st = st;
    w->blocks = blocks;
    owf_run(w, sc, primer, &r, fol, seed);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st;
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    World *d_w;
    cudaMalloc(&d_w, sizeof(World));
    u16 *d_blocks;
    cudaMalloc(&d_blocks, sizeof(u16) * (size_t)W_N);
    CpScratch *d_sc;
    cudaMalloc(&d_sc, sizeof(CpScratch));
    ChunkPrimer *d_primer;
    cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    FoliageCoord *d_fol;
    cudaMalloc(&d_fol, sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_owf<<<1, 1>>>(seed, d_st, d_w, d_blocks, d_sc, d_primer, d_fol);
    cudaDeviceSynchronize();

    u16 *h_blocks = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    cudaMemcpy(h_blocks, d_blocks, sizeof(u16) * (size_t)W_N, cudaMemcpyDeviceToHost);

    for (int i = 0; i < W_N; ++i)
        printf("%04x\n", (unsigned)h_blocks[i]);

    free(h_st); free(h_blocks);
    cudaFree(d_st); cudaFree(d_w); cudaFree(d_blocks); cudaFree(d_sc); cudaFree(d_primer); cudaFree(d_fol);
    return 0;
}
