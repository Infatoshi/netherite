/* CUDA driver for tick_world_copy - same core as CPU path. */
#include <cstdio>
#include <cstdlib>
#include "../core/tick_world_copy.h"

struct TwcEmitCtx {
    u64 tick_bits;
    u64 block_hash;
    u64 cur_bits;
};

__global__ void run_twc(Env *e, u64 seed, TwcEmitCtx *lines, int *n_lines) {
    int t;
    if (threadIdx.x || blockIdx.x) return;
    twc_init_env(e, seed);
    for (t = 0; t < TWC_NTICKS; ++t) {
        World *now;
        twc_tick_env(e);
        now = twc_now(e);
        lines[t * 3 + 0].tick_bits = (u64)now->tick;
        lines[t * 3 + 1].block_hash = twc_blocks_hash(now);
        lines[t * 3 + 2].cur_bits = (u64)(u32)e->cur;
    }
    *n_lines = TWC_NTICKS * 3;
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    int n_seeds = (argc > 1) ? 1 : 3;
    int si;

    Env *d_e = NULL;
    TwcEmitCtx *d_lines = NULL;
    int *d_n = NULL;

    if (cudaMalloc(&d_e, sizeof(Env)) != cudaSuccess ||
        cudaMalloc(&d_lines, sizeof(TwcEmitCtx) * TWC_NTICKS * 3) != cudaSuccess ||
        cudaMalloc(&d_n, sizeof(int)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)512 * 1024);

    for (si = 0; si < n_seeds; ++si) {
        u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : k_seeds[si];
        TwcEmitCtx h_lines[TWC_NTICKS * 3];
        int n = 0;
        int i;

        run_twc<<<1, 1>>>(d_e, seed, d_lines, d_n);
        {
            cudaError_t err = cudaDeviceSynchronize();
            if (err != cudaSuccess) {
                fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
                cudaFree(d_n);
                cudaFree(d_lines);
                cudaFree(d_e);
                return 1;
            }
        }
        cudaMemcpy(h_lines, d_lines, sizeof(h_lines), cudaMemcpyDeviceToHost);
        cudaMemcpy(&n, d_n, sizeof(int), cudaMemcpyDeviceToHost);
        (void)n;
        for (i = 0; i < TWC_NTICKS; ++i) {
            printf("%016llx\n", (unsigned long long)h_lines[i * 3 + 0].tick_bits);
            printf("%016llx\n", (unsigned long long)h_lines[i * 3 + 1].block_hash);
            printf("%016llx\n", (unsigned long long)h_lines[i * 3 + 2].cur_bits);
        }
    }

    cudaFree(d_n);
    cudaFree(d_lines);
    cudaFree(d_e);
    return 0;
}
