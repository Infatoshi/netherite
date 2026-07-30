/* CUDA driver for player_vitals: single-thread kernel, stdout byte-identical to the CPU driver. */
#include <cstdio>
#include <cstdlib>
#include "../core/player_vitals.h"

__global__ void run_pv(i64 seed, i32 nticks, PvStats *out) {
    if (threadIdx.x || blockIdx.x) return;
    PvStats s;
    pv_init(&s);
    for (i32 t = 0; t < nticks; ++t) {
        pv_tape_tick(&s, seed, t);
        out[t] = s;
    }
}

int main(int argc, char **argv) {
    i64 seed   = (argc > 1) ? strtoll(argv[1], 0, 10) : 1LL;
    i32 nticks = (argc > 2) ? (i32)strtol(argv[2], 0, 10) : 400;
    PvStats *d_out, *h_out = (PvStats *)malloc(sizeof(PvStats) * (size_t)nticks);
    cudaMalloc(&d_out, sizeof(PvStats) * (size_t)nticks);
    run_pv<<<1, 1>>>(seed, nticks, d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(PvStats) * (size_t)nticks, cudaMemcpyDeviceToHost);
    for (i32 t = 0; t < nticks; ++t)
        printf("%d %.6f %.6f %d %.6f\n",
               h_out[t].foodLevel, h_out[t].saturation, h_out[t].exhaustion,
               h_out[t].foodTimer, h_out[t].health);
    cudaFree(d_out);
    free(h_out);
    return 0;
}
