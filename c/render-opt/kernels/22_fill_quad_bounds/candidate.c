/* CANDIDATE: C port of MC 1.11.2 BlockModelRenderer.fillQuadBounds().
 * Must BITWISE-match golden/Golden.java. Op order preserved; build with -ffp-contract=off.
 * Java Math.min/max on these clean [0,1] floats == plain comparison (no NaN/Inf in pos lanes),
 * so ternary min/max is bit-exact; fminf/fmaxf would NOT match Java semantics - do not use them.
 * Input  (per line): 28 hex ints (vertexData) + face(0-5) + isFullCube(0/1)  = 30 tokens
 * Output (per line): 12 hex ints (raw float bits of quadBounds) + flag0 + flag1 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static float bits_to_f(uint32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }
static uint32_t f_to_bits(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

/* EnumFacing index (D-U-N-S-W-E = 0..5) */
enum { DOWN = 0, UP = 1, NORTH = 2, SOUTH = 3, WEST = 4, EAST = 5 };

static float fmin_j(float a, float b) { return a <= b ? a : b; }
static float fmax_j(float a, float b) { return a >= b ? a : b; }

/* returns flags packed: bit0 = boundsFlags[0], bit1 = boundsFlags[1] */
static int fill_quad_bounds(int fullCube, const int32_t *vertexData, int face, float *quadBounds) {
    float f = 32.0F;
    float f1 = 32.0F;
    float f2 = 32.0F;
    float f3 = -32.0F;
    float f4 = -32.0F;
    float f5 = -32.0F;

    for (int i = 0; i < 4; ++i) {
        float f6 = bits_to_f((uint32_t)vertexData[i * 7]);
        float f7 = bits_to_f((uint32_t)vertexData[i * 7 + 1]);
        float f8 = bits_to_f((uint32_t)vertexData[i * 7 + 2]);
        f = fmin_j(f, f6);
        f1 = fmin_j(f1, f7);
        f2 = fmin_j(f2, f8);
        f3 = fmax_j(f3, f6);
        f4 = fmax_j(f4, f7);
        f5 = fmax_j(f5, f8);
    }

    quadBounds[WEST] = f;
    quadBounds[EAST] = f3;
    quadBounds[DOWN] = f1;
    quadBounds[UP] = f4;
    quadBounds[NORTH] = f2;
    quadBounds[SOUTH] = f5;
    int j = 6;
    quadBounds[WEST + j] = 1.0F - f;
    quadBounds[EAST + j] = 1.0F - f3;
    quadBounds[DOWN + j] = 1.0F - f1;
    quadBounds[UP + j] = 1.0F - f4;
    quadBounds[NORTH + j] = 1.0F - f2;
    quadBounds[SOUTH + j] = 1.0F - f5;

    int b0 = 0, b1 = 0;
    switch (face) {
        case DOWN:
            b1 = (f >= 1.0E-4F || f2 >= 1.0E-4F || f3 <= 0.9999F || f5 <= 0.9999F);
            b0 = (f1 < 1.0E-4F || fullCube) && f1 == f4;
            break;
        case UP:
            b1 = (f >= 1.0E-4F || f2 >= 1.0E-4F || f3 <= 0.9999F || f5 <= 0.9999F);
            b0 = (f4 > 0.9999F || fullCube) && f1 == f4;
            break;
        case NORTH:
            b1 = (f >= 1.0E-4F || f1 >= 1.0E-4F || f3 <= 0.9999F || f4 <= 0.9999F);
            b0 = (f2 < 1.0E-4F || fullCube) && f2 == f5;
            break;
        case SOUTH:
            b1 = (f >= 1.0E-4F || f1 >= 1.0E-4F || f3 <= 0.9999F || f4 <= 0.9999F);
            b0 = (f5 > 0.9999F || fullCube) && f2 == f5;
            break;
        case WEST:
            b1 = (f1 >= 1.0E-4F || f2 >= 1.0E-4F || f4 <= 0.9999F || f5 <= 0.9999F);
            b0 = (f < 1.0E-4F || fullCube) && f == f3;
            break;
        case EAST:
            b1 = (f1 >= 1.0E-4F || f2 >= 1.0E-4F || f4 <= 0.9999F || f5 <= 0.9999F);
            b0 = (f3 > 0.9999F || fullCube) && f == f3;
            break;
    }
    return (b0 ? 1 : 0) | (b1 ? 2 : 0);
}

int main(void) {
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        unsigned long t[28];
        int face, fullCube;
        int n = sscanf(line,
            "%lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx "
            "%lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %d %d",
            &t[0],&t[1],&t[2],&t[3],&t[4],&t[5],&t[6],&t[7],&t[8],&t[9],
            &t[10],&t[11],&t[12],&t[13],&t[14],&t[15],&t[16],&t[17],&t[18],&t[19],
            &t[20],&t[21],&t[22],&t[23],&t[24],&t[25],&t[26],&t[27], &face, &fullCube);
        if (n != 30) continue;
        int32_t vd[28];
        for (int i = 0; i < 28; ++i) vd[i] = (int32_t)(uint32_t)t[i];
        float quadBounds[12];
        int flags = fill_quad_bounds(fullCube, vd, face, quadBounds);
        for (int i = 0; i < 12; ++i) {
            if (i > 0) putchar(' ');
            printf("%x", f_to_bits(quadBounds[i]));
        }
        printf(" %d %d\n", (flags & 1) ? 1 : 0, (flags & 2) ? 1 : 0);
    }
    return 0;
}
