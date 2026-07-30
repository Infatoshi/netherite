/* CUDA driver for player_death: single-thread kernel, stdout byte-identical to the CPU driver. */
#include <cstdio>
#include <cstdlib>
#include "../core/player_death.h"

__global__ void run_pd(i64 seed, i32 nticks, PdState *out) {
    if (threadIdx.x || blockIdx.x) return;
    PdState s;
    pd_init(&s);
    for (i32 t = 0; t < nticks; ++t) {
        pd_tape_tick(&s, seed, t);
        out[t] = s;
    }
}

int main(int argc, char **argv) {
    i64 seed   = (argc > 1) ? strtoll(argv[1], 0, 10) : 1LL;
    i32 nticks = (argc > 2) ? (i32)strtol(argv[2], 0, 10) : 700;
    PdState *d_out, *h_out = (PdState *)malloc(sizeof(PdState) * (size_t)nticks);
    cudaMalloc(&d_out, sizeof(PdState) * (size_t)nticks);
    run_pd<<<1, 1>>>(seed, nticks, d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(PdState) * (size_t)nticks, cudaMemcpyDeviceToHost);
    for (i32 t = 0; t < nticks; ++t)
        printf("%d %.6f %.6f %d %.6f %d %d %d\n",
               h_out[t].pv.foodLevel, h_out[t].pv.saturation, h_out[t].pv.exhaustion,
               h_out[t].pv.foodTimer, h_out[t].pv.health,
               h_out[t].dead, h_out[t].deaths, h_out[t].death_time);
    cudaFree(d_out);
    free(h_out);
    return 0;
}
