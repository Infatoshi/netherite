/* CUDA driver for tile_entity_brewing. */
#include <cstdio>
#include <cstdlib>
#include "../core/tile_entity_brewing.h"

__global__ void run_tb(u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    TeBrewing b;
    tb_run_dump(&b, out);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u64 *d_out, h_out[TB_OUT];
    cudaMalloc(&d_out, sizeof(h_out));
    run_tb<<<1, 1>>>(d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (int i = 0; i < TB_OUT; ++i) printf("%016llx\n", (unsigned long long)h_out[i]);
    cudaFree(d_out);
    return 0;
}
