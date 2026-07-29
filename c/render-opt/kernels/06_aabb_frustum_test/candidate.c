/* CANDIDATE: C port of MC 1.11.2 ClippingHelper.isBoxInFrustum + dot.
 * Input (per line): 24 raw-float-bits hex ints (6 planes x 4), then 6 raw-double-bits hex longs
 *   = AABB (minX, minY, minZ, maxX, maxY, maxZ).
 * Output: "1" if box inside frustum, else "0". Must BITWISE-match golden/Golden.java.
 * dot() promotes float coeffs to double; op order preserved; build with -ffp-contract=off. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static float bits_to_f(uint32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }
static double bits_to_d(uint64_t b) { double d; memcpy(&d, &b, sizeof d); return d; }

static double dot(const float *p, double x, double y, double z) {
    return (double)p[0] * x + (double)p[1] * y + (double)p[2] * z + (double)p[3];
}

static int isBoxInFrustum(float frustum[6][4],
                          double p1, double p3, double p5,
                          double p7, double p9, double p11) {
    for (int i = 0; i < 6; ++i) {
        const float *afloat = frustum[i];
        if (dot(afloat, p1, p3, p5) <= 0.0 && dot(afloat, p7, p3, p5) <= 0.0 && dot(afloat, p1, p9, p5) <= 0.0 && dot(afloat, p7, p9, p5) <= 0.0 && dot(afloat, p1, p3, p11) <= 0.0 && dot(afloat, p7, p3, p11) <= 0.0 && dot(afloat, p1, p9, p11) <= 0.0 && dot(afloat, p7, p9, p11) <= 0.0) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        unsigned long ft[24];
        unsigned long long dt[6];
        int n = sscanf(line,
            "%lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx "
            "%lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx "
            "%llx %llx %llx %llx %llx %llx",
            &ft[0],&ft[1],&ft[2],&ft[3],&ft[4],&ft[5],&ft[6],&ft[7],
            &ft[8],&ft[9],&ft[10],&ft[11],&ft[12],&ft[13],&ft[14],&ft[15],
            &ft[16],&ft[17],&ft[18],&ft[19],&ft[20],&ft[21],&ft[22],&ft[23],
            &dt[0],&dt[1],&dt[2],&dt[3],&dt[4],&dt[5]);
        if (n != 30) continue;
        float frustum[6][4];
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 4; ++j)
                frustum[i][j] = bits_to_f((uint32_t)ft[i * 4 + j]);
        double b[6];
        for (int i = 0; i < 6; ++i) b[i] = bits_to_d((uint64_t)dt[i]);
        int r = isBoxInFrustum(frustum, b[0], b[1], b[2], b[3], b[4], b[5]);
        putchar(r ? '1' : '0');
        putchar('\n');
    }
    return 0;
}
