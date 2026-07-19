/* CUDA driver for plant_growth - same core, same probe dump. */
#include <cstdio>
#include <cstdlib>
#include "../core/plant_growth.h"

__global__ void run_pg(i64 seed, int nticks, int *out_id, int *out_meta) {
    if (threadIdx.x || blockIdx.x) return;
    PgWorld w;
    pg_run(&w, seed, nticks);
    const u16 *b = pg_now(&w);
    PgProbe probes[PG_NPROBES];
    pg_probes(probes);
    for (int i = 0; i < PG_NPROBES; ++i) {
        u16 s = pg_get(b, probes[i].x, probes[i].y, probes[i].z);
        out_id[i] = pg_id(s);
        out_meta[i] = pg_meta(s);
    }
}

int main(int argc, char **argv) {
    i64 seed   = (argc > 1) ? strtoll(argv[1], 0, 10) : PG_DEFAULT_SEED;
    int nticks = (argc > 2) ? (int)strtol(argv[2], 0, 10) : PG_NTICKS;
    int *d_id, *d_meta;
    int h_id[PG_NPROBES], h_meta[PG_NPROBES];
    cudaMalloc(&d_id, sizeof(int) * PG_NPROBES);
    cudaMalloc(&d_meta, sizeof(int) * PG_NPROBES);
    run_pg<<<1, 1>>>(seed, nticks, d_id, d_meta);
    cudaDeviceSynchronize();
    cudaMemcpy(h_id, d_id, sizeof(int) * PG_NPROBES, cudaMemcpyDeviceToHost);
    cudaMemcpy(h_meta, d_meta, sizeof(int) * PG_NPROBES, cudaMemcpyDeviceToHost);
    for (int i = 0; i < PG_NPROBES; ++i)
        printf("%d %d\n", h_id[i], h_meta[i]);
    cudaFree(d_id);
    cudaFree(d_meta);
    return 0;
}
