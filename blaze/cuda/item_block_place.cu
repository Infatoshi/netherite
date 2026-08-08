/* CUDA driver for item_block_place - SAME core/item_block_place.h as the CPU path. */
#include <cstdio>
#include <cstdlib>
#include "../core/item_block_place.h"

__global__ void run_item_block_place(int idx_sel, u32 *out) {
    if (threadIdx.x || blockIdx.x) return;
    if (idx_sel < 0) {
        ibp_run(out);
    } else {
        out[0] = ibp_case_word(idx_sel);
    }
}

int main(int argc, char **argv) {
    int idx_sel = (argc > 1) ? atoi(argv[1]) : -1;
    int count = (idx_sel < 0) ? IBP_NUM_CASES : 1;
    u32 *d_out = NULL;
    u32 *h_out = NULL;
    int i;

    if (cudaMalloc(&d_out, sizeof(u32) * (size_t)count) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }
    h_out = (u32 *)malloc(sizeof(u32) * (size_t)count);
    if (!h_out) {
        cudaFree(d_out);
        return 1;
    }

    run_item_block_place<<<1, 1>>>(idx_sel, d_out);
    {
        cudaError_t err = cudaDeviceSynchronize();
        if (err != cudaSuccess) {
            fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
            free(h_out);
            cudaFree(d_out);
            return 1;
        }
    }
    cudaMemcpy(h_out, d_out, sizeof(u32) * (size_t)count, cudaMemcpyDeviceToHost);
    for (i = 0; i < count; ++i)
        printf("%08x\n", (unsigned)h_out[i]);

    free(h_out);
    cudaFree(d_out);
    return 0;
}
