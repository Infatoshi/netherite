/* CUDA driver for py_gym_env_smoke - same core as CPU path. */
#include <cstdio>
#include <cstdlib>
#include "../core/py_gym_env_smoke.h"

struct PgesEmitCtx {
    u64 hashes[PGES_N_TICKS + 1];
    int n;
};

__device__ __noinline__ int maz_pf_find_astar_dev(const u16 *grid, int sx, int sy, int sz,
                                                      int gx, int gy, int gz,
                                                      int entity_height, int max_range,
                                                      PfWork *work, PfResult *out) {
    return pf_find_astar(grid, sx, sy, sz, gx, gy, gz, entity_height, max_range, work, out);
}

__global__ void run_pges(PgesEnv *g, u64 seed, const PgesAction *replay, PgesEmitCtx *out) {
    PgesObs obs;
    float reward;
    int done;
    int i;

    if (threadIdx.x || blockIdx.x) return;

    pges_reset(g, seed);
    pges_obs_after_reset(g, &obs);
    out->hashes[out->n++] = pges_obs_hash(&obs);

    for (i = 0; i < PGES_N_TICKS; ++i) {
        pges_step(g, &replay[i], &obs, &reward, &done);
        out->hashes[out->n++] = pges_obs_hash(&obs);
    }
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : 12345ULL;
    PgesEnv *d_g = NULL;
    PgesAction *d_replay = NULL;
    PgesEmitCtx *d_out = NULL;
    PgesEmitCtx h_out;
    int i;

    if (cudaMalloc(&d_g, sizeof(PgesEnv)) != cudaSuccess ||
        cudaMalloc(&d_replay, sizeof(PgesAction) * PGES_N_TICKS) != cudaSuccess ||
        cudaMalloc(&d_out, sizeof(PgesEmitCtx)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }

    cudaMemcpy(d_replay, PGES_REPLAY, sizeof(PgesAction) * PGES_N_TICKS, cudaMemcpyHostToDevice);

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    h_out.n = 0;
    cudaMemcpy(d_out, &h_out, sizeof(PgesEmitCtx), cudaMemcpyHostToDevice);

    run_pges<<<1, 1>>>(d_g, seed, d_replay, d_out);
    {
        cudaError_t err = cudaDeviceSynchronize();
        if (err != cudaSuccess) {
            fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
            cudaFree(d_g);
            cudaFree(d_out);
            return 1;
        }
    }

    cudaMemcpy(&h_out, d_out, sizeof(PgesEmitCtx), cudaMemcpyDeviceToHost);
    for (i = 0; i < h_out.n; ++i)
        printf("%016llx\n", (unsigned long long)h_out.hashes[i]);

    cudaFree(d_out);
    cudaFree(d_replay);
    cudaFree(d_g);
    return 0;
}
