/* CUDA driver for ender_dragon_damage - same core/ender_dragon_damage.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include "../core/ender_dragon_damage.h"

__global__ void run_edd(u64 seed, int nticks, const McSinTable *st, EddWorld *world, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    edd_run(world, st, seed, nticks, out);
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    int nticks = (argc > 2) ? atoi(argv[2]) : EDD_NUM_TICKS;

    McSinTable h_st;
    mc_sin_table_init(&h_st);
    McSinTable *d_st;
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, &h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    EddWorld *d_world;
    u64 *d_out;
    cudaMalloc(&d_world, sizeof(EddWorld));
    cudaMalloc(&d_out, sizeof(u64) * (size_t)nticks * EDD_DUMP_FIELDS);

    run_edd<<<1, 1>>>(seed, nticks, d_st, d_world, d_out);
    cudaDeviceSynchronize();

    u64 *h_out = (u64 *)malloc(sizeof(u64) * (size_t)nticks * EDD_DUMP_FIELDS);
    cudaMemcpy(h_out, d_out, sizeof(u64) * (size_t)nticks * EDD_DUMP_FIELDS, cudaMemcpyDeviceToHost);

    for (int i = 0; i < nticks * EDD_DUMP_FIELDS; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);

    free(h_out);
    cudaFree(d_st);
    cudaFree(d_world);
    cudaFree(d_out);
    return 0;
}
