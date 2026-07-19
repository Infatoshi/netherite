/* CUDA driver for pathfinding - same core/pathfinding.h as CPU; single-thread A*. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/pathfinding.h"

struct PfEmitCtx {
    u32 *buf;
    int n;
    int cap;
};

MC_HD static void pf_emit_device(u32 v, void *ctx) {
    PfEmitCtx *c = (PfEmitCtx *)ctx;
    if (c->n < c->cap) c->buf[c->n++] = v;
}

__global__ void run_pathfinding(i64 seed, PfWork *work, u32 *out, int *out_n, int out_cap) {
    if (threadIdx.x || blockIdx.x) return;
    PfEmitCtx ctx;
    ctx.buf = out;
    ctx.n = 0;
    ctx.cap = out_cap;
    pf_run_all(seed, work, pf_emit_device, &ctx);
    *out_n = ctx.n;
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    /* Worst case: 6 scenes * (1 len + 64 waypoints * 3 coords) = 1158 u32 lines. */
    const int out_cap = PF_NUM_SCENARIOS * (1 + PF_MAX_PATH * 3);
    u32 *host = (u32 *)malloc(sizeof(u32) * out_cap);
    u32 *d_out = NULL;
    PfWork *d_work = NULL;
    int *d_n = NULL;
    int host_n = 0;

    cudaDeviceSetLimit(cudaLimitStackSize, 65536);

    if (cudaMalloc(&d_out, sizeof(u32) * out_cap) != cudaSuccess ||
        cudaMalloc(&d_work, sizeof(PfWork)) != cudaSuccess ||
        cudaMalloc(&d_n, sizeof(int)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        free(host);
        return 1;
    }

    run_pathfinding<<<1, 1>>>(seed, d_work, d_out, d_n, out_cap);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
        cudaFree(d_out);
        cudaFree(d_work);
        cudaFree(d_n);
        free(host);
        return 1;
    }

    cudaMemcpy(&host_n, d_n, sizeof(int), cudaMemcpyDeviceToHost);
    cudaMemcpy(host, d_out, sizeof(u32) * host_n, cudaMemcpyDeviceToHost);

    for (int i = 0; i < host_n; ++i)
        printf("%08x\n", (unsigned)host[i]);

    free(host);
    cudaFree(d_out);
    cudaFree(d_work);
    cudaFree(d_n);
    return 0;
}
