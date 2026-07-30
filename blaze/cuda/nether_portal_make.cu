/* CUDA driver for nether_portal_make - same core as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/nether_portal_make.h"

__global__ void run_npm(int idx, NpmWorld *w, CpnHellScratch *sc, const McSinTable *st,
                        CpnHellNoise *noise, NpmResult *r) {
    if (threadIdx.x || blockIdx.x) return;
    npm_run_scenario(idx, w, sc, st, noise, r);
}

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

static void print_result(const NpmResult *r) {
    emit_u32((u32)r->found);
    emit_u32((u32)r->ax);
    emit_u32((u32)r->ay);
    emit_u32((u32)r->az);
    emit_u32((u32)r->orient);
    emit_u32((u32)r->portal_count);
    emit_u32((u32)r->obsidian_count);
    emit_u32((u32)r->rand_orient_base);
}

static void run_one(int idx, McSinTable *d_st, NpmWorld *d_w, CpnHellScratch *d_sc,
                    CpnHellNoise *d_noise, NpmResult *d_r) {
    run_npm<<<1, 1>>>(idx, d_w, d_sc, d_st, d_noise, d_r);
    cudaDeviceSynchronize();
    NpmResult h_r;
    cudaMemcpy(&h_r, d_r, sizeof(NpmResult), cudaMemcpyDeviceToHost);
    print_result(&h_r);
}

int main(int argc, char **argv) {
    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st;
    NpmWorld *d_w;
    CpnHellScratch *d_sc;
    CpnHellNoise *d_noise;
    NpmResult *d_r;

    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaMalloc(&d_w, sizeof(NpmWorld));
    cudaMalloc(&d_sc, sizeof(CpnHellScratch));
    cudaMalloc(&d_noise, sizeof(CpnHellNoise));
    cudaMalloc(&d_r, sizeof(NpmResult));

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    if (argc > 1) {
        run_one(atoi(argv[1]), d_st, d_w, d_sc, d_noise, d_r);
    } else {
        for (int i = 0; i < NPM_NUM_SCENARIOS; ++i)
            run_one(i, d_st, d_w, d_sc, d_noise, d_r);
    }

    free(h_st);
    cudaFree(d_st); cudaFree(d_w); cudaFree(d_sc); cudaFree(d_noise); cudaFree(d_r);
    return 0;
}
