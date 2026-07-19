/* CUDA driver for tick_world_halo - same core as CPU path (SPEC: one __host__ __device__
 * source compiled two ways, verified bitwise-identical). Single env on one thread; the whole
 * multi-chunk world + halo scratch + chunk_provider working set live on the device heap. */
#include <cstdio>
#include <cstdlib>
#include "../core/tick_world_halo.h"

#define TWH_NLINES (TWH_NTICKS + TWM_NCHUNKS + 4)   /* ticks + per-chunk + 4 evidence */

struct OutCtx { u64 *out; int n; };

__device__ static void twh_dev_emit(u64 a, u64 b, u64 c, void *ctx) {
    OutCtx *o = (OutCtx *)ctx;
    int i = o->n++;
    o->out[i * 3 + 0] = a;
    o->out[i * 3 + 1] = b;
    o->out[i * 3 + 2] = c;
}

__global__ void run_twh(TwmWorld *w, TwhScratch *s, ChunkPrimer *primer, CpScratch *sc,
                        const McSinTable *st, u64 seed, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    OutCtx o; o.out = out; o.n = 0;
    twh_run(w, s, primer, sc, st, seed, twh_dev_emit, &o);
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    int n_seeds = (argc > 1) ? 1 : 3;
    int si;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);

    McSinTable  *d_st = NULL;
    TwmWorld    *d_w = NULL;
    TwhScratch  *d_s = NULL;
    ChunkPrimer *d_primer = NULL;
    CpScratch   *d_sc = NULL;
    u64         *d_out = NULL;

    if (cudaMalloc(&d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_w, sizeof(TwmWorld)) != cudaSuccess ||
        cudaMalloc(&d_s, sizeof(TwhScratch)) != cudaSuccess ||
        cudaMalloc(&d_primer, sizeof(ChunkPrimer)) != cudaSuccess ||
        cudaMalloc(&d_sc, sizeof(CpScratch)) != cudaSuccess ||
        cudaMalloc(&d_out, sizeof(u64) * TWH_NLINES * 3) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        goto cleanup;
    }

    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    for (si = 0; si < n_seeds; ++si) {
        u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : k_seeds[si];
        u64 h_out[TWH_NLINES * 3];
        int i;

        run_twh<<<1, 1>>>(d_w, d_s, d_primer, d_sc, d_st, seed, d_out);
        {
            cudaError_t err = cudaDeviceSynchronize();
            if (err != cudaSuccess) {
                fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
                goto cleanup;
            }
        }
        cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
        for (i = 0; i < TWH_NLINES; ++i) {
            printf("%016llx\n", (unsigned long long)h_out[i * 3 + 0]);
            printf("%016llx\n", (unsigned long long)h_out[i * 3 + 1]);
            printf("%016llx\n", (unsigned long long)h_out[i * 3 + 2]);
        }
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
