/* CUDA driver for populate_light_shim - same core as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/populate_light_shim.h"

__global__ void run_populate_light_shim(i64 seed, McSinTable *st, u16 *blocks_a, u16 *blocks_b,
                                        u8 *sky, u8 *blk, u8 *tmp_sky, u8 *tmp_blk,
                                        CpScratch *sc, ChunkPrimer *primer, FoliageCoord *fol,
                                        JavaRandom *r, int *out_count, int *out_idx, u16 *out_blk) {
    if (threadIdx.x || blockIdx.x) return;
    PlsOutBuf ob;
    ob.n = 0;
    ob.idx = out_idx;
    ob.blk = out_blk;
    pls_run_mushroom_scene(seed, pls_emit_buf, &ob, st, blocks_a, blocks_b,
                           sky, blk, tmp_sky, tmp_blk, sc, primer, fol, r);
    *out_count = ob.n;
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);

    McSinTable *d_st = NULL;
    u16 *d_blocks_a = NULL, *d_blocks_b = NULL;
    u8 *d_sky = NULL, *d_blk = NULL, *d_tmp_sky = NULL, *d_tmp_blk = NULL;
    CpScratch *d_sc = NULL;
    ChunkPrimer *d_primer = NULL;
    FoliageCoord *d_fol = NULL;
    JavaRandom *d_r = NULL;
    int *d_count = NULL, *d_idx = NULL;
    u16 *d_out_blk = NULL;

    if (cudaMalloc(&d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_blocks_a, sizeof(u16) * W_N) != cudaSuccess ||
        cudaMalloc(&d_blocks_b, sizeof(u16) * W_N) != cudaSuccess ||
        cudaMalloc(&d_sky, W_N) != cudaSuccess ||
        cudaMalloc(&d_blk, W_N) != cudaSuccess ||
        cudaMalloc(&d_tmp_sky, W_N) != cudaSuccess ||
        cudaMalloc(&d_tmp_blk, W_N) != cudaSuccess ||
        cudaMalloc(&d_sc, sizeof(CpScratch)) != cudaSuccess ||
        cudaMalloc(&d_primer, sizeof(ChunkPrimer)) != cudaSuccess ||
        cudaMalloc(&d_fol, sizeof(FoliageCoord) * BT_MAX_FOLIAGE) != cudaSuccess ||
        cudaMalloc(&d_r, sizeof(JavaRandom)) != cudaSuccess ||
        cudaMalloc(&d_count, sizeof(int)) != cudaSuccess ||
        cudaMalloc(&d_idx, sizeof(int) * W_N) != cudaSuccess ||
        cudaMalloc(&d_out_blk, sizeof(u16) * W_N) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }

    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaMemset(d_count, 0, sizeof(int));

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_populate_light_shim<<<1, 1>>>(seed, d_st, d_blocks_a, d_blocks_b, d_sky, d_blk,
                                      d_tmp_sky, d_tmp_blk, d_sc, d_primer, d_fol, d_r,
                                      d_count, d_idx, d_out_blk);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
        free(h_st);
        return 1;
    }

    int count = 0;
    cudaMemcpy(&count, d_count, sizeof(int), cudaMemcpyDeviceToHost);
    if (count > W_N) count = W_N;

    if (count > 0) {
        int *h_idx = (int *)malloc(sizeof(int) * (size_t)count);
        u16 *h_blk = (u16 *)malloc(sizeof(u16) * (size_t)count);
        cudaMemcpy(h_idx, d_idx, sizeof(int) * (size_t)count, cudaMemcpyDeviceToHost);
        cudaMemcpy(h_blk, d_out_blk, sizeof(u16) * (size_t)count, cudaMemcpyDeviceToHost);
        for (int i = 0; i < count; ++i)
            printf("%06x%04x\n", h_idx[i], (unsigned)h_blk[i]);
        free(h_blk);
        free(h_idx);
    }

    free(h_st);
    cudaFree(d_out_blk);
    cudaFree(d_idx);
    cudaFree(d_count);
    cudaFree(d_r);
    cudaFree(d_fol);
    cudaFree(d_primer);
    cudaFree(d_sc);
    cudaFree(d_tmp_blk);
    cudaFree(d_tmp_sky);
    cudaFree(d_blk);
    cudaFree(d_sky);
    cudaFree(d_blocks_b);
    cudaFree(d_blocks_a);
    cudaFree(d_st);
    return 0;
}
