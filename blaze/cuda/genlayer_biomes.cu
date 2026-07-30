/* CUDA: identical GenLayer biome computation from the same core/genlayer_biomes.h. Determinism
 * smoke (1 thread); the batched per-env worldgen kernel comes after the math is proven bit-exact.
 * Uses a preallocated bump arena (IntCache substitute) - NO in-kernel malloc, no raised heap. */
#include <cstdio>
#include <cstdlib>
#include "../core/genlayer_biomes.h"

__global__ void gen(i64 seed, int *out, GlArena *arena) {
    if (threadIdx.x || blockIdx.x) return;
    GLNode nodes[GL_MAX_NODES];
    int biomeIndex;
    gl_build(nodes, seed, &biomeIndex);
    arena->off = 0;
    int *r = gl_getInts(nodes, arena, biomeIndex, 0, 0, 16, 16);
    for (int i = 0; i < 16 * 16; ++i) out[i] = r[i];
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int n = 16 * 16;
    /* recursion through ~40 stacked layers needs more than the tiny default device stack. */
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);
    int *d_out;
    cudaMalloc(&d_out, sizeof(int) * n);
    GlArena *d_arena;
    cudaMalloc(&d_arena, sizeof(GlArena));
    gen<<<1, 1>>>(seed, d_out, d_arena);
    cudaDeviceSynchronize();
    int *out = (int *)malloc(sizeof(int) * n);
    cudaMemcpy(out, d_out, sizeof(int) * n, cudaMemcpyDeviceToHost);
    for (int i = 0; i < n; ++i)
        printf("%08x\n", (unsigned)out[i]);
    free(out);
    cudaFree(d_out);
    cudaFree(d_arena);
    return 0;
}
