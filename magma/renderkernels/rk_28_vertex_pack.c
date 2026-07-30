/* rk_28_vertex_pack.c - compute core of render-opt kernel 28_vertex_pack.
 * addVertexData/putBrightness4/getColorIndex/putColorMultiplier + java_f2i copied VERBATIM
 * from candidate.c; only main()/stdio removed and wrapped by rk_vertex_pack. The file-static
 * buf/vertexCount scratch is reset per call (single-threaded use). Build with -ffp-contract=off. */
#include "rk.h"
#include <string.h>
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

void rk_vertex_pack(const int32_t data[28], const int32_t bright[4],
                    const float cmul[4], int32_t out[28]) {
    vertexCount = 0;
    addVertexData(data);
    putBrightness4(bright[0], bright[1], bright[2], bright[3]);
    putColorMultiplier(cmul[0], cmul[0], cmul[0], 4);
    putColorMultiplier(cmul[1], cmul[1], cmul[1], 3);
    putColorMultiplier(cmul[2], cmul[2], cmul[2], 2);
    putColorMultiplier(cmul[3], cmul[3], cmul[3], 1);
    memcpy(out, buf, 28 * sizeof(int32_t));
}
