/* CUDA: identical surface layering from the same core/surface_blocks.h. Determinism smoke (1
 * thread); the batched per-env worldgen kernel comes after the math is proven bit-exact. The
 * 128KB ChunkPrimer lives in device global memory (passed by pointer), not on the stack. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/surface_blocks.h"

__global__ void run_sb(i64 seed, ChunkPrimer *primer) {
    if (threadIdx.x || blockIdx.x) return;
    sb_run(primer, seed);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    ChunkPrimer *d_primer;
    cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    run_sb<<<1, 1>>>(seed, d_primer);
    cudaDeviceSynchronize();

    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    cudaMemcpy(primer, d_primer, sizeof(ChunkPrimer), cudaMemcpyDeviceToHost);

    for (int i = 0; i < 65536; i++) {
        printf("%04x\n", (unsigned)primer->data[i]);
    }
    free(primer);
    cudaFree(d_primer);
    return 0;
}
