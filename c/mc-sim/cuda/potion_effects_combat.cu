/* CUDA driver for potion_effects_combat - same core as CPU path. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/potion_effects_combat.h"

struct EmitCtx {
    u64 *buf;
    int k;
};

__device__ static void d_emit(u64 bits, void *vctx) {
    EmitCtx *ctx = (EmitCtx *)vctx;
    ctx->buf[ctx->k++] = bits;
}

__global__ void run_pec(u64 *out, int *count) {
    if (threadIdx.x || blockIdx.x) return;
    EmitCtx ctx;
    ctx.buf = out;
    ctx.k = 0;
    pec_run_battery(d_emit, &ctx);
    *count = ctx.k;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u64 *d_out;
    int *d_count;
    cudaMalloc(&d_out, sizeof(u64) * MC_PEC_NOUT);
    cudaMalloc(&d_count, sizeof(int));
    run_pec<<<1, 1>>>(d_out, d_count);
    cudaDeviceSynchronize();

    int count = 0;
    cudaMemcpy(&count, d_count, sizeof(int), cudaMemcpyDeviceToHost);
    u64 *h_out = (u64 *)malloc(sizeof(u64) * count);
    cudaMemcpy(h_out, d_out, sizeof(u64) * count, cudaMemcpyDeviceToHost);

    for (int i = 0; i < count; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);

    free(h_out);
    cudaFree(d_out);
    cudaFree(d_count);
    return 0;
}
