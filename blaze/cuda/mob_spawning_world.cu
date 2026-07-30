/* CUDA driver for mob_spawning_world - same core as CPU path. */
#include <cstdio>
#include <cstdlib>
#include "../core/mob_spawning_world.h"

__global__ void run_msw(MswScene *s, i64 tick, u8 *tmp_sky, u8 *tmp_blk) {
    if (threadIdx.x || blockIdx.x) return;
    msw_run(s, tick, tmp_sky, tmp_blk);
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    i64 tick = (argc > 2) ? (i64)strtoll(argv[2], 0, 10) : 100LL;
    MswScene *h = (MswScene *)malloc(sizeof(MswScene));
    u8 *h_tmp_sky = (u8 *)malloc(MSW_VOL);
    u8 *h_tmp_blk = (u8 *)malloc(MSW_VOL);
    MswScene *d;
    u8 *d_tmp_sky, *d_tmp_blk;
    int i;

    if (!h || !h_tmp_sky || !h_tmp_blk) return 1;

    msw_init(h, seed, h_tmp_sky, h_tmp_blk);

    cudaMalloc(&d, sizeof(MswScene));
    cudaMalloc(&d_tmp_sky, MSW_VOL);
    cudaMalloc(&d_tmp_blk, MSW_VOL);
    cudaMemcpy(d, h, sizeof(MswScene), cudaMemcpyHostToDevice);
    cudaMemcpy(d_tmp_sky, h_tmp_sky, MSW_VOL, cudaMemcpyHostToDevice);
    cudaMemcpy(d_tmp_blk, h_tmp_blk, MSW_VOL, cudaMemcpyHostToDevice);

    run_msw<<<1, 1>>>(d, tick, d_tmp_sky, d_tmp_blk);
    cudaDeviceSynchronize();
    cudaMemcpy(h, d, sizeof(MswScene), cudaMemcpyDeviceToHost);

    for (i = 0; i < h->n_decisions; ++i)
        printf("%016llx\n", (unsigned long long)h->decisions[i]);

    cudaFree(d_tmp_blk);
    cudaFree(d_tmp_sky);
    cudaFree(d);
    free(h_tmp_blk);
    free(h_tmp_sky);
    free(h);
    return 0;
}
