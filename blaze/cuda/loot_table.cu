/* CUDA driver for loot_table. Same core/loot_table.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "../core/loot_table.h"

__global__ void run_lt(u32 *out, int full, int table_id, int roll_idx) {
    if (threadIdx.x || blockIdx.x) return;
    if (full)
        lt_run_battery(out);
    else
        lt_run_one(table_id, roll_idx, out);
}

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    u32 *d_out;
    u32 h_out[LT_OUT];
    int i;

    if (argc > 2) {
        int t = atoi(argv[1]);
        int r = atoi(argv[2]);
        u32 partial[LT_FIELDS_PER];
        cudaMalloc(&d_out, sizeof(partial));
        run_lt<<<1, 1>>>(d_out, 0, t, r);
        cudaDeviceSynchronize();
        cudaMemcpy(partial, d_out, sizeof(partial), cudaMemcpyDeviceToHost);
        for (i = 0; i < LT_FIELDS_PER; ++i)
            emit_u32(partial[i]);
        cudaFree(d_out);
        return 0;
    }

    cudaMalloc(&d_out, sizeof(h_out));
    run_lt<<<1, 1>>>(d_out, 1, 0, 0);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (i = 0; i < LT_OUT; ++i)
        emit_u32(h_out[i]);
    cudaFree(d_out);
    return 0;
}
