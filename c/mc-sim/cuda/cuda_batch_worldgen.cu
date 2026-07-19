/* CUDA batch driver: serial cp_provide_kernel launches; same 16-line hex as CPU. */
#include <cstdio>
#include <cstdlib>
#include "../core/cuda_batch_worldgen.h"
#include "../core/chunk_provider.h"

/* One cp_provide_chunk per <<<1,1>>> launch. chunk_provider.h's heavy device functions are
 * MC_NOINLINE (see mc.h), so this compiles in seconds rather than the old tens-of-minutes that
 * forced a cached-.o workaround. */
__global__ void cp_provide_kernel(i64 seed, int cx, int cz, const McSinTable *st,
                                  ChunkPrimer *primer, CpScratch *sc) {
    if (threadIdx.x || blockIdx.x) return;
    cp_provide_chunk(primer, sc, st, seed, cx, cz);
}

int main(void) {
    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    McSinTable *d_st = NULL;
    ChunkPrimer *d_primer = NULL;
    CpScratch *d_sc = NULL;
    ChunkPrimer *h_primer = NULL;
    int e;
    int rc = 1;

    mc_sin_table_init(h_st);

    if (cudaMalloc(&d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_primer, sizeof(ChunkPrimer)) != cudaSuccess ||
        cudaMalloc(&d_sc, sizeof(CpScratch)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        goto cleanup;
    }

    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    h_primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    if (!h_primer) goto cleanup;

    for (e = 0; e < CBW_N; ++e) {
        cudaError_t err;

        cp_provide_kernel<<<1, 1>>>(CBW_ENVS[e].seed, CBW_ENVS[e].cx, CBW_ENVS[e].cz,
                                    d_st, d_primer, d_sc);
        err = cudaDeviceSynchronize();
        if (err != cudaSuccess) {
            fprintf(stderr, "cuda sync env %d: %s\n", e, cudaGetErrorString(err));
            goto cleanup;
        }
        cudaMemcpy(h_primer, d_primer, sizeof(ChunkPrimer), cudaMemcpyDeviceToHost);
        printf("%016llx\n", (unsigned long long)CBW_ENVS[e].seed);
        printf("%016llx\n", (unsigned long long)(u64)(u32)CBW_ENVS[e].cx);
        printf("%016llx\n", (unsigned long long)(u64)(u32)CBW_ENVS[e].cz);
        printf("%016llx\n", (unsigned long long)cbw_primer_hash(h_primer));
    }

    rc = 0;

cleanup:
    free(h_primer);
    cudaFree(d_sc);
    cudaFree(d_primer);
    cudaFree(d_st);
    free(h_st);
    return rc;
}
