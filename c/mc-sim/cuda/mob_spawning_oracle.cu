/* CUDA driver for mob_spawning_oracle - same core as CPU path. */
#include <cstdio>
#include <cstdlib>
#include "../core/mob_spawning_oracle.h"

__global__ void run_mso(MsoOut *o) {
    if (threadIdx.x || blockIdx.x) return;
    mso_run(o);
}

int main(int argc, char **argv) {
    MsoOut h, *d;
    int i;
    (void)argc;
    (void)argv;
    h.n = 0;
    cudaMalloc(&d, sizeof(MsoOut));
    cudaMemcpy(d, &h, sizeof(MsoOut), cudaMemcpyHostToDevice);
    run_mso<<<1, 1>>>(d);
    cudaDeviceSynchronize();
    cudaMemcpy(&h, d, sizeof(MsoOut), cudaMemcpyDeviceToHost);
    for (i = 0; i < h.n; ++i)
        printf("%016llx\n", (unsigned long long)h.lines[i]);
    cudaFree(d);
    return 0;
}
