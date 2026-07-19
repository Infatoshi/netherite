/* CUDA driver for item_bucket_world. */
#include <cstdio>
#include <cstdlib>
#include "../core/item_bucket_world.h"

__global__ void run_ibw(u64 seed, u64 *out, World *w, u16 *cur, u16 *tmp) {
    if (threadIdx.x || blockIdx.x) return;
    ibw_run(seed, out, w, cur, tmp);
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    World *d_w = NULL;
    u16 *d_cur = NULL;
    u16 *d_tmp = NULL;
    u64 *d_out = NULL;
    u64 h_out[IBW_OUT];
    int n_seeds = (argc > 1) ? 1 : 3;
    int si;

    if (cudaMalloc(&d_w, sizeof(World)) != cudaSuccess ||
        cudaMalloc(&d_cur, sizeof(u16) * IBW_SLICE_VOL) != cudaSuccess ||
        cudaMalloc(&d_tmp, sizeof(u16) * IBW_SLICE_VOL) != cudaSuccess ||
        cudaMalloc(&d_out, sizeof(h_out)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }

    for (si = 0; si < n_seeds; ++si) {
        u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : k_seeds[si];
        int j;

        run_ibw<<<1, 1>>>(seed, d_out, d_w, d_cur, d_tmp);
        {
            cudaError_t err = cudaDeviceSynchronize();
            if (err != cudaSuccess) {
                fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
                cudaFree(d_out);
                cudaFree(d_tmp);
                cudaFree(d_cur);
                cudaFree(d_w);
                return 1;
            }
        }
        cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
        for (j = 0; j < IBW_OUT; ++j)
            printf("%016llx\n", (unsigned long long)h_out[j]);
    }

    cudaFree(d_out);
    cudaFree(d_tmp);
    cudaFree(d_cur);
    cudaFree(d_w);
    return 0;
}
