/* tests/test_frustum.c - I/O driver that exercises core/frustum.h through the
 * EXACT stdin/stdout hex format of the two bit-verified render-opt kernels, so a
 * plain diff against those kernels' own candidate binaries anchors our port to
 * their goldens (ClippingHelperImpl.init / ClippingHelper.isBoxInFrustum).
 *
 *   --mode extract : each line = 32 raw-float-bits hex ints (16 proj + 16 mv),
 *                    output = 24 hex ints (6 planes x 4 coeffs).
 *   --mode aabb    : each line = 24 float-bits + 6 double-bits, output = "1"/"0".
 *
 * The math itself lives in core/frustum.h; this file is only glue. Build with
 * -ffp-contract=off (the Makefile target does) so extract() stays bit-faithful. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "core/frustum.h"

static float bits_to_f(uint32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }
static double bits_to_d(uint64_t b) { double d; memcpy(&d, &b, sizeof d); return d; }
static uint32_t f_to_bits(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

static int run_extract(void) {
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        unsigned long t[32];
        int n = sscanf(line,
            "%lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx "
            "%lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx",
            &t[0],&t[1],&t[2],&t[3],&t[4],&t[5],&t[6],&t[7],
            &t[8],&t[9],&t[10],&t[11],&t[12],&t[13],&t[14],&t[15],
            &t[16],&t[17],&t[18],&t[19],&t[20],&t[21],&t[22],&t[23],
            &t[24],&t[25],&t[26],&t[27],&t[28],&t[29],&t[30],&t[31]);
        if (n != 32) continue;
        float proj[16], mv[16], planes[6][4];
        for (int i = 0; i < 16; ++i) proj[i] = bits_to_f((uint32_t)t[i]);
        for (int i = 0; i < 16; ++i) mv[i] = bits_to_f((uint32_t)t[16 + i]);
        cr_frustum_extract(proj, mv, planes);
        for (int i = 0; i < 24; ++i) {
            if (i > 0) putchar(' ');
            printf("%x", f_to_bits(planes[i / 4][i % 4]));
        }
        putchar('\n');
    }
    return 0;
}

static int run_aabb(void) {
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
        int r = cr_aabb_in_frustum(frustum, b[0], b[1], b[2], b[3], b[4], b[5]);
        putchar(r ? '1' : '0');
        putchar('\n');
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *mode = (argc > 2 && !strcmp(argv[1], "--mode")) ? argv[2] : "extract";
    if (!strcmp(mode, "aabb")) return run_aabb();
    return run_extract();
}
