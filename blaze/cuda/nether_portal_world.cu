/* CUDA driver for nether_portal_world - same core as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/nether_portal_world.h"

__global__ void run_npw(int idx, NpwWorld *w, CpScratch *sc, const McSinTable *st,
                        NpwResult *r) {
    if (threadIdx.x || blockIdx.x) return;
    npw_run_scenario(idx, w, sc, st, r);
}

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

static void print_result(const NpwResult *r) {
    emit_u32((u32)r->detect_valid);
    emit_u32((u32)r->detect_w);
    emit_u32((u32)r->detect_h);
    emit_u32((u32)r->detect_axis);
    emit_u32((u32)r->spawn_ok);
    emit_u32((u32)r->portal_count);
    emit_u32((u32)r->surface_y);
    emit_u32((u32)r->extract_y);
}

static void run_one(int idx, McSinTable *d_st, NpwWorld *d_w, CpScratch *d_sc,
                    NpwResult *d_r) {
    run_npw<<<1, 1>>>(idx, d_w, d_sc, d_st, d_r);
    cudaDeviceSynchronize();
    NpwResult h_r;
    cudaMemcpy(&h_r, d_r, sizeof(NpwResult), cudaMemcpyDeviceToHost);
    print_result(&h_r);
}

int main(int argc, char **argv) {
    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st;
    NpwWorld *d_w;
    CpScratch *d_sc;
    NpwResult *d_r;

    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaMalloc(&d_w, sizeof(NpwWorld));
    cudaMalloc(&d_sc, sizeof(CpScratch));
    cudaMalloc(&d_r, sizeof(NpwResult));

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    if (argc > 1) {
        run_one(atoi(argv[1]), d_st, d_w, d_sc, d_r);
    } else {
        for (int i = 0; i < NPW_NUM_SCENARIOS; ++i)
            run_one(i, d_st, d_w, d_sc, d_r);
    }

    free(h_st);
    cudaFree(d_st); cudaFree(d_w); cudaFree(d_sc); cudaFree(d_r);
    return 0;
}
