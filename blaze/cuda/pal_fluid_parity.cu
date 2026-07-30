/* CUDA driver for pal_fluid_parity - SAME core/pal_fluid_parity.h as the CPU
 * path, identical output format. Serial device thread: this is a parity gate,
 * not a throughput engine (SPEC "Open notes"). */
#include <cstdio>
#include <cstdlib>
#include "../core/pal_fluid_parity.h"

__global__ void run_pfp(Env *e, PalChunk *pc, u64 seed,
                        u16 *cur, u16 *tmp, u16 *pcur, u16 *ptmp,
                        PfpLine *lines) {
    if (threadIdx.x || blockIdx.x) return;
    pfp_run(e, pc, seed, cur, tmp, pcur, ptmp, lines);
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    int n_seeds = (argc > 1) ? 1 : 3;
    int si, bad = 0;

    Env *d_e = NULL;
    PalChunk *d_pc = NULL;
    u16 *d_cur = NULL, *d_tmp = NULL, *d_pcur = NULL, *d_ptmp = NULL;
    PfpLine *d_lines = NULL;

    if (cudaMalloc(&d_e, sizeof(Env)) != cudaSuccess ||
        cudaMalloc(&d_pc, sizeof(PalChunk)) != cudaSuccess ||
        cudaMalloc(&d_cur, sizeof(u16) * TFC_SLICE_VOL) != cudaSuccess ||
        cudaMalloc(&d_tmp, sizeof(u16) * TFC_SLICE_VOL) != cudaSuccess ||
        cudaMalloc(&d_pcur, sizeof(u16) * TFC_SLICE_VOL) != cudaSuccess ||
        cudaMalloc(&d_ptmp, sizeof(u16) * TFC_SLICE_VOL) != cudaSuccess ||
        cudaMalloc(&d_lines, sizeof(PfpLine) * TWC_NTICKS) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }

    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)512 * 1024);

    for (si = 0; si < n_seeds; ++si) {
        u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : k_seeds[si];
        PfpLine h_lines[TWC_NTICKS];
        int t;

        run_pfp<<<1, 1>>>(d_e, d_pc, seed, d_cur, d_tmp, d_pcur, d_ptmp, d_lines);
        {
            cudaError_t err = cudaDeviceSynchronize();
            if (err != cudaSuccess) {
                fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
                return 1;
            }
        }
        cudaMemcpy(h_lines, d_lines, sizeof(h_lines), cudaMemcpyDeviceToHost);
        for (t = 0; t < TWC_NTICKS; ++t) {
            printf("%016llx\n", (unsigned long long)h_lines[t].dense_hash);
            printf("%016llx\n", (unsigned long long)h_lines[t].pal_hash);
            printf("%016llx\n", (unsigned long long)h_lines[t].mismatches);
            if (h_lines[t].dense_hash != h_lines[t].pal_hash || h_lines[t].mismatches) bad = 1;
        }
    }

    cudaFree(d_e); cudaFree(d_pc); cudaFree(d_cur); cudaFree(d_tmp);
    cudaFree(d_pcur); cudaFree(d_ptmp); cudaFree(d_lines);
    if (bad) { fprintf(stderr, "pal_fluid_parity: dense != pal\n"); return 1; }
    return 0;
}
