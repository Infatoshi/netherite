/* CUDA driver for explosion - SAME core/explosion.h as the CPU path. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/explosion.h"

struct ExEmitCtx {
    u64 *buf;
    int n;
    int cap;
};

MC_HD static void ex_emit_device(u64 bits, void *ctx) {
    ExEmitCtx *c = (ExEmitCtx *)ctx;
    if (c->n < c->cap) c->buf[c->n++] = bits;
}

__global__ void run_explosion(int sel, u64 *out, int *out_n, int out_cap) {
    if (threadIdx.x || blockIdx.x) return;
    ExEmitCtx ctx;
    ctx.buf = out;
    ctx.n = 0;
    ctx.cap = out_cap;
    u16 grid[EX_VOL];
    u8 bitset[EX_VOL];
    if (sel >= 0 && sel < EX_NUM_SCENARIOS)
        ex_run_scenario(sel, grid, bitset, ex_emit_device, &ctx);
    else
        ex_run_all(ex_emit_device, &ctx);
    *out_n = ctx.n;
}

int main(int argc, char **argv) {
    int sel = (argc > 1) ? atoi(argv[1]) : -1;
    /* worst case: 5 * (1 + EX_VOL + EX_NUM_ENTITIES) */
    const int out_cap = EX_NUM_SCENARIOS * (1 + EX_VOL + EX_NUM_ENTITIES);

    u64 *host = (u64 *)malloc(sizeof(u64) * out_cap);
    u64 *d_out = NULL;
    int *d_n = NULL;
    int host_n = 0;

    cudaDeviceSetLimit(cudaLimitStackSize, 256 * 1024);

    if (cudaMalloc(&d_out, sizeof(u64) * out_cap) != cudaSuccess ||
        cudaMalloc(&d_n, sizeof(int)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        free(host);
        return 1;
    }

    run_explosion<<<1, 1>>>(sel, d_out, d_n, out_cap);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
        cudaFree(d_out);
        cudaFree(d_n);
        free(host);
        return 1;
    }

    cudaMemcpy(&host_n, d_n, sizeof(int), cudaMemcpyDeviceToHost);
    cudaMemcpy(host, d_out, sizeof(u64) * host_n, cudaMemcpyDeviceToHost);

    for (int i = 0; i < host_n; ++i)
        printf("%016llx\n", (unsigned long long)host[i]);

    free(host);
    cudaFree(d_out);
    cudaFree(d_n);
    return 0;
}
