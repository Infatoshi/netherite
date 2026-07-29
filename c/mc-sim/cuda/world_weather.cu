/* CUDA driver for world_weather - same core/world_weather.h as CPU. Output format must match. */
#include <cstdio>
#include <cstdlib>
#include "../core/world_weather.h"

__global__ void run_ww(i64 seed, i32 nticks, WwState *state, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ww_run(state, seed, nticks, out);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    i32 nticks = (argc > 2) ? (i32)strtol(argv[2], 0, 10) : WW_NTICKS;
    WwState *d_state;
    u64 *d_out, *h_out;
    i32 i, n;

    if (nticks < 1) nticks = WW_NTICKS;
    n = nticks * WW_FIELDS;
    h_out = (u64 *)malloc(sizeof(u64) * (size_t)n);
    if (!h_out) return 1;

    cudaMalloc(&d_state, sizeof(WwState));
    cudaMalloc(&d_out, sizeof(u64) * (size_t)n);
    run_ww<<<1, 1>>>(seed, nticks, d_state, d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(u64) * (size_t)n, cudaMemcpyDeviceToHost);

    for (i = 0; i < n; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);

    free(h_out);
    cudaFree(d_state);
    cudaFree(d_out);
    return 0;
}
