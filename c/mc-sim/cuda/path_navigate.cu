/* CUDA driver for path_navigate - same core/path_navigate.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include "../core/path_navigate.h"

__global__ void run_pn(u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    pn_run(out);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u64 *d_out = NULL, h_out[PN_OUT];
    if (cudaMalloc(&d_out, sizeof(h_out)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }
    run_pn<<<1, 1>>>(d_out);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
        cudaFree(d_out);
        return 1;
    }
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (int i = 0; i < PN_OUT; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);
    cudaFree(d_out);
    return 0;
}
