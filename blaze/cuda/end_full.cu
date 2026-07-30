/* CUDA: same ef_run as CPU. Device heap for EndNoise inside cpe_provide_chunk. */
#include <cstdio>
#include <cstdlib>
#include "../core/end_full.h"

__global__ void run_ef(i64 seed, CpePrimer *primer, CpeScratch *sc, EpWorld *ep, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ef_run(primer, sc, ep, seed);
    ep_dump(ep, out);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;

    CpePrimer *d_primer; cudaMalloc(&d_primer, sizeof(CpePrimer));
    CpeScratch *d_sc; cudaMalloc(&d_sc, sizeof(CpeScratch));
    EpWorld *d_ep; cudaMalloc(&d_ep, sizeof(EpWorld));
    u64 *d_out; cudaMalloc(&d_out, sizeof(u64) * (size_t)EP_NOUT);

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_ef<<<1, 1>>>(seed, d_primer, d_sc, d_ep, d_out);
    cudaDeviceSynchronize();

    CpePrimer *h_p = (CpePrimer *)malloc(sizeof(CpePrimer));
    u64 h_out[EP_NOUT];
    cudaMemcpy(h_p, d_primer, sizeof(CpePrimer), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);

    for (int i = 0; i < EF_CHUNK_N; ++i)
        printf("%04x\n", (unsigned)h_p->data[i]);
    for (int i = 0; i < EP_NOUT; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);

    free(h_p);
    cudaFree(d_primer); cudaFree(d_sc); cudaFree(d_ep); cudaFree(d_out);
    return 0;
}
