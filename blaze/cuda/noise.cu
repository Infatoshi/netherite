/* CUDA: identical noise computation from the same core/mc_noise.h. Determinism smoke (1 thread);
 * the batched per-env worldgen kernel comes after the math is proven bit-exact. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/mc_noise.h"

__global__ void gen(i64 seed, double *out, int xs, int ys, int zs, double sc) {
    if (threadIdx.x || blockIdx.x) return;
    JavaRandom r; jrand_set(&r, seed);
    NoiseOctaves o; mc_oct_init(&o, &r, 16);
    mc_oct_generate(&o, out, 0, 0, 0, xs, ys, zs, sc, sc, sc);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int xs = 5, ys = 33, zs = 5, n = xs * ys * zs;
    double sc = 684.412;
    double *d_out; cudaMalloc(&d_out, sizeof(double) * n);
    gen<<<1, 1>>>(seed, d_out, xs, ys, zs, sc);
    cudaDeviceSynchronize();
    double *out = (double *)malloc(sizeof(double) * n);
    cudaMemcpy(out, d_out, sizeof(double) * n, cudaMemcpyDeviceToHost);
    for (int i = 0; i < n; i++) {
        u64 bits; memcpy(&bits, &out[i], 8);
        printf("%016llx\n", (unsigned long long)bits);
    }
    free(out); cudaFree(d_out);
    return 0;
}
