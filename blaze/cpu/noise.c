/* CPU reference: 16-octave Perlin noise over a 5x33x5 chunk-noise region (the overworld terrain
 * shaping params, scale 684.412). Prints raw IEEE754 bits as hex so comparison is exact. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/mc_noise.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int xs = 5, ys = 33, zs = 5;
    double sc = 684.412;
    JavaRandom r; jrand_set(&r, seed);
    NoiseOctaves o; mc_oct_init(&o, &r, 16);
    double *arr = (double *)malloc(sizeof(double) * xs * ys * zs);
    mc_oct_generate(&o, arr, 0, 0, 0, xs, ys, zs, sc, sc, sc);
    for (int i = 0; i < xs * ys * zs; i++) {
        u64 bits; memcpy(&bits, &arr[i], 8);
        printf("%016llx\n", (unsigned long long)bits);
    }
    free(arr);
    return 0;
}
