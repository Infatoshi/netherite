/* CUDA: the SAME core/region_tensor.h rt_fill, single-thread (worldgen is per-chunk SEQUENTIAL and
 * reads its own mid-loop writes), so it runs on ONE thread and keeps CPU==CUDA. The SIN_TABLE is
 * built on the host and copied to the device; the device stack + malloc heap are raised (genlayer +
 * cave addTunnel recursion, GenLayer getInts in-kernel malloc/free). Dumps IDENTICAL stdout to the
 * CPU driver: header line + every u16 element as %04x in index order. */
#include <cstdio>
#include <cstdlib>
#include "../core/region_tensor.h"

__global__ void run_rt(u64 seed, int x0, int y0, int z0, int nx, int ny, int nz,
                       const McSinTable *st, ChunkPrimer *primer, CpScratch *sc, u16 *out) {
    if (threadIdx.x || blockIdx.x) return;
    rt_fill(out, seed, x0, y0, z0, nx, ny, nz, primer, sc, st);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 0LL;
    int x0 = (argc > 2) ? (int)strtol(argv[2], 0, 10) : -16;
    int y0 = (argc > 3) ? (int)strtol(argv[3], 0, 10) : 60;
    int z0 = (argc > 4) ? (int)strtol(argv[4], 0, 10) : -16;
    int nx = (argc > 5) ? (int)strtol(argv[5], 0, 10) : 32;
    int ny = (argc > 6) ? (int)strtol(argv[6], 0, 10) : 24;
    int nz = (argc > 7) ? (int)strtol(argv[7], 0, 10) : 32;

    long total = rt_count(nx, ny, nz);

    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st; cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    ChunkPrimer *d_primer; cudaMalloc(&d_primer, sizeof(ChunkPrimer));
    CpScratch *d_sc; cudaMalloc(&d_sc, sizeof(CpScratch));
    u16 *d_out; cudaMalloc(&d_out, (size_t)total * sizeof(u16));

    /* genlayer recursion + cave addTunnel recursion need more than the default device stack; the
     * GenLayer getInts use in-kernel malloc/free (IntCache substitute) -> raise the heap. */
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_rt<<<1, 1>>>((u64)seed, x0, y0, z0, nx, ny, nz, d_st, d_primer, d_sc, d_out);
    cudaDeviceSynchronize();

    u16 *h_out = (u16 *)malloc((size_t)total * sizeof(u16));
    cudaMemcpy(h_out, d_out, (size_t)total * sizeof(u16), cudaMemcpyDeviceToHost);

    printf("region seed=%lld x0=%d y0=%d z0=%d nx=%d ny=%d nz=%d\n",
           (long long)seed, x0, y0, z0, nx, ny, nz);
    for (long i = 0; i < total; ++i)
        printf("%04x\n", (unsigned)h_out[i]);

    free(h_st); free(h_out);
    cudaFree(d_st); cudaFree(d_primer); cudaFree(d_sc); cudaFree(d_out);
    return 0;
}
