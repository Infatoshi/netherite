/* CUDA driver for entities_world - SAME __host__ __device__ core as the CPU path (SPEC: one source,
 * two compiles, verified bitwise-identical). One env on one thread; the persistent multi-chunk world,
 * the double-buffered entity store, and all scratch (pathfinding window/work, collision list, spawn
 * scene) live on the device heap. Output buffer mirrors the CPU driver's flat u64 dump exactly. */
#include <cstdio>
#include <cstdlib>
#include "../core/entities_world.h"

__global__ void run_ew(EwState *st, EwScratch *sc, TwmWorld *w, TwmScratch *ts,
                       ChunkPrimer *pr, CpScratch *cs, const McSinTable *sn, u64 seed, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ew_run(st, sc, w, ts, pr, cs, sn, seed, out);
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    int n_seeds = (argc > 1) ? 1 : 3;
    int si;

    McSinTable *h_sn = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_sn);

    EwState *d_st = NULL; EwScratch *d_sc = NULL; TwmWorld *d_w = NULL; TwmScratch *d_ts = NULL;
    ChunkPrimer *d_pr = NULL; CpScratch *d_cs = NULL; McSinTable *d_sn = NULL; u64 *d_out = NULL;

    if (cudaMalloc(&d_st, sizeof(EwState)) != cudaSuccess ||
        cudaMalloc(&d_sc, sizeof(EwScratch)) != cudaSuccess ||
        cudaMalloc(&d_w, sizeof(TwmWorld)) != cudaSuccess ||
        cudaMalloc(&d_ts, sizeof(TwmScratch)) != cudaSuccess ||
        cudaMalloc(&d_pr, sizeof(ChunkPrimer)) != cudaSuccess ||
        cudaMalloc(&d_cs, sizeof(CpScratch)) != cudaSuccess ||
        cudaMalloc(&d_sn, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_out, sizeof(u64) * EW_NLINES) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        goto cleanup;
    }

    cudaMemcpy(d_sn, h_sn, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);   /* sm_120 per-thread max */

    for (si = 0; si < n_seeds; ++si) {
        u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : k_seeds[si];
        u64 *h_out = (u64 *)malloc(sizeof(u64) * EW_NLINES);
        int i;

        run_ew<<<1, 1>>>(d_st, d_sc, d_w, d_ts, d_pr, d_cs, d_sn, seed, d_out);
        {
            cudaError_t err = cudaDeviceSynchronize();
            if (err != cudaSuccess) {
                fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
                free(h_out);
                goto cleanup;
            }
        }
        cudaMemcpy(h_out, d_out, sizeof(u64) * EW_NLINES, cudaMemcpyDeviceToHost);
        for (i = 0; i < EW_NLINES; ++i)
            printf("%016llx\n", (unsigned long long)h_out[i]);
        free(h_out);
    }

cleanup:
    cudaFree(d_out); cudaFree(d_sn); cudaFree(d_cs); cudaFree(d_pr);
    cudaFree(d_ts); cudaFree(d_w); cudaFree(d_sc); cudaFree(d_st);
    free(h_sn);
    return 0;
}
