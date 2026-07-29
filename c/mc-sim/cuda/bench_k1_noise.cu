/* bench_k1_noise.cu - K1 go/no-go measurement (WORKQUEUE "GPU-native worldgen").
 *
 * Question: is one-thread-per-output-cell FP64 octave noise on the 3090 (GA102,
 * FP64 at 1/64 rate) faster than ONE 9950X3D core running the verbatim
 * mc_oct_generate path? This is the gating stage for the whole stage-split
 * worldgen redesign, so it gets measured before anything is built.
 *
 * Workload = the 4 noise regions cp_generateHeightmap fills per chunk
 * (minLimit 16 oct x 825, maxLimit 16 x 825, mainP 8 x 825, depth 16 x 25;
 * 33,400 octave-cell evals/chunk) over a 361-chunk (19x19) burst, i.e. the
 * relight window. CPU side is the exact production code; GPU side recomputes
 * each cell independently (no y-quad lerp reuse) in the same octave order, so
 * results must be BIT-IDENTICAL (--fmad=false, matching the CPU's
 * -ffp-contract=off) - the bench verifies that too.
 *
 * Build+run (GPU1 = 3090 per repo policy):
 *   nvcc -arch=sm_86 -O3 --fmad=false -Icore cuda/bench_k1_noise.cu -o build/bench_k1_noise
 *   CUDA_VISIBLE_DEVICES=1 ./build/bench_k1_noise
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cuda_runtime.h>
#include "mc.h"
#include "mc_rng.h"
#include "mc_noise.h"

#ifndef NCX
#define NCX 19
#endif
#define NCHUNK (NCX * NCX)
#define CELLS3 (5 * 5 * 33)
#define CELLS2 (5 * 5)

/* per-point recompute of the mc_ni_populate 3D cell math (identical exprs/order) */
MC_HD static double ni_fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

MC_HD static double ni_point3(const NoiseImproved *n, double xOffset, double yOffset,
                              double zOffset, int ix, int iy, int iz,
                              double xScale, double yScale, double zScale, double inv) {
    const int *P = n->permutations;
    double d5 = xOffset + (double)ix * xScale + n->xCoord;
    int i3 = (int)d5; if (d5 < (double)i3) --i3;
    int j3 = i3 & 255; d5 = d5 - (double)i3;
    double d6 = ni_fade(d5);
    double d7 = zOffset + (double)iz * zScale + n->zCoord;
    int l3 = (int)d7; if (d7 < (double)l3) --l3;
    int i4 = l3 & 255; d7 = d7 - (double)l3;
    double d8 = ni_fade(d7);
    double d9 = yOffset + (double)iy * yScale + n->yCoord;
    int k4 = (int)d9; if (d9 < (double)k4) --k4;
    int l4 = k4 & 255; d9 = d9 - (double)k4;
    double d10 = ni_fade(d9);
    /* vanilla stale-quad quirk: populateNoiseArray recomputes the corner lerps
     * d1..d4 only when l4 changes as j4 scans upward, so every later y in the
     * quad reuses gradients evaluated at the quad's ENTRY y. Find that entry. */
    int j0 = iy;
    while (j0 > 0) {
        double dE = yOffset + (double)(j0 - 1) * yScale + n->yCoord;
        int kE = (int)dE; if (dE < (double)kE) --kE;
        if ((kE & 255) != l4) break;
        --j0;
    }
    double d9e = yOffset + (double)j0 * yScale + n->yCoord;
    int k4e = (int)d9e; if (d9e < (double)k4e) --k4e;
    d9e = d9e - (double)k4e;
    int l  = P[j3] + l4;
    int i1 = P[l] + i4;
    int j1 = P[l + 1] + i4;
    int k1 = P[j3 + 1] + l4;
    int l1 = P[k1] + i4;
    int i2 = P[k1 + 1] + i4;
    double d1 = mc_ni_lerp(d6, mc_ni_grad(P[i1], d5, d9e, d7), mc_ni_grad(P[l1], d5 - 1.0, d9e, d7));
    double d2 = mc_ni_lerp(d6, mc_ni_grad(P[j1], d5, d9e - 1.0, d7), mc_ni_grad(P[i2], d5 - 1.0, d9e - 1.0, d7));
    double d3 = mc_ni_lerp(d6, mc_ni_grad(P[i1 + 1], d5, d9e, d7 - 1.0), mc_ni_grad(P[l1 + 1], d5 - 1.0, d9e, d7 - 1.0));
    double d4 = mc_ni_lerp(d6, mc_ni_grad(P[j1 + 1], d5, d9e - 1.0, d7 - 1.0), mc_ni_grad(P[i2 + 1], d5 - 1.0, d9e - 1.0, d7 - 1.0));
    double d11 = mc_ni_lerp(d10, d1, d2);
    double d12 = mc_ni_lerp(d10, d3, d4);
    double d13 = mc_ni_lerp(d8, d11, d12);
    return d13 * inv;
}

