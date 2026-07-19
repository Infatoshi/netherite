#include <cstdio>
#include <cstdlib>
#include "../core/map_gen_mineshaft.h"

__global__ void run_ms(i64 seed, u16 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ChunkPrimer p; ms_run(&p, seed, 0, 0);
    for (int i = 0; i < 65536; ++i) out[i] = p.data[i];
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    u16 *d, h[65536];
    cudaMalloc(&d, 65536*sizeof(u16));
    run_ms<<<1,1>>>(seed, d);
    cudaDeviceSynchronize();
    cudaMemcpy(h, d, 65536*sizeof(u16), cudaMemcpyDeviceToHost);
    cudaFree(d);
    for (int i = 0; i < 65536; ++i) printf("%04x\n", (unsigned)h[i]);
    return 0;
}
