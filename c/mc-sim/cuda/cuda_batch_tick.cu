/* CUDA batch driver: one block per env, T ticks each. Output matches cpu/cuda_batch_tick.c. */
#include <cstdio>
#include <cstdlib>
#include "../core/cuda_batch_tick.h"

__device__ __noinline__ int maz_pf_find_astar_dev(const u16 *grid, int sx, int sy, int sz,
                                                      int gx, int gy, int gz,
                                                      int entity_height, int max_range,
                                                      PfWork *work, PfResult *out) {
    return pf_find_astar(grid, sx, sy, sz, gx, gy, gz, entity_height, max_range, work, out);
}

__global__ void run_cbt(Env *envs, TcfAux *auxs, TcfScratch *scratchs, PfWork *works,
                        ChunkPrimer *primers, CpScratch *scs, const McSinTable *st,
                        const u64 *seeds, CbtEmitLine *lines, int n_envs) {
    int env = (int)blockIdx.x;

    if (threadIdx.x || env >= n_envs)
        return;

    cbt_run_one(&envs[env], &auxs[env], seeds[env], &primers[env], &scs[env], st,
                &scratchs[env], &works[env], &lines[env * CBT_NTICKS]);
}

int main(int argc, char **argv) {
    int n_envs = CBT_NENVS;
    int env;

    if (argc > 1)
        n_envs = (int)strtol(argv[1], 0, 10);
    if (n_envs < 1 || n_envs > CBT_NENVS)
        n_envs = CBT_NENVS;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);

    Env *d_envs = NULL;
    TcfAux *d_auxs = NULL;
    TcfScratch *d_scratchs = NULL;
    PfWork *d_works = NULL;
    ChunkPrimer *d_primers = NULL;
    CpScratch *d_scs = NULL;
    McSinTable *d_st = NULL;
    u64 *d_seeds = NULL;
    CbtEmitLine *d_lines = NULL;

    if (cudaMalloc(&d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_envs, sizeof(Env) * CBT_NENVS) != cudaSuccess ||
        cudaMalloc(&d_auxs, sizeof(TcfAux) * CBT_NENVS) != cudaSuccess ||
        cudaMalloc(&d_scratchs, sizeof(TcfScratch) * CBT_NENVS) != cudaSuccess ||
        cudaMalloc(&d_works, sizeof(PfWork) * CBT_NENVS) != cudaSuccess ||
        cudaMalloc(&d_primers, sizeof(ChunkPrimer) * CBT_NENVS) != cudaSuccess ||
        cudaMalloc(&d_scs, sizeof(CpScratch) * CBT_NENVS) != cudaSuccess ||
        cudaMalloc(&d_seeds, sizeof(u64) * CBT_NENVS) != cudaSuccess ||
        cudaMalloc(&d_lines, sizeof(CbtEmitLine) * CBT_NENVS * CBT_NTICKS) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        goto cleanup;
    }

    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaMemcpy(d_seeds, CBT_SEEDS, sizeof(u64) * CBT_NENVS, cudaMemcpyHostToDevice);
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_cbt<<<CBT_NENVS, 1>>>(d_envs, d_auxs, d_scratchs, d_works, d_primers, d_scs, d_st,
                              d_seeds, d_lines, n_envs);
    {
        cudaError_t err = cudaDeviceSynchronize();
        if (err != cudaSuccess) {
            fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
            goto cleanup;
        }
    }

    for (env = 0; env < n_envs; ++env) {
        CbtEmitLine h_lines[CBT_NTICKS];
        int t;

        cudaMemcpy(h_lines, d_lines + env * CBT_NTICKS,
                   sizeof(CbtEmitLine) * CBT_NTICKS, cudaMemcpyDeviceToHost);
        for (t = 0; t < CBT_NTICKS; ++t) {
            printf("%016llx\n", (unsigned long long)h_lines[t].tick_bits);
            printf("%016llx\n", (unsigned long long)h_lines[t].combined_hash);
            printf("%016llx\n", (unsigned long long)h_lines[t].cur_bits);
        }
    }

cleanup:
    cudaFree(d_lines);
    cudaFree(d_seeds);
    cudaFree(d_scs);
    cudaFree(d_primers);
    cudaFree(d_works);
    cudaFree(d_scratchs);
    cudaFree(d_auxs);
    cudaFree(d_envs);
    cudaFree(d_st);
    free(h_st);
    return 0;
}
