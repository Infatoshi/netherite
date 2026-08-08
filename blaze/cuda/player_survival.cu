/* CUDA driver for player_survival - same core/ header as CPU. Single-thread (worldgen + sequential
 * survival tick loop); device stack/heap limits raised for the chunk_provider worldgen recursion.
 * Big Chunk[] region lives in device global memory. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/player_survival.h"

__global__ void run_psv(i64 seed, int nticks, const McSinTable *st, ChunkPrimer *primer,
                        CpScratch *sc, Chunk *a, Chunk *b,
                        int levitation_amplifier,
                        int jump_boost_amplifier, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    psv_run_effect(
        a, b, primer, sc, st, seed, nticks,
        levitation_amplifier, jump_boost_amplifier, out);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int nticks = (argc > 2) ? atoi(argv[2]) : PSV_NTICKS;
    int levitation_amplifier = (argc > 3) ? atoi(argv[3]) : -1;
    int jump_boost_amplifier = (argc > 4) ? atoi(argv[4]) : -1;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st;
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    ChunkPrimer *d_primer;
    CpScratch *d_sc;
    Chunk *d_a, *d_b;
    u64 *d_out;
    cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    cudaMalloc(&d_sc, sizeof(CpScratch));
    cudaMalloc(&d_a, sizeof(Chunk) * PSV_NCHUNKS);
    cudaMalloc(&d_b, sizeof(Chunk) * PSV_NCHUNKS);
    cudaMalloc(&d_out, sizeof(u64) * (size_t)nticks * PSV_FIELDS);

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_psv<<<1, 1>>>(seed, nticks, d_st, d_primer, d_sc, d_a, d_b,
                      levitation_amplifier, jump_boost_amplifier, d_out);
    cudaDeviceSynchronize();

    u64 *h_out = (u64 *)malloc(sizeof(u64) * (size_t)nticks * PSV_FIELDS);
    cudaMemcpy(h_out, d_out, sizeof(u64) * (size_t)nticks * PSV_FIELDS, cudaMemcpyDeviceToHost);

    for (int i = 0; i < nticks * PSV_FIELDS; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);

    free(h_st); free(h_out);
    cudaFree(d_st); cudaFree(d_primer); cudaFree(d_sc);
    cudaFree(d_a); cudaFree(d_b); cudaFree(d_out);
    return 0;
}
