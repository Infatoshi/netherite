/* CUDA driver for mob_ai_zombie_astar - SAME core/mob_ai_zombie_astar.h as CPU.
 * PfWork on device heap; A* out-of-line to avoid nvcc compile hang. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/mob_ai_zombie_astar.h"

__device__ __noinline__ int maz_pf_find_astar_dev(const u16 *grid, int sx, int sy, int sz,
                                                  int gx, int gy, int gz,
                                                  int entity_height, int max_range,
                                                  PfWork *work, PfResult *out) {
    return pf_find_astar(grid, sx, sy, sz, gx, gy, gz, entity_height, max_range, work, out);
}

__global__ void run_mob_ai_zombie_astar(i64 seed, int nticks, MazTickOut *out, PfWork *work) {
    if (threadIdx.x || blockIdx.x) return;
    maz_run(seed, nticks, out, work);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int nticks = (argc > 2) ? atoi(argv[2]) : MAZ_NUM_TICKS;

    MazTickOut *d_out = NULL;
    PfWork *d_work = NULL;
    MazTickOut *host = (MazTickOut *)malloc(sizeof(MazTickOut) * (size_t)nticks);

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    if (cudaMalloc(&d_out, sizeof(MazTickOut) * (size_t)nticks) != cudaSuccess ||
        cudaMalloc(&d_work, sizeof(PfWork)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        free(host);
        return 1;
    }

    run_mob_ai_zombie_astar<<<1, 1>>>(seed, nticks, d_out, d_work);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
        cudaFree(d_out);
        cudaFree(d_work);
        free(host);
        return 1;
    }

    cudaMemcpy(host, d_out, sizeof(MazTickOut) * (size_t)nticks, cudaMemcpyDeviceToHost);

    for (int t = 0; t < nticks; ++t) {
        const MazTickOut *o = &host[t];
        printf("%08x\n", (unsigned)o->state);
        for (int i = 0; i < 4; ++i) {
            double v = (i == 0) ? o->x : (i == 1) ? o->y : (i == 2) ? o->z : o->yaw;
            u64 bits;
            memcpy(&bits, &v, 8);
            printf("%016llx\n", (unsigned long long)bits);
        }
        printf("%08x\n", (unsigned)o->attack_time);
        printf("%08x\n", (unsigned)o->path_idx);
    }

    free(host);
    cudaFree(d_out);
    cudaFree(d_work);
    return 0;
}
