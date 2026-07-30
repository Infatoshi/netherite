/* CUDA driver for physics_collision_math - SAME core/physics_collision_math.h as the CPU path.
 * Single-thread determinism smoke (the batched per-env physics kernel comes after the math is
 * proven bit-exact). Output format matches cpu/physics_collision_math.c exactly. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/physics_collision_math.h"

/* 12 doubles + 3 flags per scenario. */
#define MC_PCM_OUT (12 + 3)

__global__ void run_pcm(int idx_sel, double *out_d, int *out_f) {
    if (threadIdx.x || blockIdx.x) return;
    int lo = (idx_sel < 0) ? 0 : idx_sel;
    int hi = (idx_sel < 0) ? MC_PCM_NUM_SCENARIOS : idx_sel + 1;
    int s = 0;
    for (int idx = lo; idx < hi; ++idx, ++s) {
        McEntity e;
        double dx, dy, dz;
        McAABB blocks[MC_PCM_MAX_BLOCKS];
        int n = mc_pcm_scenario(idx, &e, &dx, &dy, &dz, blocks);
        mc_entity_move(&e, dx, dy, dz, blocks, n);
        double *d = out_d + (size_t)s * 12;
        d[0] = e.posX;    d[1] = e.posY;    d[2] = e.posZ;
        d[3] = e.motionX; d[4] = e.motionY; d[5] = e.motionZ;
        d[6] = e.box.minX; d[7] = e.box.minY; d[8] = e.box.minZ;
        d[9] = e.box.maxX; d[10] = e.box.maxY; d[11] = e.box.maxZ;
        int *f = out_f + (size_t)s * 3;
        f[0] = e.collidedHorizontally;
        f[1] = e.collidedVertically;
        f[2] = e.onGround;
    }
}

int main(int argc, char **argv) {
    int idx_sel = (argc > 1) ? atoi(argv[1]) : -1;
    int count = (idx_sel < 0) ? MC_PCM_NUM_SCENARIOS : 1;

    double *out_d; int *out_f;
    cudaMalloc(&out_d, sizeof(double) * 12 * count);
    cudaMalloc(&out_f, sizeof(int) * 3 * count);
    run_pcm<<<1, 1>>>(idx_sel, out_d, out_f);
    cudaDeviceSynchronize();
    double *hd = (double *)malloc(sizeof(double) * 12 * count);
    int *hf = (int *)malloc(sizeof(int) * 3 * count);
    cudaMemcpy(hd, out_d, sizeof(double) * 12 * count, cudaMemcpyDeviceToHost);
    cudaMemcpy(hf, out_f, sizeof(int) * 3 * count, cudaMemcpyDeviceToHost);

    for (int s = 0; s < count; ++s) {
        for (int i = 0; i < 12; ++i) {
            u64 bits; memcpy(&bits, &hd[s * 12 + i], 8);
            printf("%016llx\n", (unsigned long long)bits);
        }
        for (int i = 0; i < 3; ++i) {
            printf("%08x\n", (unsigned)hf[s * 3 + i]);
        }
    }
    free(hd); free(hf); cudaFree(out_d); cudaFree(out_f);
    return 0;
}
