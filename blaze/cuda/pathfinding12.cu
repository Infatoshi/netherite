/* CUDA driver for the verbatim 1.11.2 pathfinding port - same core/pathfinding12.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/pathfinding12.h"

struct Pf12EmitCtx { u32 *buf; int n; int cap; };

MC_HD static void pf12_emit_device(u32 v, void *ctx) {
    Pf12EmitCtx *c = (Pf12EmitCtx *)ctx;
    if (c->n < c->cap) c->buf[c->n++] = v;
}

__global__ void run_pf12(i64 seed, Pf12 *work, u32 *out, int *out_n, int out_cap) {
    if (threadIdx.x || blockIdx.x) return;
    Pf12EmitCtx ctx; ctx.buf = out; ctx.n = 0; ctx.cap = out_cap;
    pf12_run_all(seed, work, pf12_emit_device, &ctx);
    *out_n = ctx.n;
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    /* worst case: each case emits 1 + 3*maxpts + 1; cap generously. */
    const int out_cap = PF12_NUM_CASES * (2 + 3 * 512);
    u32 *host = (u32 *)malloc(sizeof(u32) * out_cap);
    u32 *d_out = NULL; Pf12 *d_work = NULL; int *d_n = NULL; int host_n = 0;

    cudaDeviceSetLimit(cudaLimitStackSize, 65536);

    if (cudaMalloc(&d_out, sizeof(u32) * out_cap) != cudaSuccess ||
        cudaMalloc(&d_work, sizeof(Pf12)) != cudaSuccess ||
        cudaMalloc(&d_n, sizeof(int)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        free(host);
        return 1;
    }

    run_pf12<<<1, 1>>>(seed, d_work, d_out, d_n, out_cap);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
        return 1;
    }

    cudaMemcpy(&host_n, d_n, sizeof(int), cudaMemcpyDeviceToHost);
    cudaMemcpy(host, d_out, sizeof(u32) * host_n, cudaMemcpyDeviceToHost);
    for (int i = 0; i < host_n; ++i) printf("%08x\n", (unsigned)host[i]);

    free(host);
    cudaFree(d_out); cudaFree(d_work); cudaFree(d_n);
    return 0;
}
