/* CUDA driver for tile_entity_spawner - same core/tile_entity_spawner.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include "../core/tile_entity_spawner.h"

__global__ void run_tes(u64 seed, int nticks, TeSpawnerScene *scene, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    tes_run(scene, seed, nticks, out);
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    int nticks = (argc > 2) ? atoi(argv[2]) : TES_NUM_TICKS;
    TeSpawnerScene *d_scene;
    u64 *d_out;
    u64 *h_out = (u64 *)malloc(sizeof(u64) * (size_t)nticks * TES_DUMP_FIELDS);
    int i;

    cudaMalloc(&d_scene, sizeof(TeSpawnerScene));
    cudaMalloc(&d_out, sizeof(u64) * (size_t)nticks * TES_DUMP_FIELDS);
    run_tes<<<1, 1>>>(seed, nticks, d_scene, d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(u64) * (size_t)nticks * TES_DUMP_FIELDS, cudaMemcpyDeviceToHost);

    for (i = 0; i < nticks * TES_DUMP_FIELDS; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);

    free(h_out);
    cudaFree(d_scene);
    cudaFree(d_out);
    return 0;
}
