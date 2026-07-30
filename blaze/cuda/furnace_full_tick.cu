/* CUDA driver for furnace_full_tick - same core/furnace_full_tick.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include "../core/furnace_full_tick.h"

__global__ void run_fft(u64 seed, int nticks, FftFurnace *furn, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    fft_run(furn, seed, nticks, out);
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    int nticks = (argc > 2) ? atoi(argv[2]) : FFT_NUM_TICKS;
    FftFurnace *d_furn;
    u64 *d_out;
    u64 *h_out = (u64 *)malloc(sizeof(u64) * (size_t)nticks * FFT_DUMP_FIELDS);
    int i;

    cudaMalloc(&d_furn, sizeof(FftFurnace));
    cudaMalloc(&d_out, sizeof(u64) * (size_t)nticks * FFT_DUMP_FIELDS);
    run_fft<<<1, 1>>>(seed, nticks, d_furn, d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(u64) * (size_t)nticks * FFT_DUMP_FIELDS, cudaMemcpyDeviceToHost);

    for (i = 0; i < nticks * FFT_DUMP_FIELDS; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);

    free(h_out);
    cudaFree(d_furn);
    cudaFree(d_out);
    return 0;
}
