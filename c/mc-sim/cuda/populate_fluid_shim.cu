/* CUDA: same populate+fluid_flow compose as cpu/populate_fluid_shim.c; delta emitted on host. */
#include <cstdio>
#include <cstdlib>
#include "../core/populate_fluid_shim.h"

static void emit_delta(int idx, u16 before, u16 after, void *ctx) {
    (void)ctx;
    printf("%06x %04x %04x\n", idx, (unsigned)before, (unsigned)after);
}

__global__ void run_pfs(i64 seed, const McSinTable *st, PopWorld *w, u16 *pb_blocks,
                        CpScratch *sc, ChunkPrimer *primer, FoliageCoord *fol,
                        u16 *mc_cur, u16 *mc_tmp, u16 *before_ca) {
    if (threadIdx.x || blockIdx.x) return;
    w->st = st;
    w->blocks = pb_blocks;
    JavaRandom r;
    pfs_fluid_pass(w, sc, primer, &r, fol, seed, mc_cur, mc_tmp, before_ca);
}

static void run_seed(i64 seed, McSinTable *h_st, McSinTable *d_st, PopWorld *d_w, u16 *d_pb,
                     CpScratch *d_sc, ChunkPrimer *d_primer, FoliageCoord *d_fol,
                     u16 *d_mc_cur, u16 *d_mc_tmp, u16 *d_before) {
    run_pfs<<<1, 1>>>(seed, d_st, d_w, d_pb, d_sc, d_primer, d_fol, d_mc_cur, d_mc_tmp, d_before);
    cudaDeviceSynchronize();

    u16 *h_mc = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    u16 *h_before = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    cudaMemcpy(h_mc, d_mc_cur, sizeof(u16) * (size_t)PFS_N, cudaMemcpyDeviceToHost);
    cudaMemcpy(h_before, d_before, sizeof(u16) * (size_t)PFS_N, cudaMemcpyDeviceToHost);
    pfs_emit_deltas(h_mc, h_before, emit_delta, NULL);
    free(h_before);
    free(h_mc);
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
