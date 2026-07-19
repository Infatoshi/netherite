/* CUDA driver for item_bow_use. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/item_bow_use.h"

__global__ void run_ibu(i64 seed, const McSinTable *st, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ibu_run(seed, st, out);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    McSinTable h_st;
    McSinTable *d_st;
    u64 *d_out;
    u64 h_out[IBU_OUT];
    mc_sin_table_init(&h_st);
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, &h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    cudaMalloc(&d_out, sizeof(h_out));
    run_ibu<<<1, 1>>>(seed, d_st, d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (int i = 0; i < IBU_OUT; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);
    cudaFree(d_out);
    cudaFree(d_st);
    return 0;
}
