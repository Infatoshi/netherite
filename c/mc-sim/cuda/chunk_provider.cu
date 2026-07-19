/* CUDA: the SAME core/chunk_provider.h composed pipeline, single-thread (worldgen is a per-chunk
 * SEQUENTIAL feature that reads its own mid-loop writes), so it runs on ONE thread and keeps
 * CPU==CUDA. The SIN_TABLE is built on the host and copied to the device. The device stack is
 * raised (genlayer recursion + cave addTunnel recursion). GenLayer getInts use a pre-allocated
 * bump arena in CpScratch (no in-kernel malloc), so no device-heap limit is needed - worldgen runs
 * on sm_86 where a raised malloc heap (and a 256KB stack) OOMs; 128KB stack fits the 3090. */
#include <cstdio>
#include <cstdlib>
#include "../core/chunk_provider.h"

__global__ void run_cp(i64 seed, const McSinTable *st, ChunkPrimer *primer, CpScratch *sc) {
    if (threadIdx.x || blockIdx.x) return;
    cp_provide_chunk(primer, sc, st, seed, 0, 0);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st; cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    ChunkPrimer *d_primer; cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    CpScratch *d_sc; cudaMalloc(&d_sc, sizeof(CpScratch));

    /* genlayer recursion + cave addTunnel recursion need more than the default device stack.
     * 128KB fits the 3090 (sm_86); 256KB reserves >24GB and OOMs. No malloc-heap limit: the
     * GenLayer getInts now use a pre-allocated bump arena in CpScratch (no in-kernel malloc). */
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_cp<<<1, 1>>>(seed, d_st, d_primer, d_sc);
    cudaDeviceSynchronize();

    ChunkPrimer *h_p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    cudaMemcpy(h_p, d_primer, sizeof(ChunkPrimer), cudaMemcpyDeviceToHost);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)h_p->data[i]);

    free(h_st); free(h_p);
    cudaFree(d_st); cudaFree(d_primer); cudaFree(d_sc);
    return 0;
}
