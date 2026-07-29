/* CUDA driver for player_break: single-thread kernel, stdout byte-identical to CPU driver. */
#include <cstdio>
#include <cstdlib>
#include "../core/player_break.h"

__global__ void run_pb(int nticks, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    pb_run_battery(nticks, out);
}

int main(int argc, char **argv) {
    int nticks = (argc > 1) ? atoi(argv[1]) : PB_NTICKS;
    if (nticks < 1) nticks = PB_NTICKS;
    int nout = PB_NOUT(nticks);
    u64 *d_out = 0, *h_out = (u64 *)malloc(sizeof(u64) * (size_t)nout);
    if (!h_out) return 1;
    cudaMalloc(&d_out, sizeof(u64) * (size_t)nout);
    run_pb<<<1, 1>>>(nticks, d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(u64) * (size_t)nout, cudaMemcpyDeviceToHost);
    for (int i = 0; i < nout; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);
    cudaFree(d_out);
    free(h_out);
    return 0;
}
