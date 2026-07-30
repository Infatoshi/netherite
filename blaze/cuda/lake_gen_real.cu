/* CUDA: same core/lake_gen_real.h as CPU; single-thread worldgen on device. */
#include <cstdio>
#include <cstdlib>
#include "../core/lake_gen_real.h"

__global__ void run_lgr(i64 seed, ChunkPrimer *primer, LgrCtx *ctx) {
    if (threadIdx.x || blockIdx.x) return;
    lgr_run(primer, ctx, seed, 0, 0);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    ChunkPrimer *d_primer;
    LgrCtx *d_ctx;
    cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    cudaMalloc(&d_ctx, sizeof(LgrCtx));

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_lgr<<<1, 1>>>(seed, d_primer, d_ctx);
    cudaDeviceSynchronize();

    ChunkPrimer *h_p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    cudaMemcpy(h_p, d_primer, sizeof(ChunkPrimer), cudaMemcpyDeviceToHost);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)h_p->data[i]);

    free(h_p);
    cudaFree(d_primer);
    cudaFree(d_ctx);
    return 0;
}
