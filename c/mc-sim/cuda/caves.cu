/* CUDA: SAME core/caves.h as the CPU path. Cave carving is a per-chunk SEQUENTIAL feature that
 * reads its own mid-loop writes, so it runs on ONE thread (correct, and keeps CPU==CUDA). The
 * SIN_TABLE is built on the host and copied to the device so its bytes match the CPU table. The
 * device stack limit is raised because addTunnel recurses (genlayer_biomes.cu did the same). */
#include <cstdio>
#include <cstdlib>
#include "../core/caves.h"

__global__ void run_caves(i64 seed, const McSinTable *st, ChunkPrimer *p) {
    if (threadIdx.x || blockIdx.x) return;
    for (int i = 0; i < 65536; ++i) p->data[i] = CV_STONE;
    cv_generate(p, seed, 0, 0, st);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st; cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    ChunkPrimer *d_p; cudaMalloc(&d_p, sizeof(ChunkPrimer));

    /* addTunnel recursion needs more than the tiny default device stack. */
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_caves<<<1, 1>>>(seed, d_st, d_p);
    cudaDeviceSynchronize();

    ChunkPrimer *h_p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    cudaMemcpy(h_p, d_p, sizeof(ChunkPrimer), cudaMemcpyDeviceToHost);

    for (int idx = 0; idx < 65536; ++idx)
        printf("%04x\n", (unsigned)h_p->data[idx]);

    free(h_st); free(h_p);
    cudaFree(d_st); cudaFree(d_p);
    return 0;
}
