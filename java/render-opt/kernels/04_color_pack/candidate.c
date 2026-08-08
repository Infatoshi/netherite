/* CANDIDATE: C port of MC rgb(int,int,int) + multiplyColor(int,int). Must BITWISE-match golden.
 * Reads "r g b colorA colorB" per line; prints "rgb(r,g,b) multiplyColor(colorA,colorB)".
 * Additive/shift ops use uint32_t to match Java two's-complement wrap (C signed overflow is UB).
 * The float blend in multiplyColor must build with -ffp-contract=off (runner does this). */
#include <stdio.h>
#include <stdint.h>

static int32_t rgb(int32_t rIn, int32_t gIn, int32_t bIn) {
    uint32_t v = ((uint32_t)rIn << 8) + (uint32_t)gIn;
    v = (v << 8) + (uint32_t)bIn;
    return (int32_t)v;
}

static int32_t multiplyColor(int32_t a, int32_t b) {
    int32_t i  = (a & 16711680) >> 16;
    int32_t j  = (b & 16711680) >> 16;
    int32_t k  = (a & 65280) >> 8;
    int32_t l  = (b & 65280) >> 8;
    int32_t i1 = (a & 255) >> 0;
    int32_t j1 = (b & 255) >> 0;
    int32_t k1 = (int32_t)((float)i  * (float)j  / 255.0f);
    int32_t l1 = (int32_t)((float)k  * (float)l  / 255.0f);
    int32_t i2 = (int32_t)((float)i1 * (float)j1 / 255.0f);
    uint32_t r = ((uint32_t)a & 0xFF000000u)
               | ((uint32_t)k1 << 16)
               | ((uint32_t)l1 << 8)
               | (uint32_t)i2;
    return (int32_t)r;
}

int main(void) {
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        long r, g, b, ca, cb;
        if (sscanf(line, "%ld %ld %ld %ld %ld", &r, &g, &b, &ca, &cb) != 5) continue;
        printf("%d %d\n", rgb((int32_t)r, (int32_t)g, (int32_t)b),
                          multiplyColor((int32_t)ca, (int32_t)cb));
    }
    return 0;
}
