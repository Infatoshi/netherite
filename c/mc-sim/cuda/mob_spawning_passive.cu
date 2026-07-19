/* CUDA driver for mob_spawning_passive - same core as CPU path. */
#include <cstdio>
#include <cstdlib>
#include "../core/mob_spawning_passive.h"

__global__ void run_msp(MspScene *scene, World *w, CpScratch *sc, ChunkPrimer *primer,
                        JavaRandom *r, FoliageCoord *fol, i64 seed, i64 tick) {
    if (threadIdx.x || blockIdx.x) return;
    msp_run(scene, w, sc, primer, r, fol, seed, tick);
}

int main(int argc, char **argv) {
    static const i64 k_seeds[] = {12345LL, 0LL, 7LL};
    i64 tick = (argc > 2) ? (i64)strtoll(argv[2], 0, 10) : 100LL;
    int n_seeds = 3;
    int si;

    if (argc > 1) n_seeds = 1;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);

    McSinTable *d_st = NULL;
    MspScene *d_scene = NULL;
    World *d_w = NULL;
    u16 *d_blocks = NULL;
    CpScratch *d_sc = NULL;
    ChunkPrimer *d_primer = NULL;
    FoliageCoord *d_fol = NULL;
    JavaRandom *d_r = NULL;

    if (cudaMalloc(&d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_scene, sizeof(MspScene)) != cudaSuccess ||
        cudaMalloc(&d_w, sizeof(World)) != cudaSuccess ||
        cudaMalloc(&d_blocks, sizeof(u16) * W_N) != cudaSuccess ||
        cudaMalloc(&d_sc, sizeof(CpScratch)) != cudaSuccess ||
        cudaMalloc(&d_primer, sizeof(ChunkPrimer)) != cudaSuccess ||
        cudaMalloc(&d_fol, sizeof(FoliageCoord) * BT_MAX_FOLIAGE) != cudaSuccess ||
        cudaMalloc(&d_r, sizeof(JavaRandom)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        free(h_st);
        return 1;
    }

    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    for (si = 0; si < n_seeds; ++si) {
        i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : k_seeds[si];
        World h_w;

        h_w.st = d_st;
        h_w.blocks = d_blocks;
        cudaMemcpy(d_w, &h_w, sizeof(World), cudaMemcpyHostToDevice);

        run_msp<<<1, 1>>>(d_scene, d_w, d_sc, d_primer, d_r, d_fol, seed, tick);
        {
            cudaError_t err = cudaDeviceSynchronize();
            if (err != cudaSuccess) {
                fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
                free(h_st);
                return 1;
            }
        }

        {
            MspScene h_scene;
            cudaMemcpy(&h_scene, d_scene, sizeof(MspScene), cudaMemcpyDeviceToHost);
            for (int i = 0; i < h_scene.n_records; ++i)
                printf("%016llx\n", (unsigned long long)h_scene.records[i]);
        }
    }

    free(h_st);
    cudaFree(d_r);
    cudaFree(d_fol);
    cudaFree(d_primer);
    cudaFree(d_sc);
    cudaFree(d_blocks);
    cudaFree(d_w);
    cudaFree(d_scene);
    cudaFree(d_st);
    return 0;
}
