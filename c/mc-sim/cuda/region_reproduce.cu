/* CUDA: the SAME core/region_reproduce.h rr_run(), single-thread (rt_fill/cp_provide_chunk
 * is a per-chunk SEQUENTIAL feature reading its own mid-loop writes), so it runs on ONE
 * thread and keeps CPU==CUDA bitwise. SIN_TABLE built on host and copied to device; the
 * several region tensors (RrScratch, a few hundred KB) live in cudaMalloc'd device memory,
 * NOT the thread stack. Device stack + malloc heap raised as in chunk_provider.cu (genlayer
 * recursion, cave addTunnel recursion, in-kernel malloc/free IntCache substitute). */
#include <cstdio>
#include <cstdlib>
#include "../core/region_reproduce.h"

__global__ void run_rr(const McSinTable *st, RrScratch *s) {
    if (threadIdx.x || blockIdx.x) return;
    rr_run(s, st);
}

int main(void) {
    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st; cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    RrScratch *d_s; cudaMalloc(&d_s, sizeof(RrScratch));

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_rr<<<1, 1>>>(d_st, d_s);
    cudaDeviceSynchronize();

    free(h_st);
    cudaFree(d_st); cudaFree(d_s);
    return 0;
}
