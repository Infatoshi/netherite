/* CUDA driver for end_portal - same core as CPU. */
#include <cstdio>
#include <cstdlib>
#include "../core/end_portal.h"

__global__ void run_ep(u64 seed, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    EpWorld w;
    w.seed = seed;
    ep_run(&w);
    ep_dump(&w, out);
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    u64 *d_out, h_out[EP_NOUT];
    cudaMalloc(&d_out, sizeof(h_out));
    run_ep<<<1, 1>>>(seed, d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (int i = 0; i < EP_NOUT; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);
    cudaFree(d_out);
    return 0;
}
