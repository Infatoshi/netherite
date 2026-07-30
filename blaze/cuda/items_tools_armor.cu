/* CUDA driver for items_tools_armor. */
#include <cstdio>
#include <cstdlib>
#include "../core/items_tools_armor.h"

__global__ void run_ita(u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ita_run_battery(out);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u64 *d_out, h_out[ITA_NOUT];
    cudaMalloc(&d_out, sizeof(h_out));
    run_ita<<<1, 1>>>(d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (int i = 0; i < ITA_NOUT; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);
    cudaFree(d_out);
    return 0;
}
