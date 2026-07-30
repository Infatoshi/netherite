/* CANDIDATE: C port of MC 1.11.2 ClippingHelperImpl frustum-plane extraction (pure core).
 * Inputs (per line): 32 raw-float-bits hex ints = 16 projection floats, then 16 modelview floats.
 * Output: 24 hex ints = raw bits of the 6 normalized frustum planes (4 coeffs each).
 * Must BITWISE-match golden/Golden.java. Op order preserved; build with -ffp-contract=off. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

static float bits_to_f(uint32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }
static uint32_t f_to_bits(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

/* MathHelper.sqrt(float) = (float)Math.sqrt((double)value) */
static float mc_sqrt(float value) { return (float)sqrt((double)value); }

static void normalize(float *p) {
    float f = mc_sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    p[0] /= f;
    p[1] /= f;
    p[2] /= f;
    p[3] /= f;
}

static void extract(const float *projectionMatrix, const float *modelviewMatrix, float *out) {
    float frustum[6][4];
    float clippingMatrix[16];
    const float *afloat = projectionMatrix;
    const float *afloat1 = modelviewMatrix;
    clippingMatrix[0] = afloat1[0] * afloat[0] + afloat1[1] * afloat[4] + afloat1[2] * afloat[8] + afloat1[3] * afloat[12];
    clippingMatrix[1] = afloat1[0] * afloat[1] + afloat1[1] * afloat[5] + afloat1[2] * afloat[9] + afloat1[3] * afloat[13];
    clippingMatrix[2] = afloat1[0] * afloat[2] + afloat1[1] * afloat[6] + afloat1[2] * afloat[10] + afloat1[3] * afloat[14];
    clippingMatrix[3] = afloat1[0] * afloat[3] + afloat1[1] * afloat[7] + afloat1[2] * afloat[11] + afloat1[3] * afloat[15];
    clippingMatrix[4] = afloat1[4] * afloat[0] + afloat1[5] * afloat[4] + afloat1[6] * afloat[8] + afloat1[7] * afloat[12];
    clippingMatrix[5] = afloat1[4] * afloat[1] + afloat1[5] * afloat[5] + afloat1[6] * afloat[9] + afloat1[7] * afloat[13];
    clippingMatrix[6] = afloat1[4] * afloat[2] + afloat1[5] * afloat[6] + afloat1[6] * afloat[10] + afloat1[7] * afloat[14];
    clippingMatrix[7] = afloat1[4] * afloat[3] + afloat1[5] * afloat[7] + afloat1[6] * afloat[11] + afloat1[7] * afloat[15];
    clippingMatrix[8] = afloat1[8] * afloat[0] + afloat1[9] * afloat[4] + afloat1[10] * afloat[8] + afloat1[11] * afloat[12];
    clippingMatrix[9] = afloat1[8] * afloat[1] + afloat1[9] * afloat[5] + afloat1[10] * afloat[9] + afloat1[11] * afloat[13];
    clippingMatrix[10] = afloat1[8] * afloat[2] + afloat1[9] * afloat[6] + afloat1[10] * afloat[10] + afloat1[11] * afloat[14];
    clippingMatrix[11] = afloat1[8] * afloat[3] + afloat1[9] * afloat[7] + afloat1[10] * afloat[11] + afloat1[11] * afloat[15];
    clippingMatrix[12] = afloat1[12] * afloat[0] + afloat1[13] * afloat[4] + afloat1[14] * afloat[8] + afloat1[15] * afloat[12];
    clippingMatrix[13] = afloat1[12] * afloat[1] + afloat1[13] * afloat[5] + afloat1[14] * afloat[9] + afloat1[15] * afloat[13];
    clippingMatrix[14] = afloat1[12] * afloat[2] + afloat1[13] * afloat[6] + afloat1[14] * afloat[10] + afloat1[15] * afloat[14];
    clippingMatrix[15] = afloat1[12] * afloat[3] + afloat1[13] * afloat[7] + afloat1[14] * afloat[11] + afloat1[15] * afloat[15];
    float *afloat2 = frustum[0];
    afloat2[0] = clippingMatrix[3] - clippingMatrix[0];
    afloat2[1] = clippingMatrix[7] - clippingMatrix[4];
    afloat2[2] = clippingMatrix[11] - clippingMatrix[8];
    afloat2[3] = clippingMatrix[15] - clippingMatrix[12];
    normalize(afloat2);
    float *afloat3 = frustum[1];
    afloat3[0] = clippingMatrix[3] + clippingMatrix[0];
    afloat3[1] = clippingMatrix[7] + clippingMatrix[4];
    afloat3[2] = clippingMatrix[11] + clippingMatrix[8];
    afloat3[3] = clippingMatrix[15] + clippingMatrix[12];
    normalize(afloat3);
    float *afloat4 = frustum[2];
    afloat4[0] = clippingMatrix[3] + clippingMatrix[1];
    afloat4[1] = clippingMatrix[7] + clippingMatrix[5];
    afloat4[2] = clippingMatrix[11] + clippingMatrix[9];
    afloat4[3] = clippingMatrix[15] + clippingMatrix[13];
    normalize(afloat4);
    float *afloat5 = frustum[3];
    afloat5[0] = clippingMatrix[3] - clippingMatrix[1];
    afloat5[1] = clippingMatrix[7] - clippingMatrix[5];
    afloat5[2] = clippingMatrix[11] - clippingMatrix[9];
    afloat5[3] = clippingMatrix[15] - clippingMatrix[13];
    normalize(afloat5);
    float *afloat6 = frustum[4];
    afloat6[0] = clippingMatrix[3] - clippingMatrix[2];
    afloat6[1] = clippingMatrix[7] - clippingMatrix[6];
    afloat6[2] = clippingMatrix[11] - clippingMatrix[10];
    afloat6[3] = clippingMatrix[15] - clippingMatrix[14];
    normalize(afloat6);
    float *afloat7 = frustum[5];
    afloat7[0] = clippingMatrix[3] + clippingMatrix[2];
    afloat7[1] = clippingMatrix[7] + clippingMatrix[6];
    afloat7[2] = clippingMatrix[11] + clippingMatrix[10];
    afloat7[3] = clippingMatrix[15] + clippingMatrix[14];
    normalize(afloat7);
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 4; ++j)
            out[i * 4 + j] = frustum[i][j];
}

int main(void) {
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
        float proj[16], mv[16], out[24];
        for (int i = 0; i < 16; ++i) proj[i] = bits_to_f((uint32_t)t[i]);
        for (int i = 0; i < 16; ++i) mv[i] = bits_to_f((uint32_t)t[16 + i]);
        extract(proj, mv, out);
        for (int i = 0; i < 24; ++i) {
            if (i > 0) putchar(' ');
            printf("%x", f_to_bits(out[i]));
        }
        putchar('\n');
    }
    return 0;
}
