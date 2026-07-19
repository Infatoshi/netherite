/* CUDA: identical crafting computation from the same core/crafting_recipes.h. Single-thread
 * determinism smoke (one env); the batched per-env crafting kernel comes after the logic is proven
 * bit-exact. Output format matches cpu/crafting_recipes.c and the golden exactly. */
#include <cstdio>
#include <cstdlib>
#include "../core/crafting_recipes.h"

__global__ void run(i32 *out /* CR_NTESTS*3 */) {
    if (threadIdx.x || blockIdx.x) return;
    CRRecipe R[CR_NRECIPES];
    int n = cr_build(R);
    CRStack grids[CR_NTESTS][9];
    cr_battery(grids);
    for (int t = 0; t < CR_NTESTS; ++t) {
        CRStack r = cr_findMatching(R, n, grids[t]);
        out[t * 3 + 0] = r.item;
        out[t * 3 + 1] = r.count;
        out[t * 3 + 2] = r.meta;
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int n = CR_NTESTS * 3;
    i32 *d_out;
    cudaMalloc(&d_out, sizeof(i32) * n);
    run<<<1, 1>>>(d_out);
    cudaDeviceSynchronize();
    i32 *out = (i32 *)malloc(sizeof(i32) * n);
    cudaMemcpy(out, d_out, sizeof(i32) * n, cudaMemcpyDeviceToHost);
    for (int i = 0; i < n; ++i)
        printf("%08x\n", (unsigned)out[i]);
    free(out);
    cudaFree(d_out);
    return 0;
}
