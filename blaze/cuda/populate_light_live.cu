/* CUDA driver for populate_light_live - same core as CPU. */
#include <cstdio>
#include <cstdlib>
#include "../core/populate_light_live.h"

__global__ void run_pll(i64 seed, McSinTable *st, World *w, u16 *blocks,
                        u8 *sky, u8 *blk, u8 *tmp_sky, u8 *tmp_blk,
                        CpScratch *sc, ChunkPrimer *primer, FoliageCoord *fol) {
    if (threadIdx.x || blockIdx.x) return;
    JavaRandom r;
    w->st = st;
    w->blocks = blocks;
    pll_run(w, sc, primer, &r, fol, seed, sky, blk, tmp_sky, tmp_blk);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);

    McSinTable *d_st = NULL;
    World *d_w = NULL;
    u16 *d_blocks = NULL;
    u8 *d_sky = NULL, *d_blk = NULL, *d_tmp_sky = NULL, *d_tmp_blk = NULL;
    CpScratch *d_sc = NULL;
    ChunkPrimer *d_primer = NULL;
    FoliageCoord *d_fol = NULL;

    if (cudaMalloc(&d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_w, sizeof(World)) != cudaSuccess ||
        cudaMalloc(&d_blocks, sizeof(u16) * W_N) != cudaSuccess ||
        cudaMalloc(&d_sky, W_N) != cudaSuccess ||
        cudaMalloc(&d_blk, W_N) != cudaSuccess ||
        cudaMalloc(&d_tmp_sky, W_N) != cudaSuccess ||
        cudaMalloc(&d_tmp_blk, W_N) != cudaSuccess ||
        cudaMalloc(&d_sc, sizeof(CpScratch)) != cudaSuccess ||
        cudaMalloc(&d_primer, sizeof(ChunkPrimer)) != cudaSuccess ||
        cudaMalloc(&d_fol, sizeof(FoliageCoord) * BT_MAX_FOLIAGE) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }

    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_pll<<<1, 1>>>(seed, d_st, d_w, d_blocks, d_sky, d_blk, d_tmp_sky, d_tmp_blk,
                      d_sc, d_primer, d_fol);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
        free(h_st);
        return 1;
    }

    u16 *h_blocks = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    cudaMemcpy(h_blocks, d_blocks, sizeof(u16) * (size_t)W_N, cudaMemcpyDeviceToHost);
    for (int i = 0; i < W_N; ++i)
        printf("%04x\n", (unsigned)h_blocks[i]);

    free(h_blocks);
    free(h_st);
    cudaFree(d_fol);
    cudaFree(d_primer);
    cudaFree(d_sc);
    cudaFree(d_tmp_blk);
    cudaFree(d_tmp_sky);
    cudaFree(d_blk);
    cudaFree(d_sky);
    cudaFree(d_blocks);
    cudaFree(d_w);
    cudaFree(d_st);
    return 0;
}
