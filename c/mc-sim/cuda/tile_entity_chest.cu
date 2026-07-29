/* CUDA driver for tile_entity_chest. */
#include <cstdio>
#include <cstdlib>
#include "../core/tile_entity_chest.h"

__global__ void run_tec(u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    tec_run_battery(out);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u64 *d_out, h_out[TEC_OUT];
    cudaMalloc(&d_out, sizeof(h_out));
    run_tec<<<1, 1>>>(d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (int i = 0; i < TEC_OUT; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);
    cudaFree(d_out);
    return 0;
}