MC_HD static double ni_point2(const NoiseImproved *n, double xOffset, double zOffset,
                              int ix, int iz, double xScale, double zScale, double inv) {
    const int *P = n->permutations;
    double d17 = xOffset + (double)ix * xScale + n->xCoord;
    int i6 = (int)d17; if (d17 < (double)i6) --i6;
    int k2 = i6 & 255; d17 = d17 - (double)i6;
    double d18 = ni_fade(d17);
    double d19 = zOffset + (double)iz * zScale + n->zCoord;
    int k6 = (int)d19; if (d19 < (double)k6) --k6;
    int l6 = k6 & 255; d19 = d19 - (double)k6;
    double d20 = ni_fade(d19);
    int i5 = P[k2] + 0;
    int j5 = P[i5] + l6;
    int j  = P[k2 + 1] + 0;
    int k5 = P[j] + l6;
    double d14 = mc_ni_lerp(d18, mc_ni_grad2(P[j5], d17, d19), mc_ni_grad(P[k5], d17 - 1.0, 0.0, d19));
    double d15 = mc_ni_lerp(d18, mc_ni_grad(P[j5 + 1], d17, 0.0, d19 - 1.0), mc_ni_grad(P[k5 + 1], d17 - 1.0, 0.0, d19 - 1.0));
    double d21 = mc_ni_lerp(d20, d14, d15);
    return d21 * (1.0 / inv);   /* 2D path divides by noiseScale */
}

/* octave accumulation per cell, mirroring mc_oct_generate's wrap + order */
MC_HD static double oct_cell3(const NoiseOctaves *o, int xOffset, int yOffset, int zOffset,
                              int ix, int iy, int iz,
                              double xScale, double yScale, double zScale) {
    double acc = 0.0, d3 = 1.0;
    for (int j = 0; j < o->octaves; ++j) {
        double d0 = (double)xOffset * d3 * xScale;
        double d1 = (double)yOffset * d3 * yScale;
        double d2 = (double)zOffset * d3 * zScale;
        i64 k = mc_lfloor(d0);
        i64 l = mc_lfloor(d2);
        d0 = d0 - (double)k; d2 = d2 - (double)l;
        k = k % 16777216L; l = l % 16777216L;
        d0 = d0 + (double)k; d2 = d2 + (double)l;
        acc += ni_point3(&o->gen[j], d0, d1, d2, ix, iy, iz,
                         xScale * d3, yScale * d3, zScale * d3, 1.0 / d3);
        d3 /= 2.0;
    }
    return acc;
}

MC_HD static double oct_cell2(const NoiseOctaves *o, int xOffset, int yOffset, int zOffset,
                              int ix, int iz, double xScale, double yScale, double zScale) {
    double acc = 0.0, d3 = 1.0;
    for (int j = 0; j < o->octaves; ++j) {
        double d0 = (double)xOffset * d3 * xScale;
        double d2 = (double)zOffset * d3 * zScale;
        i64 k = mc_lfloor(d0);
        i64 l = mc_lfloor(d2);
        d0 = d0 - (double)k; d2 = d2 - (double)l;
        k = k % 16777216L; l = l % 16777216L;
        d0 = d0 + (double)k; d2 = d2 + (double)l;
        (void)yOffset; (void)yScale;
        acc += ni_point2(&o->gen[j], d0, d2, ix, iz, xScale * d3, zScale * d3, d3);
        d3 /= 2.0;
    }
    return acc;
}

