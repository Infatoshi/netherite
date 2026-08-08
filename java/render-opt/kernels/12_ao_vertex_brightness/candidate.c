/* CANDIDATE: pure-C port of MC 1.11.2 AmbientOcclusionFace.updateVertexBrightness()
 *   (src/net/minecraft/client/renderer/BlockModelRenderer.java:368) + getAoBrightness/getVertexBrightness.
 *
 * Golden CAPTURED FROM REAL MINECRAFT (capture_mode "live-hook", NetheriteMod 'capture_ao'): the hook stages
 * representative blocks (stairs for the non-cubic-weight branch, plus grass/leaves/solids), and for
 * each BakedQuad it (1) replicates the method's world reads to snapshot the ~20 light/AO scalars it
 * consumes, and (2) invokes the REAL updateVertexBrightness reflectively to get the 4 vertexBrightness
 * ints + 4 vertexColorMultiplier floats (the golden). This C reproduces only the ARITHMETIC + enum
 * weight tables from those captured scalars. If the snapshot were wrong, the port (correct arithmetic
 * on wrong inputs) would NOT match the real method's output -> the runner fails loud; so a PASS is real.
 *
 * Input record (one line per quad), from golden/inputs.txt:
 *   face s0 s1  faceShape[0..11](float bits)  li lj lk ll i1 j1 k1 l1 i3  f f1 f2 f3 f4 f5 f6 f7 f8(float bits)
 *   face : EnumFacing index 0..5 (DOWN UP NORTH SOUTH WEST EAST)
 *   s0,s1: shapeState.get(0/1)
 *   li,lj,lk,ll : packed lightmap coords at the 4 corners (i,j,k,l in the method)
 *   i1,j1,k1,l1 : the conditionally-selected corner-diagonal packed coords
 *   i3   : center (or center+dir) packed coords
 *   f..f8: AmbientOcclusionLightValue scalars (f,f1,f2,f3 corners; f4..f7 diagonals; f8 center)
 *
 * doNonCubicWeight is true for all 6 faces, so the non-cubic branch is taken iff s1.
 * Output (8 lines per quad): vertexBrightness[0..3] (ints), vertexColorMultiplier[0..3] (float bits).
 * Must BITWISE-match golden/golden.txt. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static float f_of(int32_t b) { float f; memcpy(&f, &b, 4); return f; }
static int32_t b_of(float f) { int32_t b; memcpy(&b, &f, 4); return b; }

/* EnumNeighborInfo vertN weight tables: shape index = facing.getIndex() + (flip ? 6 : 0).
 * facing: DOWN0 UP1 NORTH2 SOUTH3 WEST4 EAST5; flipped adds 6. Indexed [dir][vert][8]. */
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

int main(void) {
    int face, s0, s1;
    int32_t fsb[12];
    int li, lj, lk, ll, i1, j1, k1, l1, i3;
    int32_t fb[9];
    for (;;) {
        if (scanf("%d %d %d", &face, &s0, &s1) != 3) break;
        for (int z = 0; z < 12; z++) if (scanf("%d", &fsb[z]) != 1) return 1;
        if (scanf("%d %d %d %d %d %d %d %d %d",
                  &li, &lj, &lk, &ll, &i1, &j1, &k1, &l1, &i3) != 9) return 1;
        for (int z = 0; z < 9; z++) if (scanf("%d", &fb[z]) != 1) return 1;

        float faceShape[12];
        for (int z = 0; z < 12; z++) faceShape[z] = f_of(fsb[z]);
        float f = f_of(fb[0]), f1 = f_of(fb[1]), f2 = f_of(fb[2]), f3 = f_of(fb[3]);
        float f4 = f_of(fb[4]), f5 = f_of(fb[5]), f6 = f_of(fb[6]), f7 = f_of(fb[7]), f8 = f_of(fb[8]);

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
        printf("%d\n%d\n%d\n%d\n", vb[0], vb[1], vb[2], vb[3]);
        printf("%d\n%d\n%d\n%d\n", b_of(vcm[0]), b_of(vcm[1]), b_of(vcm[2]), b_of(vcm[3]));
    }
    return 0;
}
