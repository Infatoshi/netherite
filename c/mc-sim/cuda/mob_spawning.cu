/* CUDA driver for mob_spawning - same core as CPU path. */
#include <cstdio>
#include <cstdlib>
#include "../core/mob_spawning.h"

__global__ void run_ms(MsScene *s, i64 tick) {
    if (threadIdx.x || blockIdx.x) return;
    ms_run(s, tick);
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    i64 tick = (argc > 2) ? (i64)strtoll(argv[2], 0, 10) : 100LL;
    MsScene h, *d;
    int i;

    ms_init_flat(&h, seed);
    cudaMalloc(&d, sizeof(MsScene));
    cudaMemcpy(d, &h, sizeof(MsScene), cudaMemcpyHostToDevice);
    run_ms<<<1, 1>>>(d, tick);
    cudaDeviceSynchronize();
    cudaMemcpy(&h, d, sizeof(MsScene), cudaMemcpyDeviceToHost);

    for (i = 0; i < h.n_decisions; ++i)
        printf("%016llx\n", (unsigned long long)h.decisions[i]);
    cudaFree(d);
    return 0;
}
