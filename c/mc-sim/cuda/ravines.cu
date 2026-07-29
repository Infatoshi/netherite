/* CUDA: SAME core/ravines.h as the CPU path. MapGenRavine is a per-chunk SEQUENTIAL feature that
 * reads its own mid-loop writes, so it runs on ONE thread (correct, and keeps CPU==CUDA). The
 * SIN_TABLE is built on the host and copied to the device so its bytes match the CPU table. */
#include <cstdio>
#include <cstdlib>
#include "../core/ravines.h"

__global__ void run_ravines(i64 seed, const McSinTable *st, RavinePrimer *primer) {
    if (threadIdx.x || blockIdx.x) return;
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 256; ++y)
                primer->data[mc_ravine_idx(x, y, z)] = (y >= 1 && y <= 127) ? RV_STONE : RV_AIR;

    MapGenRavine mg;
    mg.worldSeed = seed;
    mg.range = 8;
    mg.st = st;
    mc_ravine_generate(&mg, primer, 0, 0);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st; cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    RavinePrimer *d_primer; cudaMalloc(&d_primer, sizeof(RavinePrimer));
    run_ravines<<<1, 1>>>(seed, d_st, d_primer);
    cudaDeviceSynchronize();

    RavinePrimer *primer = (RavinePrimer *)malloc(sizeof(RavinePrimer));
    cudaMemcpy(primer, d_primer, sizeof(RavinePrimer), cudaMemcpyDeviceToHost);

    for (int idx = 0; idx < 65536; ++idx)
        printf("%04x\n", (unsigned)primer->data[idx]);

    free(h_st); free(primer);
    cudaFree(d_st); cudaFree(d_primer);
    return 0;
}
