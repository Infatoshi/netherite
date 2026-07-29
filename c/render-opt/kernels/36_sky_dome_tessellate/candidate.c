/* CANDIDATE: C port of MC 1.11.2 RenderGlobal.renderSky() grid tessellation.
 * Must BITWISE-match golden/Golden.java. All coords are exact integers cast through float/double,
 * so the only subtlety is reproducing Java's cast points: x = (double)(float)k, z = (double)(int)l.
 * Build with -ffp-contract=off (runner does); no FP arithmetic here anyway, just casts.
 * Input  (per line): posY (hex float-bits) + reverseX (0/1)
 * Output: one vertex per line = 3 hex doubles (raw long-bits of x,y,z). 676 verts per record. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static double f_from_hex(unsigned long b) { float f; uint32_t u = (uint32_t)b; memcpy(&f, &u, sizeof f); return (double)f; }
static unsigned long long dbits(double d) { uint64_t b; memcpy(&b, &d, sizeof b); return (unsigned long long)b; }

static void emit(double x, double y, double z) {
    printf("%llx %llx %llx\n", dbits(x), dbits(y), dbits(z));
}

int main(void) {
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        unsigned long yb;
        int reverseX;
        if (sscanf(line, "%lx %d", &yb, &reverseX) != 2) continue;
        double posY = f_from_hex(yb);  /* posY is a float in Java, passed as (double)posY */

        for (int k = -384; k <= 384; k += 64) {
            for (int l = -384; l <= 384; l += 64) {
                float f = (float) k;
                float f1 = (float) (k + 64);

                if (reverseX) {
                    f1 = (float) k;
                    f = (float) (k + 64);
                }

                emit((double) f, posY, (double) l);
                emit((double) f1, posY, (double) l);
                emit((double) f1, posY, (double) (l + 64));
                emit((double) f, posY, (double) (l + 64));
            }
        }
    }
    return 0;
}
