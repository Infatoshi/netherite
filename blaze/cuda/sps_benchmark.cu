#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "../core/sps_benchmark.h"

__device__ __noinline__ int maz_pf_find_astar_dev(
        const u16 *grid, int sx, int sy, int sz,
        int gx, int gy, int gz, int entity_height, int max_range,
        PfWork *work, PfResult *out) {
    return pf_find_astar(grid, sx, sy, sz, gx, gy, gz,
                         entity_height, max_range, work, out);
}

__global__ void sps_run_one_dev(
        Env *e, TcfAux *aux, u64 seed, ChunkPrimer *primer, CpScratch *sc,
        const McSinTable *st, TcfScratch *scratch, PfWork *work,
        CbtEmitLine *lines) {
    if (threadIdx.x || blockIdx.x) return;
    cbt_run_one(e, aux, seed, primer, sc, st, scratch, work, lines);
}

int main(int argc, char **argv) {
    struct timespec t0, t1;
    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    Env *d_e = NULL;
    TcfAux *d_aux = NULL;
    TcfScratch *d_scratch = NULL;
    PfWork *d_work = NULL;
    ChunkPrimer *d_primer = NULL;
    CpScratch *d_sc = NULL;
    McSinTable *d_st = NULL;
    CbtEmitLine *d_lines = NULL;
    CbtEmitLine h_last;
    u64 total_steps;
    double elapsed, sps;
    int n_envs = SPS_NENVS;
    int env, round;

    if (argc > 1)
        n_envs = (int)strtol(argv[1], 0, 10);
    if (n_envs < 1 || n_envs > SPS_NENVS)
        n_envs = SPS_NENVS;

    mc_sin_table_init(h_st);
    if (cudaMalloc(&d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_e, sizeof(Env)) != cudaSuccess ||
        cudaMalloc(&d_aux, sizeof(TcfAux)) != cudaSuccess ||
        cudaMalloc(&d_scratch, sizeof(TcfScratch)) != cudaSuccess ||
        cudaMalloc(&d_work, sizeof(PfWork)) != cudaSuccess ||
        cudaMalloc(&d_primer, sizeof(ChunkPrimer)) != cudaSuccess ||
        cudaMalloc(&d_sc, sizeof(CpScratch)) != cudaSuccess ||
        cudaMalloc(&d_lines, sizeof(CbtEmitLine) * SPS_NTICKS) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }

    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (round = 0; round < SPS_ROUNDS; ++round) {
        for (env = 0; env < n_envs; ++env) {
            sps_run_one_dev<<<1, 1>>>(
                d_e, d_aux, CBT_SEEDS[env], d_primer, d_sc, d_st,
                d_scratch, d_work, d_lines);
            cudaDeviceSynchronize();
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    cudaMemcpy(&h_last, d_lines + (SPS_NTICKS - 1), sizeof(CbtEmitLine),
               cudaMemcpyDeviceToHost);
    total_steps = (u64)n_envs * (u64)SPS_NTICKS * (u64)SPS_ROUNDS;
    elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    sps = elapsed > 0.0 ? (double)total_steps / elapsed : 0.0;

    printf("%016llx\n", (unsigned long long)total_steps);
    printf("%016llx\n", (unsigned long long)h_last.combined_hash);
    fprintf(stderr,
            "sps_benchmark cuda: envs=%d ticks=%d rounds=%d steps=%llu "
            "elapsed=%.3fs sps=%.0f\n",
            n_envs, SPS_NTICKS, SPS_ROUNDS,
            (unsigned long long)total_steps, elapsed, sps);

    cudaFree(d_lines);
    cudaFree(d_sc);
    cudaFree(d_primer);
    cudaFree(d_work);
    cudaFree(d_scratch);
    cudaFree(d_aux);
    cudaFree(d_e);
    cudaFree(d_st);
    free(h_st);
    return 0;
}
