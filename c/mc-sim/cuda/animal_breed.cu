/* CUDA driver for animal_breed: single-thread kernel, stdout byte-identical to the CPU driver. */
#include <cstdio>
#include <cstdlib>
#include "../core/animal_breed.h"

typedef struct {
    i32 age[AB_SLOTS];
    i32 inLove[AB_SLOTS];
    i32 isChild[AB_SLOTS];
    i32 present[AB_SLOTS];
} AbSnap;

__global__ void run_ab(i64 seed, i32 nticks, AbSnap *out) {
    if (threadIdx.x || blockIdx.x) return;
    AbState s;
    ab_init(&s);
    for (i32 t = 0; t < nticks; ++t) {
        ab_tape_tick(&s, seed, t);
        for (i32 i = 0; i < AB_SLOTS; ++i) {
            out[t].present[i] = s.a[i].present;
            out[t].age[i]     = s.a[i].growingAge;
            out[t].inLove[i]  = s.a[i].inLove;
            out[t].isChild[i] = ab_is_child(&s.a[i]);
        }
    }
}

int main(int argc, char **argv) {
    i64 seed   = (argc > 1) ? strtoll(argv[1], 0, 10) : 1LL;
    i32 nticks = (argc > 2) ? (i32)strtol(argv[2], 0, 10) : 200;
    AbSnap *d_out, *h_out = (AbSnap *)malloc(sizeof(AbSnap) * (size_t)nticks);
    cudaMalloc(&d_out, sizeof(AbSnap) * (size_t)nticks);
    run_ab<<<1, 1>>>(seed, nticks, d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(AbSnap) * (size_t)nticks, cudaMemcpyDeviceToHost);
    for (i32 t = 0; t < nticks; ++t) {
        for (i32 i = 0; i < AB_SLOTS; ++i) {
            if (i) putchar(' ');
            if (h_out[t].present[i])
                printf("%d %d %d", h_out[t].age[i], h_out[t].inLove[i], h_out[t].isChild[i]);
            else
                printf("0 0 0");
        }
        putchar('\n');
    }
    cudaFree(d_out);
    free(h_out);
    return 0;
}
