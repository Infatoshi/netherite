/* CUDA driver for interact_blocks - SAME core/interact_blocks.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/interact_blocks.h"

__global__ void run_ib(u32 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ib_run_battery(out);
}

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    u32 *d_out;
    u32 h_out[IB_OUT];
    int i;
    (void)argc;
    (void)argv;
    cudaMalloc(&d_out, sizeof(h_out));
    run_ib<<<1, 1>>>(d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (i = 0; i < IB_OUT; ++i) emit_u32(h_out[i]);
    cudaFree(d_out);
    return 0;
}
