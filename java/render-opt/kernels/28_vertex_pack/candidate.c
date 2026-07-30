/* CANDIDATE: C port of MC VertexBuffer quad-pack (addVertexData + putBrightness4 +
 * putColorMultiplier) for DefaultVertexFormats.BLOCK (7 ints/vertex, 28 ints/quad).
 * Must BITWISE-match golden/Golden.java.
 *
 * Layout per quad (28 ints): vertex v in [0,3] -> ints [7*v .. 7*v+6]:
 *   +0,+1,+2 = position floats, +3 = packed color (RGBA bytes), +4,+5 = tex floats,
 *   +6 = packed lightmap. So brightness lands at ints 6,13,20,27; color at 3,10,17,24.
 * putColorMultiplier index = ((vertexCount - vertexIndex)*28 + 12)/4 = (4-vertexIndex)*7 + 3,
 * called with vertexIndex 4,3,2,1 -> ints 3,10,17,24. Little-endian branch (assume LE host;
 * golden uses ByteOrder.nativeOrder() which is LE on Mac/anvil).
 *
 * Input line: 28 decimal int32 (quad data) | 4 decimal int32 (brightness) | 4 hex float-bits.
 * Output: 28 decimal ints. Float channel multiply uses java_f2i truncation (-ffp-contract=off). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* Java narrowing float->int (JLS 5.1.3): NaN->0, saturate, else truncate toward zero. */
static int java_f2i(float f) {
    if (f != f) return 0;
    if (f >= 2147483648.0f) return INT_MAX;
    if (f <= -2147483648.0f) return INT_MIN;
    return (int)f;
}

static int32_t buf[28];
static int vertexCount;

static void addVertexData(const int32_t *vertexData) {
    /* position = getBufferSize() = vertexCount*7 (0 on empty); copy 28 ints */
    memcpy(buf, vertexData, 28 * sizeof(int32_t));
    vertexCount += 28 / 7;
}

static void putBrightness4(int32_t b1, int32_t b2, int32_t b3, int32_t b4) {
    int i = (vertexCount - 4) * 7 + 24 / 4;   /* 6 */
    int j = 28 >> 2;                          /* 7 */
    buf[i] = b1;
    buf[i + j] = b2;
    buf[i + j * 2] = b3;
    buf[i + j * 3] = b4;
}

static int getColorIndex(int vertexIndex) {
    return ((vertexCount - vertexIndex) * 28 + 12) / 4;
}

static void putColorMultiplier(float red, float green, float blue, int vertexIndex) {
    int i = getColorIndex(vertexIndex);
    int32_t j = buf[i];
    /* little-endian branch */
    int32_t k  = java_f2i((float)(j & 255) * red);
    int32_t l  = java_f2i((float)(j >> 8 & 255) * green);
    int32_t i1 = java_f2i((float)(j >> 16 & 255) * blue);
    j = j & -16777216;
    j = j | i1 << 16 | l << 8 | k;
    buf[i] = j;
}

static float bits_to_float(const char *hex) {
    uint32_t b = (uint32_t)strtoul(hex, NULL, 16);
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

int main(void) {
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        int32_t data[28];
        int32_t bright[4];
        char h[4][32];
        char *p = line;
        int ok = 1, consumed;
        for (int n = 0; n < 28; ++n) {
            long v;
            if (sscanf(p, "%ld%n", &v, &consumed) != 1) { ok = 0; break; }
            data[n] = (int32_t)v; p += consumed;
        }
        if (!ok) continue;
        for (int n = 0; n < 4; ++n) {
            long v;
            if (sscanf(p, "%ld%n", &v, &consumed) != 1) { ok = 0; break; }
            bright[n] = (int32_t)v; p += consumed;
        }
        if (!ok) continue;
        for (int n = 0; n < 4; ++n) {
            if (sscanf(p, "%31s%n", h[n], &consumed) != 1) { ok = 0; break; }
            p += consumed;
        }
        if (!ok) continue;
        float c[4];
        for (int n = 0; n < 4; ++n) c[n] = bits_to_float(h[n]);

        vertexCount = 0;
        addVertexData(data);
        putBrightness4(bright[0], bright[1], bright[2], bright[3]);
        putColorMultiplier(c[0], c[0], c[0], 4);
        putColorMultiplier(c[1], c[1], c[1], 3);
        putColorMultiplier(c[2], c[2], c[2], 2);
        putColorMultiplier(c[3], c[3], c[3], 1);

        for (int n = 0; n < 28; ++n)
            printf("%d%c", buf[n], n == 27 ? '\n' : ' ');
    }
    return 0;
}
