/* CUDA driver for inventory_stack_rules. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/inventory_stack_rules.h"

__global__ void run_isr(int idx, u32 *out, int full_battery) {
    if (threadIdx.x || blockIdx.x) return;
    if (full_battery)
        isr_run_battery(out);
    else
        isr_run_scenario(idx, out);
}

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    u32 *d_out;
    u32 h_out[ISR_OUT];
    int one = (argc > 1) ? atoi(argv[1]) : -1;

    cudaMalloc(&d_out, sizeof(h_out));
    if (one >= 0) {
        run_isr<<<1, 1>>>(one, d_out, 0);
        cudaDeviceSynchronize();
        u32 partial[ISR_FIELDS_PER];
        cudaMemcpy(partial, d_out, sizeof(partial), cudaMemcpyDeviceToHost);
        for (int i = 0; i < ISR_FIELDS_PER; ++i) emit_u32(partial[i]);
    } else {
        run_isr<<<1, 1>>>(0, d_out, 1);
        cudaDeviceSynchronize();
        cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
        for (int i = 0; i < ISR_OUT; ++i) emit_u32(h_out[i]);
    }
    cudaFree(d_out);
    return 0;
}
