/* CUDA driver for fluid_flow - same core/fluid_flow.h as CPU; single-thread CA (sequential). */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/fluid_flow.h"

__global__ void run_fluid_flow(i64 seed, u16 *out_wb, u16 *tmp_wb, u16 *out_ss, u16 *tmp_ss) {
    if (threadIdx.x || blockIdx.x) return;
    int spring_iters = 5 + (int)(seed % 6);
    ff_init_water_bucket(out_wb);
    ff_ca_run(out_wb, tmp_wb, FF_DIM_WB_X, FF_DIM_WB_Y, FF_DIM_WB_Z, 64);
    ff_init_spring_spread(out_ss);
    ff_ca_run(out_ss, tmp_ss, FF_DIM_SS_X, FF_DIM_SS_Y, FF_DIM_SS_Z, spring_iters);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int n_wb = FF_DIM_WB_X * FF_DIM_WB_Y * FF_DIM_WB_Z;
    int n_ss = FF_DIM_SS_X * FF_DIM_SS_Y * FF_DIM_SS_Z;
    int n = n_wb + n_ss;

    cudaDeviceSetLimit(cudaLimitStackSize, 65536);

    u16 *d_wb, *d_tmp_wb, *d_ss, *d_tmp_ss;
    if (cudaMalloc(&d_wb, sizeof(u16) * n_wb) != cudaSuccess ||
        cudaMalloc(&d_tmp_wb, sizeof(u16) * n_wb) != cudaSuccess ||
        cudaMalloc(&d_ss, sizeof(u16) * n_ss) != cudaSuccess ||
        cudaMalloc(&d_tmp_ss, sizeof(u16) * n_ss) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }

    run_fluid_flow<<<1, 1>>>(seed, d_wb, d_tmp_wb, d_ss, d_tmp_ss);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
        cudaFree(d_wb);
        cudaFree(d_tmp_wb);
        cudaFree(d_ss);
        cudaFree(d_tmp_ss);
        return 1;
    }

    u16 *host = (u16 *)malloc(sizeof(u16) * n);
    cudaMemcpy(host, d_wb, sizeof(u16) * n_wb, cudaMemcpyDeviceToHost);
    cudaMemcpy(host + n_wb, d_ss, sizeof(u16) * n_ss, cudaMemcpyDeviceToHost);

    for (int i = 0; i < n; ++i)
        printf("%04x\n", (unsigned)host[i]);

    free(host);
    cudaFree(d_wb);
    cudaFree(d_tmp_wb);
    cudaFree(d_ss);
    cudaFree(d_tmp_ss);
    return 0;
}
