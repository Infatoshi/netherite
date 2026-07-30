/* CUDA driver for item_food_eat. */
#include <cstdio>
#include <cstdlib>
#include "../core/item_food_eat.h"

__global__ void run_ife(i64 seed, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ife_run(seed, out);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    u64 *d_out, h_out[IFE_OUT];
    cudaMalloc(&d_out, sizeof(h_out));
    run_ife<<<1, 1>>>(seed, d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (int i = 0; i < IFE_OUT; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);
    cudaFree(d_out);
    return 0;
}
