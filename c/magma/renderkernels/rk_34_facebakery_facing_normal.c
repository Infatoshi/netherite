/* rk_34_facebakery_facing_normal.c - compute core of render-opt kernel 34_facebakery_facing_normal.
 * getFacingFromVertexData + applyFacing (+ epsilonEquals, DIR_VEC/FACE_DIR tables) copied VERBATIM
 * from candidate.c; only main()/stdio removed. Build with -ffp-contract=off. */
#include "rk.h"
#include <math.h>
#include <string.h>

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

int rk_facebakery_get_facing(const int32_t faceData[28]) {
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

void rk_facebakery_apply_facing(int32_t p1[28], int targetFacing) {
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
