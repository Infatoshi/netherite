/* CUDA driver for world_step - same core/world_step.h as the CPU path (SPEC: one
 * __host__ __device__ source compiled two ways, verified bitwise-identical). Single env on one
 * thread; the whole double-buffered multi-chunk world + env-CA scratch + player workspace +
 * chunk_provider working set live on the device heap. */
#include <cstdio>
#include <cstdlib>
#include "../core/world_step.h"

__global__ void run_ws(TwmWorld *w, WsScratch *s, ChunkPrimer *primer, CpScratch *sc,
                       const McSinTable *st, u64 seed, int nticks, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ws_run(w, s, primer, sc, st, seed, nticks, out);
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    int n_seeds = (argc > 1) ? 1 : 3;
    int nticks  = (argc > 2) ? atoi(argv[2]) : WS_NTICKS;
    size_t nlines = (size_t)nticks * WS_PERTICK + WS_TAIL;
    int si;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);

    McSinTable  *d_st = NULL;
    TwmWorld    *d_w = NULL;
    WsScratch   *d_s = NULL;
    ChunkPrimer *d_primer = NULL;
    CpScratch   *d_sc = NULL;
    u64         *d_out = NULL;

    if (cudaMalloc(&d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_w, sizeof(TwmWorld)) != cudaSuccess ||
        cudaMalloc(&d_s, sizeof(WsScratch)) != cudaSuccess ||
        cudaMalloc(&d_primer, sizeof(ChunkPrimer)) != cudaSuccess ||
        cudaMalloc(&d_sc, sizeof(CpScratch)) != cudaSuccess ||
        cudaMalloc(&d_out, sizeof(u64) * nlines) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        goto cleanup;
    }

    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    for (si = 0; si < n_seeds; ++si) {
        u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : k_seeds[si];
        u64 *h_out = (u64 *)malloc(sizeof(u64) * nlines);
        size_t i;

        run_ws<<<1, 1>>>(d_w, d_s, d_primer, d_sc, d_st, seed, nticks, d_out);
        {
            cudaError_t err = cudaDeviceSynchronize();
            if (err != cudaSuccess) {
                fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
                free(h_out);
                goto cleanup;
            }
        }
        cudaMemcpy(h_out, d_out, sizeof(u64) * nlines, cudaMemcpyDeviceToHost);
        for (i = 0; i < nlines; ++i)
            printf("%016llx\n", (unsigned long long)h_out[i]);
        free(h_out);
    }

cleanup:
    cudaFree(d_out);
    cudaFree(d_sc);
    cudaFree(d_primer);
    cudaFree(d_s);
    cudaFree(d_w);
    cudaFree(d_st);
    free(h_st);
    return 0;
}
