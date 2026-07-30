/* CPU reference: sweeps MC MathHelper sin/cos/floor and prints raw bits. sin/cos as %08x of the
 * float bit pattern (floatToRawIntBits), floor as a decimal int. Must match the verbatim-Java
 * golden bitwise and the CUDA path bitwise. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/mathhelper.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    static McSinTable st;
    mc_sin_table_init(&st);
    for (int i = 0; i < MH_NT; ++i) {
        float v = (float)((i - 6283) * 0.01);
        float s = mc_sin(&st, v), c = mc_cos(&st, v);
        u32 sb, cb; memcpy(&sb, &s, 4); memcpy(&cb, &c, 4);
        printf("%08x\n", (unsigned)sb);
        printf("%08x\n", (unsigned)cb);
    }
    for (int i = 0; i < MH_NF; ++i) {
        double d = (i - 1000) * 0.123;
        printf("%d\n", mc_floor(d));
    }
    return 0;
}
