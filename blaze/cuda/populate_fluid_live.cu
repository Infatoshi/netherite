/* CUDA: same populate+fluid_flow compose as cpu/populate_fluid_live.c; full world on host. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/populate_fluid_live.h"

__global__ void run_pfl(i64 seed, const McSinTable *st, PopWorld *w, u16 *pb_blocks,
                        CpScratch *sc, ChunkPrimer *primer, FoliageCoord *fol,
                        u16 *mc_cur, u16 *mc_tmp, u16 *before_ca) {
    if (threadIdx.x || blockIdx.x) return;
    w->st = st;
    w->blocks = pb_blocks;
    JavaRandom r;
    pfl_run(w, sc, primer, &r, fol, seed, mc_cur, mc_tmp, before_ca);
}

static void run_seed(i64 seed, McSinTable *h_st, McSinTable *d_st, PopWorld *d_w, u16 *d_pb,
                     CpScratch *d_sc, ChunkPrimer *d_primer, FoliageCoord *d_fol,
                     u16 *d_mc_cur, u16 *d_mc_tmp, u16 *d_before) {
    run_pfl<<<1, 1>>>(seed, d_st, d_w, d_pb, d_sc, d_primer, d_fol, d_mc_cur, d_mc_tmp, d_before);
    cudaDeviceSynchronize();

    u16 *h_blocks = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    cudaMemcpy(h_blocks, d_pb, sizeof(u16) * (size_t)PFS_N, cudaMemcpyDeviceToHost);
    for (int i = 0; i < PFS_N; ++i)
        printf("%04x\n", (unsigned)h_blocks[i]);
    free(h_blocks);
}

int main(int argc, char **argv) {
    static const i64 k_seeds[] = {12345LL, 0LL, 7LL};

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st;
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    PopWorld *d_w;
    u16 *d_pb, *d_mc_cur, *d_mc_tmp, *d_before;
    CpScratch *d_sc;
    ChunkPrimer *d_primer;
    FoliageCoord *d_fol;

    cudaMalloc(&d_w, sizeof(PopWorld));
    cudaMalloc(&d_pb, sizeof(u16) * (size_t)PFS_N);
    cudaMalloc(&d_mc_cur, sizeof(u16) * (size_t)PFS_N);
    cudaMalloc(&d_mc_tmp, sizeof(u16) * (size_t)PFS_N);
    cudaMalloc(&d_before, sizeof(u16) * (size_t)PFS_N);
    cudaMalloc(&d_sc, sizeof(CpScratch));
    cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    cudaMalloc(&d_fol, sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    if (argc > 1) {
        run_seed(strtoll(argv[1], 0, 10), h_st, d_st, d_w, d_pb, d_sc, d_primer, d_fol,
                 d_mc_cur, d_mc_tmp, d_before);
    } else {
        int i;
        for (i = 0; i < 3; ++i)
            run_seed(k_seeds[i], h_st, d_st, d_w, d_pb, d_sc, d_primer, d_fol,
                     d_mc_cur, d_mc_tmp, d_before);
    }

    free(h_st);
    cudaFree(d_st);
    cudaFree(d_w);
    cudaFree(d_pb);
    cudaFree(d_mc_cur);
    cudaFree(d_mc_tmp);
    cudaFree(d_before);
    cudaFree(d_sc);
    cudaFree(d_primer);
    cudaFree(d_fol);
    return 0;
}
