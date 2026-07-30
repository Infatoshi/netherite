/* CANDIDATE: C port of MC VertexBuffer.sortVertexData distance + index sort.
 * Must BITWISE-match golden/Golden.java (the permutation, one quad index per line).
 *
 * Distance: getDistanceSq verbatim (float math, -ffp-contract=off so the centroid average and
 * squared sum round like the JVM). Sort: DESCENDING distance, ties -> ascending original index.
 * Java uses a STABLE Arrays.sort on Integer[]; we reproduce it with a TOTAL-ORDER comparator
 * (primary = Float.compare(dist_b, dist_a); tie -> original index ascending), so a non-stable
 * qsort still yields the exact stable TimSort permutation. Float.compare semantics (-0.0 < +0.0,
 * NaN largest) are replicated for safety, though gen keeps all distances finite non-negative.
 *
 * Input: line 1 = "n camXbits camYbits camZbits" (hex float-bits); then n lines of 28 hex int32. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int N;
static float *afloat;

static float bits_to_float(const char *hex) {
    uint32_t b = (uint32_t)strtoul(hex, NULL, 16);
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

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

int main(void) {
    char line[1024];
    if (!fgets(line, sizeof line, stdin)) return 0;
    char hx[3][32];
    if (sscanf(line, "%d %31s %31s %31s", &N, hx[0], hx[1], hx[2]) != 4) return 0;
    float camX = bits_to_float(hx[0]), camY = bits_to_float(hx[1]), camZ = bits_to_float(hx[2]);

    float *floatbuf = malloc((size_t)N * 28 * sizeof(float));
    for (int q = 0; q < N; ++q) {
        if (!fgets(line, sizeof line, stdin)) { N = q; break; }
        char *p = line; int consumed;
        for (int n = 0; n < 28; ++n) {
            char h[32];
            sscanf(p, "%31s%n", h, &consumed);
            p += consumed;
            floatbuf[q * 28 + n] = bits_to_float(h);
        }
    }

    afloat = malloc((size_t)N * sizeof(float));
    for (int j = 0; j < N; ++j)
        afloat[j] = getDistanceSq(floatbuf, camX, camY, camZ, 7, j * 28);

    int *idx = malloc((size_t)N * sizeof(int));
    for (int k = 0; k < N; ++k) idx[k] = k;
    qsort(idx, (size_t)N, sizeof(int), cmp);

    for (int k = 0; k < N; ++k) printf("%d\n", idx[k]);

    free(floatbuf); free(afloat); free(idx);
    return 0;
}
