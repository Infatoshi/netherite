/* CUDA: same core/chunk_provider_flat.h, single-thread (column fill is sequential).
 * Raises heap limit to match the project CUDA driver pattern (no device malloc here). */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/chunk_provider_flat.h"

__global__ void run_cpf(const char *preset, int preset_len, CpfPrimer *primer) {
    if (threadIdx.x || blockIdx.x) return;
    char buf[256];
    const char *p = NULL;
    if (preset_len > 0 && preset_len < 255) {
        for (int i = 0; i < preset_len; ++i) buf[i] = preset[i];
        buf[preset_len] = 0;
        p = buf;
    }
    cpf_provide_chunk(primer, p);
}

int main(int argc, char **argv) {
    (void)(argc > 1 ? strtoll(argv[1], 0, 10) : 12345LL);
    const char *preset = (argc > 2) ? argv[2] : NULL;
    int preset_len = preset ? (int)strlen(preset) : 0;

    char *d_preset = NULL;
    if (preset_len > 0) {
        cudaMalloc(&d_preset, (size_t)preset_len);
        cudaMemcpy(d_preset, preset, (size_t)preset_len, cudaMemcpyHostToDevice);
    }

    CpfPrimer *d_primer;
    cudaMalloc(&d_primer, sizeof(CpfPrimer));

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024);

    run_cpf<<<1, 1>>>(d_preset, preset_len, d_primer);
    cudaDeviceSynchronize();

    CpfPrimer h_primer;
    cudaMemcpy(&h_primer, d_primer, sizeof(CpfPrimer), cudaMemcpyDeviceToHost);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)h_primer.data[i]);

    if (d_preset) cudaFree(d_preset);
    cudaFree(d_primer);
    return 0;
}
