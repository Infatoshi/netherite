/* CUDA driver for entity_spine - same core/entity_spine.h as CPU. */
#include <cstdio>
#include "../core/entity_spine.h"

__global__ void run_es(u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    es_run(out);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u64 *d_out, h_out[ES_OUT];
    cudaMalloc(&d_out, sizeof(h_out));
    run_es<<<1, 1>>>(d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (int i = 0; i < ES_OUT; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);
    cudaFree(d_out);
    return 0;
}
