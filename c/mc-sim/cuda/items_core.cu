/* CUDA driver for items_core. */
#include <cstdio>
#include <cstdlib>
#include "../core/items_core.h"

__global__ void run_ic(u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ic_run_battery(out);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u64 *d_out, h_out[9];
    cudaMalloc(&d_out, sizeof(h_out));
    run_ic<<<1, 1>>>(d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (int i = 0; i < 9; ++i) printf("%016llx\n", (unsigned long long)h_out[i]);
    cudaFree(d_out);
    return 0;
}
