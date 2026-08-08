/* CANDIDATE: C port of MC atan2() (LUT-based) + fastInvSqrt. Must BITWISE-match golden/Golden.java.
 * Rebuilds FRAC_BIAS / ASINE_TAB / COS_TAB exactly, then runs the same arithmetic.
 * atan2 signature is atan2(y, x): reads "y x" doubles per line; prints the decimal result value.
 * Build with -ffp-contract=off (runner does) so all double ops round like the JVM.
 * Compare is "tol" (not bitwise): the asin/cos LUT is seeded by libm asin/cos, which differs from
 * Java StrictMath by <=1 ULP on a few entries, so a recomputed table can't be bit-identical. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double FRAC_BIAS;
static double ASINE_TAB[257];
static double COS_TAB[257];

static double bits_to_double(uint64_t b) {
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}

static void build_tables(void) {
    FRAC_BIAS = bits_to_double(4805340802404319232ULL);
    for (int j = 0; j < 257; ++j) {
        double d0 = (double)j / 256.0;
        double d1 = asin(d0);
        COS_TAB[j] = cos(d1);
        ASINE_TAB[j] = d1;
    }
}

static double fastInvSqrt(double p) {
    double d0 = 0.5 * p;
    int64_t i;
    memcpy(&i, &p, sizeof i);
    i = 6910469410427058090LL - (i >> 1);   /* arithmetic shift, matches Java signed >> */
    memcpy(&p, &i, sizeof p);
    p = p * (1.5 - d0 * p * p);
    return p;
}

static double mc_atan2(double y, double x) {
    double d0 = x * x + y * y;

    if (d0 != d0) {                              /* Double.isNaN(d0) */
        return bits_to_double(0x7ff8000000000000ULL); /* Java Double.NaN bits */
    } else {
        int flag = y < 0.0;
        if (flag) y = -y;
        int flag1 = x < 0.0;
        if (flag1) x = -x;
        int flag2 = y > x;
        if (flag2) {
            double d1 = x;
            x = y;
            y = d1;
        }
        double d9 = fastInvSqrt(d0);
        x = x * d9;
        y = y * d9;
        double d2 = FRAC_BIAS + y;
        uint64_t db;
        memcpy(&db, &d2, sizeof db);
        int i = (int)(uint32_t)db;              /* (int)Double.doubleToRawLongBits(d2): low 32 bits */
        double d3 = ASINE_TAB[i];
        double d4 = COS_TAB[i];
        double d5 = d2 - FRAC_BIAS;
        double d6 = y * d4 - x * d5;
        double d7 = (6.0 + d6 * d6) * d6 * 0.16666666666666666;
        double d8 = d3 + d7;
        if (flag2) d8 = (M_PI / 2.0) - d8;
        if (flag1) d8 = M_PI - d8;
        if (flag) d8 = -d8;
        return d8;
    }
}

int main(void) {
    build_tables();
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        double y, x;
        if (sscanf(line, "%lf %lf", &y, &x) != 2) continue;
        double r = mc_atan2(y, x);
        printf("%.17g\n", r);
    }
    return 0;
}
