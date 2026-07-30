/* CUDA driver for projectile_motion - same core/projectile_motion.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/projectile_motion.h"

struct PmEmitCtx {
    u64 *buf;
    int n;
    int cap;
};

MC_HD static void pm_emit_device(u64 bits, void *ctx) {
    PmEmitCtx *c = (PmEmitCtx *)ctx;
    if (c->n < c->cap) c->buf[c->n++] = bits;
}

__global__ void run_projectile_motion(i64 seed, u64 *out, int *out_n, int out_cap) {
    if (threadIdx.x || blockIdx.x) return;
    PmEmitCtx ctx;
    ctx.buf = out;
    ctx.n = 0;
    ctx.cap = out_cap;
    pm_run_all(seed, pm_emit_device, &ctx);
    *out_n = ctx.n;
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    /* Worst case: 4 scenes * (48 ticks * 3 pos + 1 inGround flag). */
    const int out_cap = PM_NUM_SCENARIOS * (PM_MAX_TICKS * 3 + 1);
    u64 *host = (u64 *)malloc(sizeof(u64) * out_cap);
    u64 *d_out = NULL;
    int *d_n = NULL;
    int host_n = 0;

    cudaDeviceSetLimit(cudaLimitStackSize, 65536);

    if (cudaMalloc(&d_out, sizeof(u64) * out_cap) != cudaSuccess ||
        cudaMalloc(&d_n, sizeof(int)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        free(host);
        return 1;
    }

    run_projectile_motion<<<1, 1>>>(seed, d_out, d_n, out_cap);
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
