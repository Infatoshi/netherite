/* CANDIDATE: C port of MC 1.11.2 FaceBakery.getFacingFromVertexData() + applyFacing().
 * Must BITWISE-match golden/Golden.java. Faithfulness traps:
 *  - LWJGL Vector3f.cross uses operand order (left.y*right.z - left.z*right.y, right.x*left.z -
 *    left.x*right.z, left.x*right.y - left.y*right.x); preserve it.
 *  - normal length = (float)sqrt((double)(sumsq)); divide each comp by f (float /).
 *  - facing pick: f2 >= 0.0F && f2 > f1 over values() order D-U-N-S-W-E; null -> UP.
 *  - applyFacing: out = copy of input; ALWAYS overwrite pos lanes [0,1,2]; overwrite uv lanes [4,5]
 *    ONLY on epsilonEquals match; lanes [3],[6] keep original. epsilonEquals = fabsf(b-a) < 1e-5F.
 * Build with -ffp-contract=off (runner does).
 * Input  (per line): 28 hex ints + targetFacing(0-5) = 29 tokens
 * Output (per line): chosenFacingIndex + 28 hex ints (reordered vertexData) */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

enum { DOWN = 0, UP = 1, NORTH = 2, SOUTH = 3, WEST = 4, EAST = 5 };

static const int DIR_VEC[6][3] = {
    {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0},
};

static const int FACE_DIR[6][4][3] = {
    {{WEST, DOWN, SOUTH}, {WEST, DOWN, NORTH}, {EAST, DOWN, NORTH}, {EAST, DOWN, SOUTH}},
    {{WEST, UP, NORTH}, {WEST, UP, SOUTH}, {EAST, UP, SOUTH}, {EAST, UP, NORTH}},
    {{EAST, UP, NORTH}, {EAST, DOWN, NORTH}, {WEST, DOWN, NORTH}, {WEST, UP, NORTH}},
    {{WEST, UP, SOUTH}, {WEST, DOWN, SOUTH}, {EAST, DOWN, SOUTH}, {EAST, UP, SOUTH}},
    {{WEST, UP, NORTH}, {WEST, DOWN, NORTH}, {WEST, DOWN, SOUTH}, {WEST, UP, SOUTH}},
    {{EAST, UP, SOUTH}, {EAST, DOWN, SOUTH}, {EAST, DOWN, NORTH}, {EAST, UP, NORTH}},
};

static float bits_to_f(uint32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }
static uint32_t f_to_bits(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

static int getFacingFromVertexData(const int32_t *faceData) {
    float v0x = bits_to_f((uint32_t)faceData[0]), v0y = bits_to_f((uint32_t)faceData[1]), v0z = bits_to_f((uint32_t)faceData[2]);
    float v1x = bits_to_f((uint32_t)faceData[7]), v1y = bits_to_f((uint32_t)faceData[8]), v1z = bits_to_f((uint32_t)faceData[9]);
    float v2x = bits_to_f((uint32_t)faceData[14]), v2y = bits_to_f((uint32_t)faceData[15]), v2z = bits_to_f((uint32_t)faceData[16]);
    /* sub: v3 = v0 - v1; v4 = v2 - v1 */
    float v3x = v0x - v1x, v3y = v0y - v1y, v3z = v0z - v1z;
    float v4x = v2x - v1x, v4y = v2y - v1y, v4z = v2z - v1z;
    /* cross(left=v4, right=v3): v5 */
    float v5x = v4y * v3z - v4z * v3y;
    float v5y = v3x * v4z - v4x * v3z;
    float v5z = v4x * v3y - v4y * v3x;
    float f = (float) sqrt((double) (v5x * v5x + v5y * v5y + v5z * v5z));
    v5x /= f;
    v5y /= f;
    v5z /= f;
    int enumfacing = -1;
    float f1 = 0.0F;
    for (int ef = 0; ef < 6; ++ef) {
        float bx = (float) DIR_VEC[ef][0], by = (float) DIR_VEC[ef][1], bz = (float) DIR_VEC[ef][2];
        float f2 = v5x * bx + v5y * by + v5z * bz;
        if (f2 >= 0.0F && f2 > f1) {
            f1 = f2;
            enumfacing = ef;
        }
    }
    if (enumfacing == -1) return UP;
    return enumfacing;
}

static int epsilonEquals(float a, float b) { return fabsf(b - a) < 1.0E-5F; }

static void applyFacing(int32_t *p1, int targetFacing) {
    int32_t aint[28];
    memcpy(aint, p1, sizeof aint);
    float afloat[6];
    afloat[WEST] = 999.0F;
    afloat[DOWN] = 999.0F;
    afloat[NORTH] = 999.0F;
    afloat[EAST] = -999.0F;
    afloat[UP] = -999.0F;
    afloat[SOUTH] = -999.0F;

    for (int i = 0; i < 4; ++i) {
        int j = 7 * i;
        float f = bits_to_f((uint32_t)aint[j]);
        float f1 = bits_to_f((uint32_t)aint[j + 1]);
        float f2 = bits_to_f((uint32_t)aint[j + 2]);
        if (f < afloat[WEST]) afloat[WEST] = f;
        if (f1 < afloat[DOWN]) afloat[DOWN] = f1;
        if (f2 < afloat[NORTH]) afloat[NORTH] = f2;
        if (f > afloat[EAST]) afloat[EAST] = f;
        if (f1 > afloat[UP]) afloat[UP] = f1;
        if (f2 > afloat[SOUTH]) afloat[SOUTH] = f2;
    }

    const int (*efd)[3] = FACE_DIR[targetFacing];

    for (int i1 = 0; i1 < 4; ++i1) {
        int j1 = 7 * i1;
        const int *vi = efd[i1];
        float f8 = afloat[vi[0]];
        float f3 = afloat[vi[1]];
        float f4 = afloat[vi[2]];
        p1[j1] = (int32_t) f_to_bits(f8);
        p1[j1 + 1] = (int32_t) f_to_bits(f3);
        p1[j1 + 2] = (int32_t) f_to_bits(f4);
        for (int k = 0; k < 4; ++k) {
            int l = 7 * k;
            float f5 = bits_to_f((uint32_t)aint[l]);
            float f6 = bits_to_f((uint32_t)aint[l + 1]);
            float f7 = bits_to_f((uint32_t)aint[l + 2]);
            if (epsilonEquals(f8, f5) && epsilonEquals(f3, f6) && epsilonEquals(f4, f7)) {
                p1[j1 + 4] = aint[l + 4];
                p1[j1 + 4 + 1] = aint[l + 4 + 1];
            }
        }
    }
}

int main(void) {
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        unsigned long t[28];
        int target;
        int n = sscanf(line,
            "%lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx "
            "%lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %d",
            &t[0],&t[1],&t[2],&t[3],&t[4],&t[5],&t[6],&t[7],&t[8],&t[9],
            &t[10],&t[11],&t[12],&t[13],&t[14],&t[15],&t[16],&t[17],&t[18],&t[19],
            &t[20],&t[21],&t[22],&t[23],&t[24],&t[25],&t[26],&t[27], &target);
        if (n != 29) continue;
        int32_t vd[28];
        for (int i = 0; i < 28; ++i) vd[i] = (int32_t)(uint32_t)t[i];

        int chosen = getFacingFromVertexData(vd);
        applyFacing(vd, target);

        printf("%d", chosen);
        for (int i = 0; i < 28; ++i) printf(" %x", (uint32_t)vd[i]);
        printf("\n");
    }
    return 0;
}
