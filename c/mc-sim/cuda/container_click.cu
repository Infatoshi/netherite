/* CUDA driver for container_click. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/container_click.h"

__global__ void run_cc(u32 *out) {
    if (threadIdx.x || blockIdx.x) return;
    cc_run_battery(out);
}

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    u32 *d_out;
    u32 h_out[CC_OUT];
    int i;
    (void)argc;
    (void)argv;
    cudaMalloc(&d_out, sizeof(h_out));
    run_cc<<<1, 1>>>(d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (i = 0; i < CC_OUT; ++i) emit_u32(h_out[i]);
    cudaFree(d_out);
    return 0;
}
