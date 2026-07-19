/* CUDA: SAME core/mc_math.h lookups as cpu/mathhelper.c. The SIN_TABLE is built on the host and
 * copied to the device so the table bytes are identical to the CPU path (CPU==CUDA by construction).
 * Single thread does the sweep; host prints in identical format. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/mathhelper.h"

__global__ void run_mathhelper(const McSinTable *st, unsigned int *trig, int *flr) {
    if (threadIdx.x || blockIdx.x) return;
    for (int i = 0; i < MH_NT; ++i) {
        float v = (float)((i - 6283) * 0.01);
        float s = mc_sin(st, v), c = mc_cos(st, v);
        unsigned int sb, cb; memcpy(&sb, &s, 4); memcpy(&cb, &c, 4);
        trig[2 * i] = sb; trig[2 * i + 1] = cb;
    }
    for (int i = 0; i < MH_NF; ++i) {
        double d = (i - 1000) * 0.123;
        flr[i] = mc_floor(d);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    McSinTable *h_st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(h_st);
    McSinTable *d_st; cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);
    unsigned int *d_trig; cudaMalloc(&d_trig, sizeof(unsigned int) * 2 * MH_NT);
    int *d_flr; cudaMalloc(&d_flr, sizeof(int) * MH_NF);
    run_mathhelper<<<1, 1>>>(d_st, d_trig, d_flr);
    cudaDeviceSynchronize();
    unsigned int *trig = (unsigned int *)malloc(sizeof(unsigned int) * 2 * MH_NT);
    int *flr = (int *)malloc(sizeof(int) * MH_NF);
    cudaMemcpy(trig, d_trig, sizeof(unsigned int) * 2 * MH_NT, cudaMemcpyDeviceToHost);
    cudaMemcpy(flr, d_flr, sizeof(int) * MH_NF, cudaMemcpyDeviceToHost);
    for (int i = 0; i < MH_NT; ++i) {
        printf("%08x\n", trig[2 * i]);
        printf("%08x\n", trig[2 * i + 1]);
    }
    for (int i = 0; i < MH_NF; ++i) printf("%d\n", flr[i]);
    free(h_st); free(trig); free(flr);
    cudaFree(d_st); cudaFree(d_trig); cudaFree(d_flr);
    return 0;
}
