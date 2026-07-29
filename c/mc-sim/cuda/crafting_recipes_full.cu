/* CUDA: same core/crafting_recipes_full.h as CPU; single-thread smoke. */
#include <cstdio>
#include <cstdlib>
#include "../core/crafting_recipes_full.h"

__global__ void run(i32 *out) {
    if (threadIdx.x || blockIdx.x) return;
    CRRecipe R[CRF_NRECIPES];
    int n = crf_build(R);
    CRStack grids[CRF_NTESTS][9];
    crf_battery(grids);
    for (int t = 0; t < CRF_NTESTS; ++t) {
        CRStack r = crf_findMatching(R, n, grids[t]);
        out[t * 3 + 0] = r.item;
        out[t * 3 + 1] = r.count;
        out[t * 3 + 2] = r.meta;
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int nout = CRF_NTESTS * 3;
    i32 *d_out;
    cudaMalloc(&d_out, sizeof(i32) * nout);
    run<<<1, 1>>>(d_out);
    cudaDeviceSynchronize();
    i32 *out = (i32 *)malloc(sizeof(i32) * nout);
    cudaMemcpy(out, d_out, sizeof(i32) * nout, cudaMemcpyDeviceToHost);
    for (int i = 0; i < nout; ++i)
        printf("%08x\n", (unsigned)out[i]);
    free(out);
    cudaFree(d_out);
    return 0;
}
