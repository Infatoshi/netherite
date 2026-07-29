/* CANDIDATE: C port of MC BlockModelRenderer getAoBrightness / getVertexBrightness.
 * Must BITWISE-match golden/Golden.java. Input line: "br1 br2 br3 br4 f1hex f2hex f3hex f4hex"
 * (4 signed decimal int32 + 4 raw-bits-hex floats). Output: "<ao> <vb>" two decimal ints.
 *
 * Java int arithmetic wraps on overflow; the br1+br2+br3+br4 sum is done in uint32_t then
 * reinterpreted as int32_t so the arithmetic >>2 sign-extends exactly like Java. Float math is
 * done in `float` (not double) with -ffp-contract=off so the (int) truncation matches the JVM. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int32_t getAoBrightness(int32_t br1, int32_t br2, int32_t br3, int32_t br4) {
    if (br1 == 0) br1 = br4;
    if (br2 == 0) br2 = br4;
    if (br3 == 0) br3 = br4;
    uint32_t sum = (uint32_t)br1 + (uint32_t)br2 + (uint32_t)br3 + (uint32_t)br4;
    int32_t shifted = (int32_t)sum >> 2;   /* arithmetic shift, matches Java signed >> */
    return shifted & 16711935;
}

static int32_t getVertexBrightness(int32_t p1, int32_t p2, int32_t p3, int32_t p4,
                                   float f5, float f6, float f7, float f8) {
    int32_t i = (int32_t)((float)((p1 >> 16) & 255) * f5 + (float)((p2 >> 16) & 255) * f6
                        + (float)((p3 >> 16) & 255) * f7 + (float)((p4 >> 16) & 255) * f8) & 255;
    int32_t j = (int32_t)((float)(p1 & 255) * f5 + (float)(p2 & 255) * f6
                        + (float)(p3 & 255) * f7 + (float)(p4 & 255) * f8) & 255;
    return i << 16 | j;
}

static float bits_to_float(const char *hex) {
    uint32_t b = (uint32_t)strtoul(hex, NULL, 16);
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

int main(void) {
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        long b1, b2, b3, b4;
        char h1[32], h2[32], h3[32], h4[32];
        if (sscanf(line, "%ld %ld %ld %ld %31s %31s %31s %31s",
                   &b1, &b2, &b3, &b4, h1, h2, h3, h4) != 8) continue;
        float f1 = bits_to_float(h1), f2 = bits_to_float(h2);
        float f3 = bits_to_float(h3), f4 = bits_to_float(h4);
        int32_t ao = getAoBrightness((int32_t)b1, (int32_t)b2, (int32_t)b3, (int32_t)b4);
        int32_t vb = getVertexBrightness((int32_t)b1, (int32_t)b2, (int32_t)b3, (int32_t)b4,
                                         f1, f2, f3, f4);
        printf("%d %d\n", ao, vb);
    }
    return 0;
}
