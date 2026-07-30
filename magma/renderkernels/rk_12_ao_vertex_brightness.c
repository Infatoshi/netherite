/* rk_12_ao_vertex_brightness.c - compute core of render-opt kernel 12_ao_vertex_brightness.
 * updateVertexBrightness + getAoBrightness/getVertexBrightness + enum weight tables copied
 * VERBATIM from candidate.c; only main()/stdio removed. Build with -ffp-contract=off. */
#include "rk.h"

/* EnumNeighborInfo vertN weight tables (verbatim). Indexed [dir][vert][8]. */
static const int VWEIGHT[6][4][8] = {
    /* DOWN */ {{10,3,10,9,4,9,4,3},{10,2,10,8,4,8,4,2},{11,2,11,8,5,8,5,2},{11,3,11,9,5,9,5,3}},
    /* UP   */ {{5,3,5,9,11,9,11,3},{5,2,5,8,11,8,11,2},{4,2,4,8,10,8,10,2},{4,3,4,9,10,9,10,3}},
    /* NORTH*/ {{1,10,1,4,7,4,7,10},{1,11,1,5,7,5,7,11},{0,11,0,5,6,5,6,11},{0,10,0,4,6,4,6,10}},
    /* SOUTH*/ {{1,10,7,10,7,4,1,4},{0,10,6,10,6,4,0,4},{0,11,6,11,6,5,0,5},{1,11,7,11,7,5,1,5}},
    /* WEST */ {{1,3,1,9,7,9,7,3},{1,2,1,8,7,8,7,2},{0,2,0,8,6,8,6,2},{0,3,0,9,6,9,6,3}},
    /* EAST */ {{6,3,6,9,0,9,0,3},{6,2,6,8,0,8,0,2},{7,2,7,8,1,8,1,2},{7,3,7,9,1,9,1,3}},
};
/* VertexTranslations: output vert slot for vert0..3 per direction. */
static const int VTRANS[6][4] = {
    /* DOWN */ {0,1,2,3}, /* UP */ {2,3,0,1}, /* NORTH */ {3,0,1,2},
    /* SOUTH*/ {0,1,2,3}, /* WEST */ {3,0,1,2}, /* EAST */ {1,2,3,0},
};

static int getAoBrightness(int br1, int br2, int br3, int br4) {
    if (br1 == 0) br1 = br4;
    if (br2 == 0) br2 = br4;
    if (br3 == 0) br3 = br4;
    return (br1 + br2 + br3 + br4) >> 2 & 16711935;
}
static int getVertexBrightness(int p1, int p2, int p3, int p4, float w1, float w2, float w3, float w4) {
    int i = (int)((float)(p1 >> 16 & 255) * w1 + (float)(p2 >> 16 & 255) * w2
                + (float)(p3 >> 16 & 255) * w3 + (float)(p4 >> 16 & 255) * w4) & 255;
    int j = (int)((float)(p1 & 255) * w1 + (float)(p2 & 255) * w2
                + (float)(p3 & 255) * w3 + (float)(p4 & 255) * w4) & 255;
    return i << 16 | j;
}

void rk_ao_vertex_brightness(int face, int s1,
                             const float faceShape[12],
                             int li, int lj, int lk, int ll,
                             int i1, int j1, int k1, int l1, int i3,
                             float f, float f1, float f2, float f3,
                             float f4, float f5, float f6, float f7, float f8,
                             int out_vb[4], float out_vcm[4]) {
    const int *vt = VTRANS[face];
    int vb[4]; float vcm[4];

    if (s1) { /* doNonCubicWeight is always true */
        float f29 = (f3 + f + f5 + f8) * 0.25F;
        float f30 = (f2 + f + f4 + f8) * 0.25F;
        float f31 = (f2 + f1 + f6 + f8) * 0.25F;
        float f32 = (f3 + f1 + f7 + f8) * 0.25F;
        const int (*vw)[8] = VWEIGHT[face];
        float w[4][4];
        for (int v = 0; v < 4; v++) {
            w[v][0] = faceShape[vw[v][0]] * faceShape[vw[v][1]];
            w[v][1] = faceShape[vw[v][2]] * faceShape[vw[v][3]];
            w[v][2] = faceShape[vw[v][4]] * faceShape[vw[v][5]];
            w[v][3] = faceShape[vw[v][6]] * faceShape[vw[v][7]];
        }
        vcm[vt[0]] = f29 * w[0][0] + f30 * w[0][1] + f31 * w[0][2] + f32 * w[0][3];
        vcm[vt[1]] = f29 * w[1][0] + f30 * w[1][1] + f31 * w[1][2] + f32 * w[1][3];
        vcm[vt[2]] = f29 * w[2][0] + f30 * w[2][1] + f31 * w[2][2] + f32 * w[2][3];
        vcm[vt[3]] = f29 * w[3][0] + f30 * w[3][1] + f31 * w[3][2] + f32 * w[3][3];
        int i2 = getAoBrightness(ll, li, j1, i3);
        int j2 = getAoBrightness(lk, li, i1, i3);
        int k2 = getAoBrightness(lk, lj, k1, i3);
        int l2 = getAoBrightness(ll, lj, l1, i3);
        vb[vt[0]] = getVertexBrightness(i2, j2, k2, l2, w[0][0], w[0][1], w[0][2], w[0][3]);
        vb[vt[1]] = getVertexBrightness(i2, j2, k2, l2, w[1][0], w[1][1], w[1][2], w[1][3]);
        vb[vt[2]] = getVertexBrightness(i2, j2, k2, l2, w[2][0], w[2][1], w[2][2], w[2][3]);
        vb[vt[3]] = getVertexBrightness(i2, j2, k2, l2, w[3][0], w[3][1], w[3][2], w[3][3]);
    } else {
        float f9  = (f3 + f + f5 + f8) * 0.25F;
        float f10 = (f2 + f + f4 + f8) * 0.25F;
        float f11 = (f2 + f1 + f6 + f8) * 0.25F;
        float f12 = (f3 + f1 + f7 + f8) * 0.25F;
        vb[vt[0]] = getAoBrightness(ll, li, j1, i3);
        vb[vt[1]] = getAoBrightness(lk, li, i1, i3);
        vb[vt[2]] = getAoBrightness(lk, lj, k1, i3);
        vb[vt[3]] = getAoBrightness(ll, lj, l1, i3);
        vcm[vt[0]] = f9; vcm[vt[1]] = f10; vcm[vt[2]] = f11; vcm[vt[3]] = f12;
    }
    out_vb[0] = vb[0]; out_vb[1] = vb[1]; out_vb[2] = vb[2]; out_vb[3] = vb[3];
    out_vcm[0] = vcm[0]; out_vcm[1] = vcm[1]; out_vcm[2] = vcm[2]; out_vcm[3] = vcm[3];
}
