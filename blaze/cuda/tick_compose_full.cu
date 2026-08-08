/* CUDA driver for tick_compose_full - same core as CPU path.
 * TcfScratch + PfWork + chunk_provider scratch on device heap. */
#include <cstdio>
#include <cstdlib>
#include "../core/tick_compose_full.h"

struct TcfEmitCtx {
    u64 tick_bits;
    u64 combined_hash;
    u64 cur_bits;
};

__device__ __noinline__ int maz_pf_find_astar_dev(const u16 *grid, int sx, int sy, int sz,
                                                      int gx, int gy, int gz,
                                                      int entity_height, int max_range,
                                                      PfWork *work, PfResult *out) {
    return pf_find_astar(grid, sx, sy, sz, gx, gy, gz, entity_height, max_range, work, out);
}

__global__ void run_tcf(Env *e, TcfAux *aux, TcfScratch *scratch, u64 seed,
                        ChunkPrimer *primer, CpScratch *sc, const McSinTable *st,
                        PfWork *work, TcfEmitCtx *lines, int *n_lines) {
    int t;

    if (threadIdx.x || blockIdx.x) return;

    tcf_init_env(e, aux, seed, primer, sc, st, scratch);

    for (t = 0; t < TCF_NTICKS; ++t) {
        World *now;
        u64 blocks_h, light_h, ent_h, spawn_h;
        int n_dec = 0;

        tcf_tick_env(e, aux, primer, st, scratch, work, &n_dec);
        now = twc_now(e);
        blocks_h = twc_blocks_hash(now);
        light_h = tlc_light_hash(now);
        ent_h = te_entity_hash(&aux->te);
        spawn_h = ts_spawn_hash(scratch->decisions, n_dec);

        lines[t * 3 + 0].tick_bits = (u64)now->tick;
        lines[t * 3 + 1].combined_hash = tcf_combine_hash(blocks_h, light_h, ent_h, spawn_h);
        lines[t * 3 + 2].cur_bits = (u64)(u32)e->cur;
    }
    *n_lines = TCF_NTICKS * 3;
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
    PfWork *d_work = NULL;
    TcfEmitCtx *d_lines = NULL;
    int *d_n = NULL;

    if (cudaMalloc(&d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_e, sizeof(Env)) != cudaSuccess ||
        cudaMalloc(&d_aux, sizeof(TcfAux)) != cudaSuccess ||
        cudaMalloc(&d_scratch, sizeof(TcfScratch)) != cudaSuccess ||
        cudaMalloc(&d_primer, sizeof(ChunkPrimer)) != cudaSuccess ||
        cudaMalloc(&d_sc, sizeof(CpScratch)) != cudaSuccess ||
        cudaMalloc(&d_work, sizeof(PfWork)) != cudaSuccess ||
        cudaMalloc(&d_lines, sizeof(TcfEmitCtx) * TCF_NTICKS * 3) != cudaSuccess ||
        cudaMalloc(&d_n, sizeof(int)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        goto cleanup;
    }

    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    for (si = 0; si < n_seeds; ++si) {
        u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : k_seeds[si];
        TcfEmitCtx h_lines[TCF_NTICKS * 3];
        int n = 0;
        int i;

        run_tcf<<<1, 1>>>(d_e, d_aux, d_scratch, seed, d_primer, d_sc, d_st, d_work,
                          d_lines, d_n);
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
        for (i = 0; i < TCF_NTICKS; ++i) {
            printf("%016llx\n", (unsigned long long)h_lines[i * 3 + 0].tick_bits);
            printf("%016llx\n", (unsigned long long)h_lines[i * 3 + 1].combined_hash);
            printf("%016llx\n", (unsigned long long)h_lines[i * 3 + 2].cur_bits);
        }
    }

cleanup:
    cudaFree(d_n);
    cudaFree(d_lines);
    cudaFree(d_work);
    cudaFree(d_sc);
    cudaFree(d_primer);
    cudaFree(d_scratch);
    cudaFree(d_aux);
    cudaFree(d_e);
    cudaFree(d_st);
    free(h_st);
    return 0;
}
