/* CUDA: same core/smelting_recipes.h as CPU; output format must match exactly. */
#include <cstdio>
#include <cstdlib>
#include "../core/smelting_recipes.h"

__global__ void run(u32 *out) {
    if (threadIdx.x || blockIdx.x) return;
    sr_run_dump(out);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u32 *d_out;
    cudaMalloc(&d_out, sizeof(u32) * SR_OUT);
    run<<<1, 1>>>(d_out);
    cudaDeviceSynchronize();
    u32 *out = (u32 *)malloc(sizeof(u32) * SR_OUT);
    cudaMemcpy(out, d_out, sizeof(u32) * SR_OUT, cudaMemcpyDeviceToHost);
    for (int i = 0; i < SR_OUT; ++i)
        printf("%08x\n", (unsigned)out[i]);
    free(out);
    cudaFree(d_out);
    return 0;
}
