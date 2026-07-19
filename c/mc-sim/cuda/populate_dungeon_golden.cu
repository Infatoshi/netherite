/* CUDA: same pdg_run as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/populate_dungeon_golden.h"

struct PdgCudaOut {
    int n;
    int idx[W_N];
    u16 blk[W_N];
};

__device__ void pdg_cuda_emit(int idx, u16 block, void *ctx) {
    PdgCudaOut *b = (PdgCudaOut *)ctx;
    if (b->n < W_N) {
        b->idx[b->n] = idx;
        b->blk[b->n] = block;
        b->n++;
    }
}

__global__ void run_pdg(i64 seed, const McSinTable *st, u16 *before, u16 *after,
                        CpScratch *sc, ChunkPrimer *primer, PdgCudaOut *out) {
    if (threadIdx.x || blockIdx.x) return;
    JavaRandom r;
    pdg_run(seed, pdg_cuda_emit, out, (McSinTable *)st, before, after, sc, primer, &r);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st;
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    u16 *d_before, *d_after;
    cudaMalloc(&d_before, sizeof(u16) * (size_t)W_N);
    cudaMalloc(&d_after, sizeof(u16) * (size_t)W_N);
    CpScratch *d_sc;
    cudaMalloc(&d_sc, sizeof(CpScratch));
    ChunkPrimer *d_primer;
    cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    PdgCudaOut *d_out;
    cudaMalloc(&d_out, sizeof(PdgCudaOut));

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_pdg<<<1, 1>>>(seed, d_st, d_before, d_after, d_sc, d_primer, d_out);
    {
        cudaError_t e0 = cudaGetLastError();
        cudaError_t e1 = cudaDeviceSynchronize();
        if (e0 != cudaSuccess || e1 != cudaSuccess) {
            fprintf(stderr, "run_pdg failed: launch=%s sync=%s\n",
                    cudaGetErrorString(e0), cudaGetErrorString(e1));
            return 1;
        }
    }

    PdgCudaOut h_out;
    cudaMemcpy(&h_out, d_out, sizeof(PdgCudaOut), cudaMemcpyDeviceToHost);

    if (h_out.n == W_N) {
        for (int i = 0; i < h_out.n; ++i)
            printf("%04x\n", (unsigned)h_out.blk[i]);
    } else {
        for (int i = 0; i < h_out.n; ++i)
            printf("%06x%04x\n", h_out.idx[i], (unsigned)h_out.blk[i]);
    }

    free(h_st);
    cudaFree(d_st);
    cudaFree(d_before);
    cudaFree(d_after);
    cudaFree(d_sc);
    cudaFree(d_primer);
    cudaFree(d_out);
    return 0;
}
