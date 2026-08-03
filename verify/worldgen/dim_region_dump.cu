/* dim_region_dump.cu - CUDA twin of dim_region_dump.c (same nf_run / cpe paths).
 *
 * Single-thread kernel per chunk for CPU==CUDA bit-parity of sequential worldgen.
 * Build (GPU0 Blackwell default):
 *   nvcc -arch=sm_120 -O3 --fmad=false -Iblaze/core \
 *        verify/worldgen/dim_region_dump.cu -o verify/worldgen/dim_region_dump_cuda
 *
 * CLI matches the CPU tool (nether|end dump modes only; fortress find stays CPU).
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cuda_runtime.h>

#include "nether_full.h"
#include "chunk_provider_end.h"

static int ce_to_van(int v) {
    if (v == CE_AIR) return 0;
    if (v == CE_END_STONE) return 121;
    if (v == CE_STONE) return 1;
    return v;
}

__global__ void k_nf(i64 seed, int cx, int cz, const McSinTable *st,
                     CpnPrimer *primer, CpnHellScratch *sc, CpnHellNoise *noise) {
    if (threadIdx.x || blockIdx.x) return;
    nf_run(primer, sc, st, noise, seed, cx, cz);
}

__global__ void k_cpe(i64 seed, int cx, int cz, CpePrimer *primer, CpeScratch *sc) {
    if (threadIdx.x || blockIdx.x) return;
    cpe_provide_chunk(primer, sc, seed, cx, cz);
}

static void die_cuda(const char *what, cudaError_t err) {
    if (err == cudaSuccess) return;
    fprintf(stderr, "cuda %s: %s\n", what, cudaGetErrorString(err));
    exit(1);
}

static FILE *open_out(const char *path) {
    if (!path || !path[0] || !strcmp(path, "-")) return stdout;
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    return f;
}

static int dump_nether(i64 seed, int cx0, int cz0, int ncx, int ncz, FILE *out) {
    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    CpnHellNoise *h_noise = (CpnHellNoise *)malloc(sizeof(CpnHellNoise));
    CpnPrimer *h_primer = (CpnPrimer *)malloc(sizeof(CpnPrimer));
    if (!h_st || !h_noise || !h_primer) return 2;
    mc_sin_table_init(h_st);
    cpn_noise_init(h_noise, seed);

    McSinTable *d_st = nullptr;
    CpnPrimer *d_primer = nullptr;
    CpnHellScratch *d_sc = nullptr;
    CpnHellNoise *d_noise = nullptr;
    die_cuda("malloc st", cudaMalloc(&d_st, sizeof(McSinTable)));
    die_cuda("malloc primer", cudaMalloc(&d_primer, sizeof(CpnPrimer)));
    die_cuda("malloc sc", cudaMalloc(&d_sc, sizeof(CpnHellScratch)));
    die_cuda("malloc noise", cudaMalloc(&d_noise, sizeof(CpnHellNoise)));
    die_cuda("cpy st", cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice));
    die_cuda("cpy noise",
             cudaMemcpy(d_noise, h_noise, sizeof(CpnHellNoise), cudaMemcpyHostToDevice));
    die_cuda("stack", cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024));

    long long non_air = 0;
    for (int iz = 0; iz < ncz; ++iz) {
        for (int ix = 0; ix < ncx; ++ix) {
            int cx = cx0 + ix, cz = cz0 + iz;
            k_nf<<<1, 1>>>(seed, cx, cz, d_st, d_primer, d_sc, d_noise);
            die_cuda("launch nf", cudaGetLastError());
            die_cuda("sync nf", cudaDeviceSynchronize());
            die_cuda("cpy primer",
                     cudaMemcpy(h_primer, d_primer, sizeof(CpnPrimer),
                                cudaMemcpyDeviceToHost));
            for (int lx = 0; lx < 16; ++lx) {
                for (int lz = 0; lz < 16; ++lz) {
                    for (int y = 0; y < 256; ++y) {
                        int id = (int)h_primer->data[cpn_idx(lx, y, lz)];
                        if (!id) continue;
                        fprintf(out, "%d,%d,%d,%d\n",
                                cx * 16 + lx, y, cz * 16 + lz, id);
                        ++non_air;
                    }
                }
            }
        }
    }
    fprintf(stderr,
            "dim_region_dump_cuda nether seed=%lld chunks=(%d,%d)+%dx%d non_air=%lld\n",
            (long long)seed, cx0, cz0, ncx, ncz, non_air);

    free(h_st);
    free(h_noise);
    free(h_primer);
    cudaFree(d_st);
    cudaFree(d_primer);
    cudaFree(d_sc);
    cudaFree(d_noise);
    return 0;
}

static int dump_end(i64 seed, int cx0, int cz0, int ncx, int ncz, FILE *out) {
    CpePrimer *h_primer = (CpePrimer *)malloc(sizeof(CpePrimer));
    if (!h_primer) return 2;
    CpePrimer *d_primer = nullptr;
    CpeScratch *d_sc = nullptr;
    die_cuda("malloc primer", cudaMalloc(&d_primer, sizeof(CpePrimer)));
    die_cuda("malloc sc", cudaMalloc(&d_sc, sizeof(CpeScratch)));
    die_cuda("stack", cudaDeviceSetLimit(cudaLimitStackSize, (size_t)128 * 1024));

    long long non_air = 0;
    for (int iz = 0; iz < ncz; ++iz) {
        for (int ix = 0; ix < ncx; ++ix) {
            int cx = cx0 + ix, cz = cz0 + iz;
            k_cpe<<<1, 1>>>(seed, cx, cz, d_primer, d_sc);
            die_cuda("launch cpe", cudaGetLastError());
            die_cuda("sync cpe", cudaDeviceSynchronize());
            die_cuda("cpy primer",
                     cudaMemcpy(h_primer, d_primer, sizeof(CpePrimer),
                                cudaMemcpyDeviceToHost));
            for (int lx = 0; lx < 16; ++lx) {
                for (int lz = 0; lz < 16; ++lz) {
                    for (int y = 0; y < 256; ++y) {
                        int id = ce_to_van(cpe_get(h_primer, lx, y, lz));
                        if (!id) continue;
                        fprintf(out, "%d,%d,%d,%d\n",
                                cx * 16 + lx, y, cz * 16 + lz, id);
                        ++non_air;
                    }
                }
            }
        }
    }
    fprintf(stderr,
            "dim_region_dump_cuda end seed=%lld chunks=(%d,%d)+%dx%d non_air=%lld\n",
            (long long)seed, cx0, cz0, ncx, ncz, non_air);

    free(h_primer);
    cudaFree(d_primer);
    cudaFree(d_sc);
    return 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s nether|end <seed> <cx0> <cz0> <ncx> <ncz> [-o out.txt]\n",
            argv0);
}

int main(int argc, char **argv) {
    if (argc < 7) {
        usage(argv[0]);
        return 2;
    }
    const char *mode = argv[1];
    if (strcmp(mode, "nether") && strcmp(mode, "end")) {
        usage(argv[0]);
        return 2;
    }
    i64 seed = strtoll(argv[2], 0, 10);
    int cx0 = atoi(argv[3]), cz0 = atoi(argv[4]);
    int ncx = atoi(argv[5]), ncz = atoi(argv[6]);
    const char *out_path = "-";
    for (int i = 7; i < argc; ++i) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out_path = argv[++i];
        else {
            fprintf(stderr, "bad arg %s\n", argv[i]);
            return 2;
        }
    }
    if (ncx <= 0 || ncz <= 0) {
        fprintf(stderr, "bad ncx/ncz\n");
        return 2;
    }
    FILE *out = open_out(out_path);
    int rc;
    if (!strcmp(mode, "nether"))
        rc = dump_nether(seed, cx0, cz0, ncx, ncz, out);
    else
        rc = dump_end(seed, cx0, cz0, ncx, ncz, out);
    if (out != stdout) fclose(out);
    return rc;
}