/* the 4 regions of cp_generateHeightmap, vanilla scales */
#define COORD_SCALE 684.412
#define MAIN_XZ (COORD_SCALE / 80.0)
#define MAIN_Y  (COORD_SCALE / 160.0)

typedef struct { NoiseOctaves minLimit, maxLimit, mainP, depth; } K1Noise;

__global__ void k1_region3(const K1Noise *n, double *out_min, double *out_max,
                           double *out_main, int ncx) {
    int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= ncx * ncx * CELLS3) return;
    int cell = t % CELLS3, chunk = t / CELLS3;
    /* mc_oct_generate fills x-major (ix outer, iz mid, iy inner) */
    int iy = cell % 33, iz = (cell / 33) % 5, ix = cell / (33 * 5);
    int cx = chunk % ncx - ncx / 2, cz = chunk / ncx - ncx / 2;
    int p1 = cx * 4, p3 = cz * 4;
    out_min[t]  = oct_cell3(&n->minLimit, p1, 0, p3, ix, iy, iz, COORD_SCALE, COORD_SCALE, COORD_SCALE);
    out_max[t]  = oct_cell3(&n->maxLimit, p1, 0, p3, ix, iy, iz, COORD_SCALE, COORD_SCALE, COORD_SCALE);
    out_main[t] = oct_cell3(&n->mainP,    p1, 0, p3, ix, iy, iz, MAIN_XZ, MAIN_Y, MAIN_XZ);
}

__global__ void k1_region2(const K1Noise *n, double *out_depth, int ncx) {
    int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= ncx * ncx * CELLS2) return;
    int cell = t % CELLS2, chunk = t / CELLS2;
    int iz = cell % 5, ix = cell / 5;
    int cx = chunk % ncx - ncx / 2, cz = chunk / ncx - ncx / 2;
    out_depth[t] = oct_cell2(&n->depth, cx * 4, 10, cz * 4, ix, iz, 200.0, 1.0, 200.0);
}

static double now_s(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

#define CK(x) do { cudaError_t e = (x); if (e != cudaSuccess) { \
    fprintf(stderr, "CUDA %s:%d %s\n", __FILE__, __LINE__, cudaGetErrorString(e)); exit(1); } } while (0)

