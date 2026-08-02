/* CUDA: same owr_run compose as cpu/overworld_region.c; full world on host.
 * Args: [seed [bcx [bcz]]] (defaults 0 0 0). stdout BYTE-IDENTICAL to the CPU driver. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/overworld_region.h"

__global__ void run_owr(i64 seed, int bcx, int bcz, const McSinTable *st, World *w, u16 *blocks,
                        u8 *sky, u8 *blk, u8 *tmp_sky, u8 *tmp_blk,
                        CpScratch *sc, ChunkPrimer *primer, FoliageCoord *fol,
                        u16 *mc_cur, u16 *mc_tmp, u16 *before_ca) {
    if (threadIdx.x || blockIdx.x) return;
    JavaRandom r;
    w->st = st;
    w->blocks = blocks;
    owr_run(w, sc, primer, &r, fol, seed, sky, blk, tmp_sky, tmp_blk, mc_cur, mc_tmp, before_ca,
            bcx, bcz);
}

static void run_region(i64 seed, int bcx, int bcz, McSinTable *d_st, World *d_w, u16 *d_blocks,
                       u8 *d_sky, u8 *d_blk, u8 *d_tmp_sky, u8 *d_tmp_blk,
                       CpScratch *d_sc, ChunkPrimer *d_primer, FoliageCoord *d_fol,
                       u16 *d_mc_cur, u16 *d_mc_tmp, u16 *d_before) {
    run_owr<<<1, 1>>>(seed, bcx, bcz, d_st, d_w, d_blocks, d_sky, d_blk, d_tmp_sky, d_tmp_blk,
                      d_sc, d_primer, d_fol, d_mc_cur, d_mc_tmp, d_before);
    cudaDeviceSynchronize();

    u16 *h_blocks = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    cudaMemcpy(h_blocks, d_blocks, sizeof(u16) * (size_t)W_N, cudaMemcpyDeviceToHost);
    printf("owr seed=%lld bcx=%d bcz=%d\n", (long long)seed, bcx, bcz);
    for (int i = 0; i < W_N; ++i)
        printf("%04x\n", (unsigned)h_blocks[i]);
    free(h_blocks);
}

int main(int argc, char **argv) {
    /* Default (no argv): seeds 0/9/19 - mirrors cpu/overworld_region.c so the
     * make verify-overworld_region target covers the liquid-stamp fail forever.
     * Argv override keeps single-seed forensics (and optional bcx/bcz). */
    static const i64 k_seeds[] = {0LL, 9LL, 19LL};
    int bcx = 0, bcz = 0;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st;
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    World *d_w;
    u16 *d_blocks, *d_mc_cur, *d_mc_tmp, *d_before;
    u8 *d_sky, *d_blk, *d_tmp_sky, *d_tmp_blk;
    CpScratch *d_sc;
    ChunkPrimer *d_primer;
    FoliageCoord *d_fol;

    cudaMalloc(&d_w, sizeof(World));
    cudaMalloc(&d_blocks, sizeof(u16) * (size_t)W_N);
    cudaMalloc(&d_sky, W_N);
    cudaMalloc(&d_blk, W_N);
    cudaMalloc(&d_tmp_sky, W_N);
    cudaMalloc(&d_tmp_blk, W_N);
    cudaMalloc(&d_mc_cur, sizeof(u16) * (size_t)PFS_N);
    cudaMalloc(&d_mc_tmp, sizeof(u16) * (size_t)PFS_N);
    cudaMalloc(&d_before, sizeof(u16) * (size_t)PFS_N);
    cudaMalloc(&d_sc, sizeof(CpScratch));
    cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    cudaMalloc(&d_fol, sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    if (argc > 1) {
        i64 seed = strtoll(argv[1], 0, 10);
        if (argc > 2) bcx = (int)strtol(argv[2], 0, 10);
        if (argc > 3) bcz = (int)strtol(argv[3], 0, 10);
        run_region(seed, bcx, bcz, d_st, d_w, d_blocks, d_sky, d_blk, d_tmp_sky, d_tmp_blk,
                   d_sc, d_primer, d_fol, d_mc_cur, d_mc_tmp, d_before);
    } else {
        for (int i = 0; i < 3; ++i)
            run_region(k_seeds[i], 0, 0, d_st, d_w, d_blocks, d_sky, d_blk, d_tmp_sky, d_tmp_blk,
                       d_sc, d_primer, d_fol, d_mc_cur, d_mc_tmp, d_before);
    }

    free(h_st);
    cudaFree(d_fol);
    cudaFree(d_primer);
    cudaFree(d_sc);
    cudaFree(d_before);
    cudaFree(d_mc_tmp);
    cudaFree(d_mc_cur);
    cudaFree(d_tmp_blk);
    cudaFree(d_tmp_sky);
    cudaFree(d_blk);
    cudaFree(d_sky);
    cudaFree(d_blocks);
    cudaFree(d_w);
    cudaFree(d_st);
    return 0;
}
