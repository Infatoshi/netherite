/* CUDA driver for tick_spawn - same core as CPU path. */
#include <cstdio>
#include <cstdlib>
#include "../core/tick_spawn.h"

struct TsEmitCtx {
    u64 tick_bits;
    u64 spawn_hash;
    u64 cur_bits;
};

__global__ void run_ts(Env *e, TsAux *aux, u64 seed,
                       u16 *blocks, u8 *tmp_sky, u8 *tmp_blk,
                       u64 *decisions, TsEmitCtx *lines, int *n_lines) {
    int t;
    if (threadIdx.x || blockIdx.x) return;
    ts_init_env(e, aux, seed, blocks, tmp_sky, tmp_blk);
    for (t = 0; t < TWC_NTICKS; ++t) {
        World *now;
        int n_dec = 0;
        ts_tick_env(e, aux, blocks, decisions, &n_dec);
        now = twc_now(e);
        lines[t * 3 + 0].tick_bits = (u64)now->tick;
        lines[t * 3 + 1].spawn_hash = ts_spawn_hash(decisions, n_dec);
        lines[t * 3 + 2].cur_bits = (u64)(u32)e->cur;
    }
    *n_lines = TWC_NTICKS * 3;
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    int n_seeds = (argc > 1) ? 1 : 3;
    int si;

    Env *d_e = NULL;
    TsAux *d_aux = NULL;
    u16 *d_blocks = NULL;
    u8 *d_tmp_sky = NULL;
    u8 *d_tmp_blk = NULL;
    u64 *d_decisions = NULL;
    TsEmitCtx *d_lines = NULL;
    int *d_n = NULL;

    if (cudaMalloc(&d_e, sizeof(Env)) != cudaSuccess ||
        cudaMalloc(&d_aux, sizeof(TsAux)) != cudaSuccess ||
        cudaMalloc(&d_blocks, sizeof(u16) * TS_VOL) != cudaSuccess ||
        cudaMalloc(&d_tmp_sky, TS_VOL) != cudaSuccess ||
        cudaMalloc(&d_tmp_blk, TS_VOL) != cudaSuccess ||
        cudaMalloc(&d_decisions, sizeof(u64) * TS_MAX_DECISIONS) != cudaSuccess ||
        cudaMalloc(&d_lines, sizeof(TsEmitCtx) * TWC_NTICKS * 3) != cudaSuccess ||
        cudaMalloc(&d_n, sizeof(int)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)512 * 1024);

    for (si = 0; si < n_seeds; ++si) {
        u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : k_seeds[si];
        TsEmitCtx h_lines[TWC_NTICKS * 3];
        int n = 0;
        int i;

        run_ts<<<1, 1>>>(d_e, d_aux, seed, d_blocks, d_tmp_sky, d_tmp_blk,
                         d_decisions, d_lines, d_n);
        {
            cudaError_t err = cudaDeviceSynchronize();
            if (err != cudaSuccess) {
                fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
                goto cleanup;
            }
        }
        cudaMemcpy(h_lines, d_lines, sizeof(h_lines), cudaMemcpyDeviceToHost);
        cudaMemcpy(&n, d_n, sizeof(int), cudaMemcpyDeviceToHost);
        (void)n;
        for (i = 0; i < TWC_NTICKS; ++i) {
            printf("%016llx\n", (unsigned long long)h_lines[i * 3 + 0].tick_bits);
            printf("%016llx\n", (unsigned long long)h_lines[i * 3 + 1].spawn_hash);
            printf("%016llx\n", (unsigned long long)h_lines[i * 3 + 2].cur_bits);
        }
    }

cleanup:
    cudaFree(d_n);
    cudaFree(d_lines);
    cudaFree(d_decisions);
    cudaFree(d_tmp_blk);
    cudaFree(d_tmp_sky);
    cudaFree(d_blocks);
    cudaFree(d_aux);
    cudaFree(d_e);
    return 0;
}
