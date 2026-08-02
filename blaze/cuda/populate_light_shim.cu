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

static int run_seed(i64 seed,
                    McSinTable *d_st, u16 *d_blocks_a, u16 *d_blocks_b,
                    u8 *d_sky, u8 *d_blk, u8 *d_tmp_sky, u8 *d_tmp_blk,
                    CpScratch *d_sc, ChunkPrimer *d_primer, FoliageCoord *d_fol,
                    JavaRandom *d_r, int *d_count, int *d_idx, u16 *d_out_blk) {
    cudaMemset(d_count, 0, sizeof(int));
    run_populate_light_shim<<<1, 1>>>(seed, d_st, d_blocks_a, d_blocks_b, d_sky, d_blk,
                                      d_tmp_sky, d_tmp_blk, d_sc, d_primer, d_fol, d_r,
                                      d_count, d_idx, d_out_blk);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
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
    return 0;
}

int main(int argc, char **argv) {
    /* Seeds that place mushrooms under the stale light stub but not under the
     * fixpoint CA (or vice versa). Default seed 12345 is vacuous (0 diffs). */
    static const i64 k_seeds[] = {9LL, 19LL};

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
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    int rc = 0;
    if (argc > 1) {
        rc = run_seed(strtoll(argv[1], 0, 10),
                      d_st, d_blocks_a, d_blocks_b, d_sky, d_blk, d_tmp_sky, d_tmp_blk,
                      d_sc, d_primer, d_fol, d_r, d_count, d_idx, d_out_blk);
    } else {
        for (int i = 0; i < 2 && rc == 0; ++i)
            rc = run_seed(k_seeds[i],
                          d_st, d_blocks_a, d_blocks_b, d_sky, d_blk, d_tmp_sky, d_tmp_blk,
                          d_sc, d_primer, d_fol, d_r, d_count, d_idx, d_out_blk);
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
    return rc;
}
