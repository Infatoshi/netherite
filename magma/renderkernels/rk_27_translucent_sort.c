/* rk_27_translucent_sort.c - compute core of render-opt kernel 27_translucent_sort.
 * getDistanceSq/floatToIntBits/floatCompare/cmp copied VERBATIM from candidate.c; only
 * main()/stdio removed and wrapped by rk_translucent_sort. The file-static scratch (afloat)
 * mirrors the original's globals; single-threaded use. Build with -ffp-contract=off. */
#include "rk.h"
#include <stdlib.h>
#include <string.h>

static float *afloat;

/* Java Float.floatToIntBits: collapse all NaN to 0x7fc00000, keep -0.0 as 0x80000000. */
static int32_t floatToIntBits(float f) {
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    if ((b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu) != 0) b = 0x7fc00000u;
    return (int32_t)b;
}

/* Java Float.compare(f1,f2). */
static int floatCompare(float f1, float f2) {
    if (f1 < f2) return -1;
    if (f1 > f2) return 1;
    int32_t b1 = floatToIntBits(f1), b2 = floatToIntBits(f2);
    if (b1 == b2) return 0;
    return b1 < b2 ? -1 : 1;
}

/* verbatim getDistanceSq (FloatBuffer -> float*) */
static float getDistanceSq(const float *p0, float p1, float p2, float p3, int p4, int p5) {
    float f   = p0[p5 + p4 * 0 + 0];
    float f1  = p0[p5 + p4 * 0 + 1];
    float f2  = p0[p5 + p4 * 0 + 2];
    float f3  = p0[p5 + p4 * 1 + 0];
    float f4  = p0[p5 + p4 * 1 + 1];
    float f5  = p0[p5 + p4 * 1 + 2];
    float f6  = p0[p5 + p4 * 2 + 0];
    float f7  = p0[p5 + p4 * 2 + 1];
    float f8  = p0[p5 + p4 * 2 + 2];
    float f9  = p0[p5 + p4 * 3 + 0];
    float f10 = p0[p5 + p4 * 3 + 1];
    float f11 = p0[p5 + p4 * 3 + 2];
    float f12 = (f + f3 + f6 + f9) * 0.25F - p1;
    float f13 = (f1 + f4 + f7 + f10) * 0.25F - p2;
    float f14 = (f2 + f5 + f8 + f11) * 0.25F - p3;
    return f12 * f12 + f13 * f13 + f14 * f14;
}

static int cmp(const void *pa, const void *pb) {
    int a = *(const int *)pa, b = *(const int *)pb;
    int r = floatCompare(afloat[b], afloat[a]);   /* descending distance */
    if (r != 0) return r;
    return a < b ? -1 : (a > b ? 1 : 0);           /* stable: ascending original index */
}

void rk_translucent_sort(const float *floatbuf, int n,
                         float camX, float camY, float camZ, int *out_idx) {
    afloat = malloc((size_t)n * sizeof(float));
    for (int j = 0; j < n; ++j)
        afloat[j] = getDistanceSq(floatbuf, camX, camY, camZ, 7, j * 28);

    for (int k = 0; k < n; ++k) out_idx[k] = k;
    qsort(out_idx, (size_t)n, sizeof(int), cmp);

    free(afloat);
    afloat = NULL;
}
