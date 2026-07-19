/* CUDA driver for combat_math - SAME core/combat_math.h as the CPU path. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/combat_math.h"

__global__ void run_combat_math(int idx_sel, float *out) {
    if (threadIdx.x || blockIdx.x) return;
    int lo = (idx_sel < 0) ? 0 : idx_sel;
    int hi = (idx_sel < 0) ? MC_CM_NUM_SCENARIOS : idx_sel + 1;
    for (int idx = lo; idx < hi; ++idx)
        out[idx - lo] = mc_combat_scenario_damage(idx);
}

int main(int argc, char **argv) {
    int idx_sel = (argc > 1) ? atoi(argv[1]) : -1;
    int count = (idx_sel < 0) ? MC_CM_NUM_SCENARIOS : 1;

    float *d_out;
    cudaMalloc(&d_out, sizeof(float) * count);
    run_combat_math<<<1, 1>>>(idx_sel, d_out);
    cudaDeviceSynchronize();

    float *h_out = (float *)malloc(sizeof(float) * count);
    cudaMemcpy(h_out, d_out, sizeof(float) * count, cudaMemcpyDeviceToHost);

    for (int i = 0; i < count; ++i) {
        u32 bits; memcpy(&bits, &h_out[i], 4);
        printf("%08x\n", (unsigned)bits);
    }
    free(h_out);
    cudaFree(d_out);
    return 0;
}
