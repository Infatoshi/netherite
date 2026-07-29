/* CUDA driver for nether_portal - SAME core/nether_portal.h as the CPU path. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/nether_portal.h"

static void emit_u32(u32 v, void *ctx) {
    (void)ctx;
    printf("%08x\n", (unsigned)v);
}

static void emit_f32(float v, void *ctx) {
    u32 bits;
    (void)ctx;
    memcpy(&bits, &v, 4);
    printf("%08x\n", (unsigned)bits);
}

static void emit_f64(double v, void *ctx) {
    u64 bits;
    (void)ctx;
    memcpy(&bits, &v, 8);
    printf("%016llx\n", (unsigned long long)bits);
}

__global__ void run_nether_portal(int idx_sel, NpScenarioResult *results, int *count_out) {
    if (threadIdx.x || blockIdx.x) return;
    int lo = (idx_sel < 0) ? 0 : idx_sel;
    int hi = (idx_sel < 0) ? NP_NUM_SCENARIOS : idx_sel + 1;
    *count_out = 0;
    for (int idx = lo; idx < hi; ++idx) {
        np_run_scenario(idx, &results[idx - lo]);
        (*count_out)++;
    }
}

int main(int argc, char **argv) {
    int idx_sel = (argc > 1) ? atoi(argv[1]) : -1;
    int lo = (idx_sel < 0) ? 0 : idx_sel;
    int hi = (idx_sel < 0) ? NP_NUM_SCENARIOS : idx_sel + 1;
    int nsc = hi - lo;

    NpScenarioResult *d_res, *h_res = (NpScenarioResult *)malloc(sizeof(NpScenarioResult) * nsc);
    int *d_count, h_count = 0;
    cudaMalloc(&d_res, sizeof(NpScenarioResult) * nsc);
    cudaMalloc(&d_count, sizeof(int));
    run_nether_portal<<<1, 1>>>(idx_sel, d_res, d_count);
    cudaDeviceSynchronize();
    cudaMemcpy(h_res, d_res, sizeof(NpScenarioResult) * nsc, cudaMemcpyDeviceToHost);
    cudaMemcpy(&h_count, d_count, sizeof(int), cudaMemcpyDeviceToHost);

    for (int s = 0; s < h_count; ++s)
        np_emit_scenario(lo + s, &h_res[s], emit_u32, emit_f32, emit_f64, NULL);

    free(h_res);
    cudaFree(d_res);
    cudaFree(d_count);
    return 0;
}
