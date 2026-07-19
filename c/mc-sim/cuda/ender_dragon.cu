/* CUDA driver for ender_dragon - same core/ender_dragon.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include "../core/ender_dragon.h"

__global__ void run_ed(u64 seed, int nticks, const McSinTable *st, EdArena *arena, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ed_run(arena, st, seed, nticks, out);
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    int nticks = (argc > 2) ? atoi(argv[2]) : ED_NUM_TICKS;

    McSinTable h_st;
    mc_sin_table_init(&h_st);
    McSinTable *d_st;
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, &h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    EdArena *d_arena;
    u64 *d_out;
    cudaMalloc(&d_arena, sizeof(EdArena));
    cudaMalloc(&d_out, sizeof(u64) * (size_t)nticks * ED_DUMP_FIELDS);

    run_ed<<<1, 1>>>(seed, nticks, d_st, d_arena, d_out);
    cudaDeviceSynchronize();

    u64 *h_out = (u64 *)malloc(sizeof(u64) * (size_t)nticks * ED_DUMP_FIELDS);
    cudaMemcpy(h_out, d_out, sizeof(u64) * (size_t)nticks * ED_DUMP_FIELDS, cudaMemcpyDeviceToHost);

    for (int i = 0; i < nticks * ED_DUMP_FIELDS; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);

    free(h_out);
    cudaFree(d_st);
    cudaFree(d_arena);
    cudaFree(d_out);
    return 0;
}
