/* CUDA driver for enchant_table. Same core/enchant_table.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/enchant_table.h"

__global__ void run_et(u32 *out) {
    if (threadIdx.x || blockIdx.x) return;
    et_run_battery(out);
}

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    u32 *d_out;
    u32 h_out[ET_OUT];
    int i;
    (void)argc;
    (void)argv;
    cudaMalloc(&d_out, sizeof(h_out));
    run_et<<<1, 1>>>(d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (i = 0; i < ET_OUT; ++i)
        emit_u32(h_out[i]);
    cudaFree(d_out);
    return 0;
}