int main(void) {
    K1Noise *h = (K1Noise *)malloc(sizeof(K1Noise));
    {   /* ChunkProviderOverworld ctor draw order (terrain_noise_init) */
        JavaRandom r; jrand_set(&r, 0);
        mc_oct_init(&h->minLimit, &r, 16);
        mc_oct_init(&h->maxLimit, &r, 16);
        mc_oct_init(&h->mainP, &r, 8);
        mc_oct_init(&h->depth, &r, 16);  /* skip-advance irrelevant for a throughput bench */
    }

    /* ---- CPU reference: production mc_oct_generate, one core ---- */
    size_t n3 = (size_t)NCHUNK * CELLS3, n2 = (size_t)NCHUNK * CELLS2;
    double *c_min = (double *)malloc(n3 * 8), *c_max = (double *)malloc(n3 * 8);
    double *c_main = (double *)malloc(n3 * 8), *c_depth = (double *)malloc(n2 * 8);
    double t0 = now_s();
    int reps_cpu = 5;
    for (int rep = 0; rep < reps_cpu; ++rep)
        for (int chunk = 0; chunk < NCHUNK; ++chunk) {
            int cx = chunk % NCX - NCX / 2, cz = chunk / NCX - NCX / 2;
            int p1 = cx * 4, p3 = cz * 4;
            mc_oct_generate(&h->depth, c_depth + chunk * CELLS2, p1, 10, p3, 5, 1, 5, 200.0, 1.0, 200.0);
            mc_oct_generate(&h->mainP, c_main + chunk * CELLS3, p1, 0, p3, 5, 33, 5, MAIN_XZ, MAIN_Y, MAIN_XZ);
            mc_oct_generate(&h->minLimit, c_min + chunk * CELLS3, p1, 0, p3, 5, 33, 5, COORD_SCALE, COORD_SCALE, COORD_SCALE);
            mc_oct_generate(&h->maxLimit, c_max + chunk * CELLS3, p1, 0, p3, 5, 33, 5, COORD_SCALE, COORD_SCALE, COORD_SCALE);
        }
    double cpu_s = (now_s() - t0) / reps_cpu;

    /* ---- GPU: one thread per output cell ---- */
    K1Noise *d_n; double *d_min, *d_max, *d_main, *d_depth;
    CK(cudaMalloc(&d_n, sizeof(K1Noise)));
    CK(cudaMemcpy(d_n, h, sizeof(K1Noise), cudaMemcpyHostToDevice));
    CK(cudaMalloc(&d_min, n3 * 8)); CK(cudaMalloc(&d_max, n3 * 8));
    CK(cudaMalloc(&d_main, n3 * 8)); CK(cudaMalloc(&d_depth, n2 * 8));
    int bs = 128;
    int g3 = (int)((n3 + bs - 1) / bs), g2 = (int)((n2 + bs - 1) / bs);
    /* warmup + correctness */
    k1_region3<<<g3, bs>>>(d_n, d_min, d_max, d_main, NCX);
    k1_region2<<<g2, bs>>>(d_n, d_depth, NCX);
    CK(cudaDeviceSynchronize());
    cudaEvent_t ev0, ev1; CK(cudaEventCreate(&ev0)); CK(cudaEventCreate(&ev1));
    int reps = 20;
    CK(cudaEventRecord(ev0));
    for (int rep = 0; rep < reps; ++rep) {
        k1_region3<<<g3, bs>>>(d_n, d_min, d_max, d_main, NCX);
        k1_region2<<<g2, bs>>>(d_n, d_depth, NCX);
    }
    CK(cudaEventRecord(ev1));
    CK(cudaEventSynchronize(ev1));
    float ms; CK(cudaEventElapsedTime(&ms, ev0, ev1));
    double gpu_s = ms * 1e-3 / reps;

    /* ---- bit-compare ---- */
    double *g_min = (double *)malloc(n3 * 8), *g_max = (double *)malloc(n3 * 8);
    double *g_main = (double *)malloc(n3 * 8), *g_depth = (double *)malloc(n2 * 8);
    CK(cudaMemcpy(g_min, d_min, n3 * 8, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(g_max, d_max, n3 * 8, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(g_main, d_main, n3 * 8, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(g_depth, d_depth, n2 * 8, cudaMemcpyDeviceToHost));
    size_t bad = 0;
    bad += memcmp(g_min, c_min, n3 * 8) ? 1 : 0;
    size_t badcells = 0; double maxad = 0.0;
    for (size_t i = 0; i < n3; ++i) {
        if (g_min[i] != c_min[i]) ++badcells;
        if (g_max[i] != c_max[i]) ++badcells;
        if (g_main[i] != c_main[i]) ++badcells;
        double d = fabs(g_min[i] - c_min[i]); if (d > maxad) maxad = d;
    }
    for (size_t i = 0; i < n2; ++i) if (g_depth[i] != c_depth[i]) ++badcells;

    int dev; cudaGetDevice(&dev);
    struct cudaDeviceProp p; cudaGetDeviceProperties(&p, dev);
    double ocells = (double)NCHUNK * 33400.0;
    printf("K1 noise-field bench: %d chunks (19x19), %.0f octave-cell evals\n", NCHUNK, ocells);
    printf("GPU: %s\n", p.name);
    printf("CPU 1 core (production mc_oct_generate): %8.3f ms  -> %8.0f chunks/s\n",
           cpu_s * 1e3, NCHUNK / cpu_s);
    printf("GPU thread-per-cell (%d regions kernels): %8.3f ms  -> %8.0f chunks/s\n",
           2, gpu_s * 1e3, NCHUNK / gpu_s);
    printf("speedup: %.1fx   bit-mismatched cells: %zu / %zu (max abs diff minLimit %.3e)\n",
           cpu_s / gpu_s, badcells, 3 * n3 + n2, maxad);
    (void)bad;
    return 0;
}
