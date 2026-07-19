/* CUDA driver for enchant_damage_full - SAME core/enchant_damage_full.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include "../core/enchant_damage_full.h"

__global__ void run_enchant_damage_full(u32 *out) {
    if (threadIdx.x || blockIdx.x) return;
    int k = 0;
    edf_run_battery(out, &k);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u32 *d_out, h_out[EDF_NOUT];
    cudaMalloc(&d_out, sizeof(h_out));
    run_enchant_damage_full<<<1, 1>>>(d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (int i = 0; i < EDF_NOUT; ++i)
        printf("%08x\n", (unsigned)h_out[i]);
    cudaFree(d_out);
    return 0;
}
