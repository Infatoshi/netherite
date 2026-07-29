/* CUDA batch driver: BRT_N region tensors in PARALLEL, one thread per env, each thread
 * with its own primer + scratch (per-env cudaMalloc'd arrays so threads never collide).
 * stdout is BYTE-IDENTICAL to cpu/batch_region_tensor.c (same header + %04x order) so
 * runner.py verifies CPU==CUDA element-wise. Throughput (wall-clock, blocks/sec,
 * chunks/sec for the FILL only, excluding dump) goes to STDERR so it is not diffed. */
#include <cstdio>
#include <cstdlib>
#include "../core/batch_region_tensor.h"

/* One thread per env: fill this env's slice of the shared output buffer. */
__global__ void brt_kernel(const BrtEnv *envs, const McSinTable *st, u16 *out_all,
                           ChunkPrimer *primers, CpScratch *scs) {
    int e = blockIdx.x * blockDim.x + threadIdx.x;
    if (e >= BRT_N) return;
    brt_fill_one(out_all, &envs[e], e, &primers[e], &scs[e], st);
}

int main(void) {
    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    u16 *h_out = (u16 *)malloc((size_t)BRT_N * BRT_VOL * sizeof(u16));
    McSinTable *d_st = NULL;
    BrtEnv *d_envs = NULL;
    u16 *d_out = NULL;
    ChunkPrimer *d_primers = NULL;
    CpScratch *d_scs = NULL;
    cudaEvent_t t0, t1;
    float ms = 0.0f;
    long total_blocks, total_chunk_gens = 0;
    int e;
    long i;
    int rc = 1;

    if (!h_st || !h_out) { fprintf(stderr, "host malloc failed\n"); goto cleanup; }
    mc_sin_table_init(h_st);

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    if (cudaMalloc(&d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_envs, sizeof(BRT_ENVS)) != cudaSuccess ||
        cudaMalloc(&d_out, (size_t)BRT_N * BRT_VOL * sizeof(u16)) != cudaSuccess ||
        cudaMalloc(&d_primers, (size_t)BRT_N * sizeof(ChunkPrimer)) != cudaSuccess ||
        cudaMalloc(&d_scs, (size_t)BRT_N * sizeof(CpScratch)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        goto cleanup;
    }

    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaMemcpy(d_envs, BRT_ENVS, sizeof(BRT_ENVS), cudaMemcpyHostToDevice);

    cudaEventCreate(&t0);
    cudaEventCreate(&t1);
    cudaEventRecord(t0);
    brt_kernel<<<1, BRT_N>>>(d_envs, d_st, d_out, d_primers, d_scs);
    cudaEventRecord(t1);
    {
        cudaError_t err = cudaDeviceSynchronize();
        if (err != cudaSuccess) {
            fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
            goto cleanup;
        }
    }
    cudaEventElapsedTime(&ms, t0, t1);

    cudaMemcpy(h_out, d_out, (size_t)BRT_N * BRT_VOL * sizeof(u16), cudaMemcpyDeviceToHost);

    /* stdout: byte-identical to the CPU driver. */
    for (e = 0; e < BRT_N; ++e) {
        const BrtEnv *env = &BRT_ENVS[e];
        const u16 *slice = h_out + (long)e * BRT_VOL;
        printf("env %d seed=%lld x0=%d y0=%d z0=%d\n",
               e, (long long)env->seed, env->x0, env->y0, env->z0);
        for (i = 0; i < BRT_VOL; ++i)
            printf("%04x\n", (unsigned)slice[i]);
    }

    /* stderr: throughput budget (fill only). */
    total_blocks = (long)BRT_N * BRT_VOL;
    for (e = 0; e < BRT_N; ++e) total_chunk_gens += brt_env_chunk_gens(&BRT_ENVS[e]);
    {
        double sec = (double)ms / 1000.0;
        double bps = (sec > 0.0) ? (double)total_blocks / sec : 0.0;
        double cps = (sec > 0.0) ? (double)total_chunk_gens / sec : 0.0;
        fprintf(stderr,
                "batch_region_tensor: B=%d dims=%dx%dx%d vol/env=%d total_blocks=%ld "
                "chunk_gens=%ld fill=%.4f ms  %.3e blocks/sec  %.3e chunks/sec\n",
                BRT_N, BRT_NX, BRT_NY, BRT_NZ, BRT_VOL, total_blocks, total_chunk_gens,
                (double)ms, bps, cps);
    }

    rc = 0;

cleanup:
    cudaFree(d_scs);
    cudaFree(d_primers);
    cudaFree(d_out);
    cudaFree(d_envs);
    cudaFree(d_st);
    free(h_out);
    free(h_st);
    return rc;
}
