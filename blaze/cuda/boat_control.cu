/* CUDA driver for boat_control. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/boat_control.h"

__global__ void run_bc(McSinTable st, u32 *out) {
    if (threadIdx.x || blockIdx.x) return;
    bc_run_battery(&st, out);
}

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    McSinTable st;
    u32 *d_out;
    u32 h_out[BC_OUT];
    int i;
    (void)argc;
    (void)argv;
    mc_sin_table_init(&st);
    cudaMalloc(&d_out, sizeof(h_out));
    run_bc<<<1, 1>>>(st, d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (i = 0; i < BC_OUT; ++i) emit_u32(h_out[i]);
    cudaFree(d_out);
    return 0;
}
