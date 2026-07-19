/* CANDIDATE: C port of MC sin()/cos() table lookup. Must BITWISE-match golden/Golden.java.
 * Builds the same 65536-entry SIN_TABLE (float)sin((double)i*PI*2/65536), then indexes it.
 * Reads one float angle per line; prints "sinbits cosbits" (hex of floatToRawIntBits).
 * Build with -ffp-contract=off (runner does) so the index float-math rounds like the JVM. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float SIN_TABLE[65536];

static void build_table(void) {
    for (int i = 0; i < 65536; ++i) {
        SIN_TABLE[i] = (float)sin((double)i * M_PI * 2.0 / 65536.0);
    }
}

/* Java narrowing float->int (JLS 5.1.3): NaN->0, saturate to INT_MIN/INT_MAX out of range,
 * else round toward zero. C's plain (int) cast is UB out of range, so emulate. */
static int java_f2i(float f) {
    if (f != f) return 0;                       /* NaN */
    if (f >= 2147483648.0f) return INT_MAX;     /* >= 2^31 */
    if (f <= -2147483648.0f) return INT_MIN;    /* <= -2^31 */
    return (int)f;                              /* in range: truncate toward zero */
}

static float sin_lut(float value) {
    return SIN_TABLE[java_f2i(value * 10430.378f) & 65535];
}

static float cos_lut(float value) {
    return SIN_TABLE[java_f2i(value * 10430.378f + 16384.0f) & 65535];
}

int main(void) {
    build_table();
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        float v;
        if (sscanf(line, "%f", &v) != 1) continue;
        float s = sin_lut(v), c = cos_lut(v);
        uint32_t sb, cb;
        memcpy(&sb, &s, sizeof sb);
        memcpy(&cb, &c, sizeof cb);
        printf("%x %x\n", sb, cb);
    }
    return 0;
}
