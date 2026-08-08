/* test_rk.c - bitwise self-test for the renderkernels library.
 *
 * For each covered kernel it reads the render-opt golden inputs, runs the rk_* function, and
 * asserts the output equals the golden output BITWISE (exact integer values / exact IEEE-754
 * raw bits). Prints per-kernel PASS/FAIL and an overall PASS; exits nonzero if any fail.
 *
 * Golden sources (absolute paths):
 *   12,14,18,21  -> captured golden/inputs.txt + golden.txt under render-opt/kernels/<dir>/golden/
 *   13,15,28     -> regenerated here into renderkernels/testdata/ (those kernels ship only a
 *                   Golden.java + gen_inputs.py; k*_inputs.txt was produced by gen_inputs.py and
 *                   k*_golden.txt by running the verbatim Golden.java, so the compare is still
 *                   against the real MC arithmetic, bit-for-bit).
 */
#include "rk.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define RO "../../java/render-opt/kernels/"
#define TD "testdata/"

static float  f_of_i(int32_t b) { float f; memcpy(&f, &b, 4); return f; }
static int32_t b_of_f(float f)  { int32_t b; memcpy(&b, &f, 4); return b; }
static float  f_of_u(uint32_t b){ float f; memcpy(&f, &b, 4); return f; }

static int fails_total = 0;

static FILE *xopen(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "  cannot open %s\n", path); }
    return f;
}

/* ---- kernel 12: ao_vertex_brightness ---- */
static int test_12(void) {
    FILE *in = xopen(RO "12_ao_vertex_brightness/golden/inputs.txt");
    FILE *go = xopen(RO "12_ao_vertex_brightness/golden/golden.txt");
    if (!in || !go) return 1;
    int face, s0, s1, n = 0, bad = 0;
    while (fscanf(in, "%d %d %d", &face, &s0, &s1) == 3) {
        int32_t fsb[12]; int32_t g[9]; int32_t fb[9];
        for (int z = 0; z < 12; z++) if (fscanf(in, "%d", &fsb[z]) != 1) { fclose(in); fclose(go); return 1; }
        for (int z = 0; z < 9;  z++) if (fscanf(in, "%d", &g[z])   != 1) { fclose(in); fclose(go); return 1; }
        for (int z = 0; z < 9;  z++) if (fscanf(in, "%d", &fb[z])  != 1) { fclose(in); fclose(go); return 1; }
        float faceShape[12];
        for (int z = 0; z < 12; z++) faceShape[z] = f_of_i(fsb[z]);
        float f[9]; for (int z = 0; z < 9; z++) f[z] = f_of_i(fb[z]);
        int vb[4]; float vcm[4];
        rk_ao_vertex_brightness(face, s1, faceShape,
            g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8],
            f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], vb, vcm);
        int32_t gv;
        for (int z = 0; z < 4; z++) { if (fscanf(go, "%d", &gv) != 1) { bad++; break; } if (gv != vb[z]) bad++; }
        for (int z = 0; z < 4; z++) { if (fscanf(go, "%d", &gv) != 1) { bad++; break; } if (gv != b_of_f(vcm[z])) bad++; }
        n++;
    }
    fclose(in); fclose(go);
    printf("  kernel 12 ao_vertex_brightness: %d records, %d mismatches -> %s\n", n, bad, bad ? "FAIL" : "PASS");
    fails_total += bad;
    return bad != 0;
}

/* ---- kernel 13: ao_pack_helpers ---- */
static int test_13(void) {
    FILE *in = xopen(TD "k13_inputs.txt");
    FILE *go = xopen(TD "k13_golden.txt");
    if (!in || !go) return 1;
    long b1, b2, b3, b4; unsigned h1, h2, h3, h4; int n = 0, bad = 0;
    while (fscanf(in, "%ld %ld %ld %ld %x %x %x %x", &b1, &b2, &b3, &b4, &h1, &h2, &h3, &h4) == 8) {
        int32_t ao = rk_ao_get_ao_brightness((int32_t)b1, (int32_t)b2, (int32_t)b3, (int32_t)b4);
        int32_t vb = rk_ao_get_vertex_brightness((int32_t)b1, (int32_t)b2, (int32_t)b3, (int32_t)b4,
                                                 f_of_u(h1), f_of_u(h2), f_of_u(h3), f_of_u(h4));
        int gao, gvb;
        if (fscanf(go, "%d %d", &gao, &gvb) != 2) { bad++; break; }
        if (gao != ao || gvb != vb) bad++;
        n++;
    }
    fclose(in); fclose(go);
    printf("  kernel 13 ao_pack_helpers:      %d records, %d mismatches -> %s\n", n, bad, bad ? "FAIL" : "PASS");
    fails_total += bad;
    return bad != 0;
}

/* ---- kernel 14: light_query ---- */
static int test_14(void) {
    FILE *in = xopen(RO "14_light_query/golden/inputs.txt");
    FILE *go = xopen(RO "14_light_query/golden/golden.txt");
    if (!in || !go) return 1;
    int type, nb, up, east, west, south, north, own, n = 0, bad = 0;
    while (fscanf(in, "%d %d %d %d %d %d %d %d", &type, &nb, &up, &east, &west, &south, &north, &own) == 8) {
        int r = rk_light_query(nb, up, east, west, south, north, own);
        int g; if (fscanf(go, "%d", &g) != 1) { bad++; break; }
        if (g != r) bad++;
        n++;
    }
    fclose(in); fclose(go);
    printf("  kernel 14 light_query:          %d records, %d mismatches -> %s\n", n, bad, bad ? "FAIL" : "PASS");
    fails_total += bad;
    return bad != 0;
}

