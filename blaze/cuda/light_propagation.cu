/* CUDA driver for light_propagation - same core/light_propagation.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/light_propagation.h"

__global__ void run_light_propagation(i64 seed, u16 *blocks, u8 *sky, u8 *blk,
                                      u8 *tmp_sky, u8 *tmp_blk) {
    if (threadIdx.x || blockIdx.x) return;
    lp_init_scene(blocks, seed);
    for (int i = 0; i < LP_VOL; ++i) {
        sky[i] = 0;
        blk[i] = 0;
    }
    lp_propagate(sky, blk, tmp_sky, tmp_blk, blocks, 128);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    u16 *d_blocks = NULL;
    u8 *d_sky = NULL;
    u8 *d_blk = NULL;
    u8 *d_tmp_sky = NULL;
    u8 *d_tmp_blk = NULL;

    if (cudaMalloc(&d_blocks, sizeof(u16) * LP_VOL) != cudaSuccess ||
        cudaMalloc(&d_sky, LP_VOL) != cudaSuccess ||
        cudaMalloc(&d_blk, LP_VOL) != cudaSuccess ||
        cudaMalloc(&d_tmp_sky, LP_VOL) != cudaSuccess ||
        cudaMalloc(&d_tmp_blk, LP_VOL) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }

    run_light_propagation<<<1, 1>>>(seed, d_blocks, d_sky, d_blk, d_tmp_sky, d_tmp_blk);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
        cudaFree(d_blocks);
        cudaFree(d_sky);
        cudaFree(d_blk);
        cudaFree(d_tmp_sky);
        cudaFree(d_tmp_blk);
        return 1;
    }

    u8 *host_sky = (u8 *)malloc(LP_VOL);
    u8 *host_blk = (u8 *)malloc(LP_VOL);
    cudaMemcpy(host_sky, d_sky, LP_VOL, cudaMemcpyDeviceToHost);
    cudaMemcpy(host_blk, d_blk, LP_VOL, cudaMemcpyDeviceToHost);

    for (int i = 0; i < LP_VOL; ++i) {
        u64 bits = (u64)mc_light(host_sky[i], host_blk[i]);
        printf("%016llx\n", (unsigned long long)bits);
    }

    free(host_blk);
    free(host_sky);
    cudaFree(d_tmp_blk);
    cudaFree(d_tmp_sky);
    cudaFree(d_blk);
    cudaFree(d_sky);
    cudaFree(d_blocks);
    return 0;
}
