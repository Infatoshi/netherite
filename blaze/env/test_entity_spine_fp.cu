/* CPU vs CUDA bit-pattern test for primitives the living spine compiles:
 * MathHelper.sqrt = (float)sqrt((double)f) (Entity.java:1430 moveRelative)
 * and MathHelper.sin/cos table (already device via mc_sin).
 *
 * mc_atan2 is host-only and is not on the zero-intent spine path.
 *
 * Usage: test_entity_spine_fp [device]
 * Exit 0 iff every sample matches bitwise. */
#include <cstdio>
#include <cstring>
#include <cuda_runtime.h>
#include <math.h>

#include "mc_math.h"
#include "entity_spine.h"

#define N 256

__global__ void k_sqrt_f2d(float *out) {
    int i = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    if (i >= N) return;
    float f = (float)i * 0.01f;
    out[i] = (float)sqrt((double)f);
}

__global__ void k_sin_table(float *out, const McSinTable *st) {
    int i = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    if (i >= N) return;
    float yaw = (float)i * 0.017453292f;
    out[i] = mc_sin(st, yaw);
}

static int ck(cudaError_t e, const char *what) {
    if (e == cudaSuccess) return 0;
    fprintf(stderr, "FAIL: %s: %s\n", what, cudaGetErrorString(e));
    return 1;
}

int main(int argc, char **argv) {
    int device = 0, i, fails = 0;
    float h_sqrt[N], d_sqrt[N], h_sin[N], d_sin[N];
    float *g_sqrt = NULL, *g_sin = NULL;
    McSinTable st, *g_st = NULL;

    if (argc > 1) device = atoi(argv[1]);
    if (ck(cudaSetDevice(device), "cudaSetDevice")) return 2;
    mc_sin_table_init(&st);

    for (i = 0; i < N; ++i) {
        float f = (float)i * 0.01f;
        h_sqrt[i] = (float)sqrt((double)f);
        h_sin[i] = mc_sin(&st, (float)i * 0.017453292f);
    }
    if (ck(cudaMalloc(&g_sqrt, sizeof h_sqrt), "malloc sqrt") ||
        ck(cudaMalloc(&g_sin, sizeof h_sin), "malloc sin") ||
        ck(cudaMalloc(&g_st, sizeof st), "malloc st") ||
        ck(cudaMemcpy(g_st, &st, sizeof st, cudaMemcpyHostToDevice), "st"))
        return 2;
    k_sqrt_f2d<<<1, N>>>(g_sqrt);
    k_sin_table<<<1, N>>>(g_sin, g_st);
    if (ck(cudaDeviceSynchronize(), "sync") ||
        ck(cudaMemcpy(d_sqrt, g_sqrt, sizeof h_sqrt, cudaMemcpyDeviceToHost),
           "sqrt down") ||
        ck(cudaMemcpy(d_sin, g_sin, sizeof h_sin, cudaMemcpyDeviceToHost),
           "sin down"))
        return 2;

    for (i = 0; i < N; ++i) {
        uint32_t a, b;
        memcpy(&a, &h_sqrt[i], 4);
        memcpy(&b, &d_sqrt[i], 4);
        if (a != b) {
            fprintf(stderr, "FAIL sqrt i=%d cpu=%08x cuda=%08x\n", i, a, b);
            fails = 1;
            break;
        }
        memcpy(&a, &h_sin[i], 4);
        memcpy(&b, &d_sin[i], 4);
        if (a != b) {
            fprintf(stderr, "FAIL sin i=%d cpu=%08x cuda=%08x\n", i, a, b);
            fails = 1;
            break;
        }
    }
    cudaFree(g_sqrt);
    cudaFree(g_sin);
    cudaFree(g_st);
    if (fails) return 1;
    fprintf(stderr, "OK: %d sqrt + sin samples CPU==CUDA bitwise device=%d\n",
            N, device);
    return 0;
}
