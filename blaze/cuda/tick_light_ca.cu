/* CUDA driver for tick_light_ca - same core as CPU path. */
#include <cstdio>
#include <cstdlib>
#include "../core/tick_light_ca.h"

struct TlcEmitCtx {
    u64 tick_bits;
    u64 light_hash;
    u64 cur_bits;
};

__global__ void run_tlc(Env *e, u64 seed, u16 *blocks, u8 *sky, u8 *blk,
                        u8 *tmp_sky, u8 *tmp_blk, TlcEmitCtx *lines, int *n_lines) {
    int t;
    if (threadIdx.x || blockIdx.x) return;
    tlc_init_env(e, seed);
    for (t = 0; t < TWC_NTICKS; ++t) {
        World *now;
        tlc_tick_env(e, blocks, sky, blk, tmp_sky, tmp_blk);
        now = twc_now(e);
        lines[t * 3 + 0].tick_bits = (u64)now->tick;
        lines[t * 3 + 1].light_hash = tlc_light_hash(now);
        lines[t * 3 + 2].cur_bits = (u64)(u32)e->cur;
    }
    *n_lines = TWC_NTICKS * 3;
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    int n_seeds = (argc > 1) ? 1 : 3;
    int si;

    Env *d_e = NULL;
    u16 *d_blocks = NULL;
    u8 *d_sky = NULL;
    u8 *d_blk = NULL;
    u8 *d_tmp_sky = NULL;
    u8 *d_tmp_blk = NULL;
    TlcEmitCtx *d_lines = NULL;
    int *d_n = NULL;

    if (cudaMalloc(&d_e, sizeof(Env)) != cudaSuccess ||
        cudaMalloc(&d_blocks, sizeof(u16) * TLC_SLICE_VOL) != cudaSuccess ||
        cudaMalloc(&d_sky, TLC_SLICE_VOL) != cudaSuccess ||
        cudaMalloc(&d_blk, TLC_SLICE_VOL) != cudaSuccess ||
        cudaMalloc(&d_tmp_sky, TLC_SLICE_VOL) != cudaSuccess ||
        cudaMalloc(&d_tmp_blk, TLC_SLICE_VOL) != cudaSuccess ||
        cudaMalloc(&d_lines, sizeof(TlcEmitCtx) * TWC_NTICKS * 3) != cudaSuccess ||
        cudaMalloc(&d_n, sizeof(int)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)512 * 1024);

    for (si = 0; si < n_seeds; ++si) {
        u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : k_seeds[si];
        TlcEmitCtx h_lines[TWC_NTICKS * 3];
        int n = 0;
        int i;

        run_tlc<<<1, 1>>>(d_e, seed, d_blocks, d_sky, d_blk, d_tmp_sky, d_tmp_blk, d_lines, d_n);
        {
            cudaError_t err = cudaDeviceSynchronize();
            if (err != cudaSuccess) {
                fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
                cudaFree(d_n);
                cudaFree(d_lines);
                cudaFree(d_tmp_blk);
                cudaFree(d_tmp_sky);
                cudaFree(d_blk);
                cudaFree(d_sky);
                cudaFree(d_blocks);
                cudaFree(d_e);
                return 1;
            }
        }
        cudaMemcpy(h_lines, d_lines, sizeof(h_lines), cudaMemcpyDeviceToHost);
        cudaMemcpy(&n, d_n, sizeof(int), cudaMemcpyDeviceToHost);
        (void)n;
        for (i = 0; i < TWC_NTICKS; ++i) {
            printf("%016llx\n", (unsigned long long)h_lines[i * 3 + 0].tick_bits);
            printf("%016llx\n", (unsigned long long)h_lines[i * 3 + 1].light_hash);
            printf("%016llx\n", (unsigned long long)h_lines[i * 3 + 2].cur_bits);
        }
    }

    cudaFree(d_n);
    cudaFree(d_lines);
    cudaFree(d_tmp_blk);
    cudaFree(d_tmp_sky);
    cudaFree(d_blk);
    cudaFree(d_sky);
    cudaFree(d_blocks);
    cudaFree(d_e);
    return 0;
}
