/* CUDA driver for projectile_entity_hit - same core/projectile_entity_hit.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/projectile_entity_hit.h"

struct PehEmitCtx {
    u64 *buf;
    int n;
    int cap;
};

MC_HD static void peh_emit_device(u64 bits, void *ctx) {
    PehEmitCtx *c = (PehEmitCtx *)ctx;
    if (c->n < c->cap) c->buf[c->n++] = bits;
}

__global__ void run_projectile_entity_hit(i64 seed, u64 *out, int *out_n, int out_cap) {
    if (threadIdx.x || blockIdx.x) return;
    PehEmitCtx ctx;
    ctx.buf = out;
    ctx.n = 0;
    ctx.cap = out_cap;
    peh_run_all(seed, peh_emit_device, &ctx);
    *out_n = ctx.n;
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    const int out_cap = PEH_NUM_SCENARIOS * 10;
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

    run_projectile_entity_hit<<<1, 1>>>(seed, d_out, d_n, out_cap);
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
