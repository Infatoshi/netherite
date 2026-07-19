/* CUDA driver for render_opt_obs_hook - same core as CPU path.
 * TcfScratch + chunk_provider scratch on device heap. */
#include <cstdio>
#include <cstdlib>
#include "../core/render_opt_obs_hook.h"

__global__ void run_rooh(Env *e, TcfAux *aux, TcfScratch *scratch, u64 seed,
                         ChunkPrimer *primer, CpScratch *sc, const McSinTable *st,
                         i32 *packed) {
    if (threadIdx.x || blockIdx.x) return;
    rooh_init_and_snapshot(e, aux, seed, primer, sc, st, scratch, packed);
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    int n_seeds = (argc > 1) ? 1 : 3;
    int si;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);

    McSinTable *d_st = NULL;
    Env *d_e = NULL;
    TcfAux *d_aux = NULL;
    TcfScratch *d_scratch = NULL;
    ChunkPrimer *d_primer = NULL;
    CpScratch *d_sc = NULL;
    i32 *d_packed = NULL;

    if (cudaMalloc(&d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_e, sizeof(Env)) != cudaSuccess ||
        cudaMalloc(&d_aux, sizeof(TcfAux)) != cudaSuccess ||
        cudaMalloc(&d_scratch, sizeof(TcfScratch)) != cudaSuccess ||
        cudaMalloc(&d_primer, sizeof(ChunkPrimer)) != cudaSuccess ||
        cudaMalloc(&d_sc, sizeof(CpScratch)) != cudaSuccess ||
        cudaMalloc(&d_packed, sizeof(i32) * ROOH_VOL) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        goto cleanup;
    }

    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    for (si = 0; si < n_seeds; ++si) {
        u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : k_seeds[si];
        i32 h_packed[ROOH_VOL];
        int i;

        run_rooh<<<1, 1>>>(d_e, d_aux, d_scratch, seed, d_primer, d_sc, d_st, d_packed);
        {
            cudaError_t err = cudaDeviceSynchronize();
            if (err != cudaSuccess) {
                fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
                goto cleanup;
            }
        }
        cudaMemcpy(h_packed, d_packed, sizeof(h_packed), cudaMemcpyDeviceToHost);
        for (i = 0; i < ROOH_VOL; ++i)
            printf("%08x\n", (unsigned)(u32)h_packed[i]);
    }

cleanup:
    cudaFree(d_packed);
    cudaFree(d_sc);
    cudaFree(d_primer);
    cudaFree(d_scratch);
    cudaFree(d_aux);
    cudaFree(d_e);
    cudaFree(d_st);
    free(h_st);
    return 0;
}
