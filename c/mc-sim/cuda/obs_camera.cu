/* CUDA driver for obs_camera - SAME core/obs_camera.h as the CPU path,
 * identical output format. The region tensor is filled HOST-side with the
 * same rt_fill (worldgen CPU==CUDA parity is region_tensor's gate, not this
 * one) and copied to the device; the camera renders ONE PIXEL PER THREAD -
 * the batch shape the RL obs path will use (rays are fully independent).
 * A rays/s throughput line goes to stderr so stdout stays diffable. */
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "../core/region_tensor.h"
#include "../core/obs_camera.h"

#define RG_X0 (-48)
#define RG_Y0 28
#define RG_Z0 (-48)
#define RG_NX 113
#define RG_NY 104
#define RG_NZ 113

static const float k_poses[][2] = {
    {180.0f, 0.0f}, {0.0f, 0.0f}, {90.0f, 15.0f},
    {270.0f, -30.0f}, {45.0f, 60.0f}, {200.0f, -75.0f},
};
#define NPOSES (int)(sizeof k_poses / sizeof k_poses[0])

/* one thread = one (pose, pixel) ray */
__global__ void run_oc(OcRegion rg, const McSinTable *st,
                       const float *poses, int nposes,
                       u16 *ids, u8 *depth, u8 *edge) {
    int gi = blockIdx.x * blockDim.x + threadIdx.x;
    int pi = gi / OC_NPIX, pix = gi % OC_NPIX;
    if (pi >= nposes) return;
    oc_pixel(&rg, st, 8.5, 80.0, 8.5, poses[pi * 2], poses[pi * 2 + 1],
             pix % OC_W, pix / OC_W,
             &ids[pi * OC_NPIX + pix], &depth[pi * OC_NPIX + pix],
             &edge[pi * OC_NPIX + pix]);
}

int main(int argc, char **argv) {
    static const i64 k_seeds[] = {0LL, 3LL, 12345LL};
    int n_seeds = (argc > 1) ? 1 : 3;
    long ncells = rt_count(RG_NX, RG_NY, RG_NZ);
    int si, pi, i;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    u16 *cells = (u16 *)malloc((size_t)ncells * sizeof(u16));
    static u16 h_ids[NPOSES * OC_NPIX];
    static u8 h_depth[NPOSES * OC_NPIX];
    static u8 h_edge[NPOSES * OC_NPIX];
    mc_sin_table_init(st);

    u16 *d_cells = NULL, *d_ids = NULL;
    u8 *d_depth = NULL, *d_edge = NULL;
    McSinTable *d_st = NULL;
    float *d_poses = NULL;
    if (cudaMalloc(&d_cells, (size_t)ncells * sizeof(u16)) != cudaSuccess ||
        cudaMalloc(&d_ids, sizeof(h_ids)) != cudaSuccess ||
        cudaMalloc(&d_depth, sizeof(h_depth)) != cudaSuccess ||
        cudaMalloc(&d_edge, sizeof(h_edge)) != cudaSuccess ||
        cudaMalloc(&d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&d_poses, sizeof(k_poses)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }
    cudaMemcpy(d_st, st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaMemcpy(d_poses, k_poses, sizeof(k_poses), cudaMemcpyHostToDevice);

    for (si = 0; si < n_seeds; ++si) {
        i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : k_seeds[si];
        OcRegion rg;
        rt_fill(cells, (u64)seed, RG_X0, RG_Y0, RG_Z0, RG_NX, RG_NY, RG_NZ,
                primer, sc, st);
        cudaMemcpy(d_cells, cells, (size_t)ncells * sizeof(u16),
                   cudaMemcpyHostToDevice);
        rg.cells = d_cells;
        rg.x0 = RG_X0; rg.y0 = RG_Y0; rg.z0 = RG_Z0;
        rg.nx = RG_NX; rg.ny = RG_NY; rg.nz = RG_NZ;

        int total = NPOSES * OC_NPIX;
        int threads = 128, blocks = (total + threads - 1) / threads;
        run_oc<<<blocks, threads>>>(rg, d_st, d_poses, NPOSES, d_ids,
                                    d_depth, d_edge);
        {
            cudaError_t err = cudaDeviceSynchronize();
            if (err != cudaSuccess) {
                fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
                return 1;
            }
        }
        if (si == 0) { /* throughput, stderr only */
            const int reps = 200;
            struct timespec a, b;
            clock_gettime(CLOCK_MONOTONIC, &a);
            for (i = 0; i < reps; ++i)
                run_oc<<<blocks, threads>>>(rg, d_st, d_poses, NPOSES,
                                            d_ids, d_depth, d_edge);
            cudaDeviceSynchronize();
            clock_gettime(CLOCK_MONOTONIC, &b);
            double s = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
            fprintf(stderr, "[oc] %.1fM rays/s (%d frames/launch, %d reps)\n",
                    (double)total * reps / s / 1e6, NPOSES, reps);
        }
        cudaMemcpy(h_ids, d_ids, sizeof(h_ids), cudaMemcpyDeviceToHost);
        cudaMemcpy(h_depth, d_depth, sizeof(h_depth), cudaMemcpyDeviceToHost);
        cudaMemcpy(h_edge, d_edge, sizeof(h_edge), cudaMemcpyDeviceToHost);

        printf("obs_camera seed=%lld poses=%d %dx%d\n",
               (long long)seed, NPOSES, OC_W, OC_H);
        for (pi = 0; pi < NPOSES; ++pi) {
            for (i = 0; i < OC_NPIX; ++i)
                printf("%04x\n", (unsigned)h_ids[pi * OC_NPIX + i]);
            for (i = 0; i < OC_NPIX; ++i)
                printf("%02x\n", (unsigned)h_depth[pi * OC_NPIX + i]);
            for (i = 0; i < OC_NPIX; ++i)
                printf("%d\n", (int)h_edge[pi * OC_NPIX + i]);
        }
    }

    cudaFree(d_poses); cudaFree(d_st); cudaFree(d_edge); cudaFree(d_depth);
    cudaFree(d_ids);
    cudaFree(d_cells);
    free(cells); free(sc); free(primer); free(st);
    return 0;
}
