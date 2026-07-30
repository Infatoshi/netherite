/* CUDA driver for physics_collision_full - same core header as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/physics_collision_full.h"

#define PCF_OUT_D 12
#define PCF_OUT_F 4

__global__ void run_pcf(int idx_sel, double *out_d, int *out_f) {
    if (threadIdx.x || blockIdx.x) return;
    int lo = (idx_sel < 0) ? 0 : idx_sel;
    int hi = (idx_sel < 0) ? PCF_NUM_SCENARIOS : idx_sel + 1;
    int s = 0;
    for (int idx = lo; idx < hi; ++idx, ++s) {
        McPcfEntity e;
        pcf_run_scenario(idx, &e);
        double *d = out_d + (size_t)s * PCF_OUT_D;
        d[0] = e.posX;    d[1] = e.posY;    d[2] = e.posZ;
        d[3] = e.motionX; d[4] = e.motionY; d[5] = e.motionZ;
        d[6] = e.box.minX; d[7] = e.box.minY; d[8] = e.box.minZ;
        d[9] = e.box.maxX; d[10] = e.box.maxY; d[11] = e.box.maxZ;
        int *f = out_f + (size_t)s * PCF_OUT_F;
        f[0] = e.collidedHorizontally;
        f[1] = e.collidedVertically;
        f[2] = e.onGround;
        f[3] = e.isInWeb;
    }
}

int main(int argc, char **argv) {
    int idx_sel = (argc > 1) ? atoi(argv[1]) : -1;
    int count = (idx_sel < 0) ? PCF_NUM_SCENARIOS : 1;

    double *out_d; int *out_f;
    cudaMalloc(&out_d, sizeof(double) * PCF_OUT_D * count);
    cudaMalloc(&out_f, sizeof(int) * PCF_OUT_F * count);
    run_pcf<<<1, 1>>>(idx_sel, out_d, out_f);
    cudaDeviceSynchronize();
    double *hd = (double *)malloc(sizeof(double) * PCF_OUT_D * count);
    int *hf = (int *)malloc(sizeof(int) * PCF_OUT_F * count);
    cudaMemcpy(hd, out_d, sizeof(double) * PCF_OUT_D * count, cudaMemcpyDeviceToHost);
    cudaMemcpy(hf, out_f, sizeof(int) * PCF_OUT_F * count, cudaMemcpyDeviceToHost);

    for (int s = 0; s < count; ++s) {
        for (int i = 0; i < PCF_OUT_D; ++i) {
            u64 bits; memcpy(&bits, &hd[s * PCF_OUT_D + i], 8);
            printf("%016llx\n", (unsigned long long)bits);
        }
        for (int i = 0; i < PCF_OUT_F; ++i)
            printf("%08x\n", (unsigned)hf[s * PCF_OUT_F + i]);
    }
    free(hd); free(hf); cudaFree(out_d); cudaFree(out_f);
    return 0;
}