/* ---- kernel 15: light_combine_pack ---- */
static int test_15(void) {
    FILE *in = xopen(TD "k15_inputs.txt");
    FILE *go = xopen(TD "k15_golden.txt");
    if (!in || !go) return 1;
    long sky, block, ov; int n = 0, bad = 0;
    while (fscanf(in, "%ld %ld %ld", &sky, &block, &ov) == 3) {
        int32_t r = rk_light_combine_pack((int32_t)sky, (int32_t)block, (int32_t)ov);
        int g; if (fscanf(go, "%d", &g) != 1) { bad++; break; }
        if (g != r) bad++;
        n++;
    }
    fclose(in); fclose(go);
    printf("  kernel 15 light_combine_pack:   %d records, %d mismatches -> %s\n", n, bad, bad ? "FAIL" : "PASS");
    fails_total += bad;
    return bad != 0;
}

/* ---- kernel 18: biome_color_blend ---- */
static int test_18(void) {
    FILE *in = xopen(RO "18_biome_color_blend/golden/inputs.txt");
    FILE *go = xopen(RO "18_biome_color_blend/golden/golden.txt");
    if (!in || !go) return 1;
    int c[9], n = 0, bad = 0;
    while (fscanf(in, "%d %d %d %d %d %d %d %d %d",
                  &c[0],&c[1],&c[2],&c[3],&c[4],&c[5],&c[6],&c[7],&c[8]) == 9) {
        int r = rk_biome_color_blend(c);
        int g; if (fscanf(go, "%d", &g) != 1) { bad++; break; }
        if (g != r) bad++;
        n++;
    }
    fclose(in); fclose(go);
    printf("  kernel 18 biome_color_blend:    %d records, %d mismatches -> %s\n", n, bad, bad ? "FAIL" : "PASS");
    fails_total += bad;
    return bad != 0;
}

/* ---- kernel 21: should_side_render ---- */
static int test_21(void) {
    FILE *in = xopen(RO "21_should_side_render/golden/inputs.txt");
    FILE *go = xopen(RO "21_should_side_render/golden/golden.txt");
    if (!in || !go) return 1;
    int side, nbr, n = 0, bad = 0;
    long long mnx, mny, mnz, mxx, mxy, mxz;
    while (fscanf(in, "%d %lld %lld %lld %lld %lld %lld %d",
                  &side, &mnx, &mny, &mnz, &mxx, &mxy, &mxz, &nbr) == 8) {
        double minX, minY, minZ, maxX, maxY, maxZ;
        memcpy(&minX, &mnx, 8); memcpy(&minY, &mny, 8); memcpy(&minZ, &mnz, 8);
        memcpy(&maxX, &mxx, 8); memcpy(&maxY, &mxy, 8); memcpy(&maxZ, &mxz, 8);
        int r = rk_should_side_render(side, minX, minY, minZ, maxX, maxY, maxZ, nbr);
        int g; if (fscanf(go, "%d", &g) != 1) { bad++; break; }
        if (g != r) bad++;
        n++;
    }
    fclose(in); fclose(go);
    printf("  kernel 21 should_side_render:   %d records, %d mismatches -> %s\n", n, bad, bad ? "FAIL" : "PASS");
    fails_total += bad;
    return bad != 0;
}

/* ---- kernel 28: vertex_pack ---- */
static int test_28(void) {
    FILE *in = xopen(TD "k28_inputs.txt");
    FILE *go = xopen(TD "k28_golden.txt");
    if (!in || !go) return 1;
    int n = 0, bad = 0;
    for (;;) {
        int32_t data[28], bright[4]; unsigned hx[4]; float cmul[4];
        int ok = 1;
        for (int z = 0; z < 28; z++) { long v; if (fscanf(in, "%ld", &v) != 1) { ok = 0; break; } data[z] = (int32_t)v; }
        if (!ok) break;
        for (int z = 0; z < 4; z++) { long v; if (fscanf(in, "%ld", &v) != 1) { ok = 0; break; } bright[z] = (int32_t)v; }
        for (int z = 0; z < 4; z++) { if (fscanf(in, "%x", &hx[z]) != 1) { ok = 0; break; } cmul[z] = f_of_u(hx[z]); }
        if (!ok) break;
        int32_t out[28];
        rk_vertex_pack(data, bright, cmul, out);
        for (int z = 0; z < 28; z++) { int g; if (fscanf(go, "%d", &g) != 1) { bad++; ok = 0; break; } if (g != out[z]) bad++; }
        if (!ok) break;
        n++;
    }
    fclose(in); fclose(go);
    printf("  kernel 28 vertex_pack:          %d records, %d mismatches -> %s\n", n, bad, bad ? "FAIL" : "PASS");
    fails_total += bad;
    return bad != 0;
}

int main(void) {
    printf("renderkernels bitwise self-test\n");
    int any = 0;
    any |= test_12();
    any |= test_13();
    any |= test_14();
    any |= test_15();
    any |= test_18();
    any |= test_21();
    any |= test_28();
    printf("----\n");
    if (any || fails_total) {
        printf("OVERALL: FAIL (%d total bitwise mismatches)\n", fails_total);
        return 1;
    }
    printf("OVERALL: PASS (all covered kernels bit-exact)\n");
    printf("Ported-but-not-bit-tested here: 07,08,27,31,32,33,34 "
           "(no captured golden/inputs.txt+golden.txt pair; verified standalone in render-opt).\n");
    return 0;
}
