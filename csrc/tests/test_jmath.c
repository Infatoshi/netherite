/* engine/jfloor.h and engine/jmath.h against MathHelper on the oracle
 * (oracle/harness/netherite/oracle/JmathProbe.java): the floors and the placement
 * facings at huge, NaN and infinite inputs, where a plain C cast is undefined
 * and Java's saturates.
 *   test_jmath DIR   DIR/values.txt (make -C oracle jmath-probe writes it) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../engine/jmath.h"

static float f_of(uint32_t b)
{
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

static uint32_t bits_of(float f)
{
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
}

/* the vanilla call shapes, as place.c's yaw_quad and activate.c spell them */
static long answer(const char *kind, uint64_t in)
{
    if (!strcmp(kind, "fd"))
    {
        double d;
        memcpy(&d, &in, sizeof d);
        return mh_floor(d);
    }
    float y = f_of((uint32_t)in);
    if (!strcmp(kind, "ff")) return mh_floor_float(y);
    if (!strcmp(kind, "q4")) return mh_floor((double)(y * 4.0F / 360.0F) + 0.5) & 3;
    if (!strcmp(kind, "q4s")) return mh_floor((double)(y * 4.0F / 360.0F) + 2.5) & 3;
    if (!strcmp(kind, "q4d")) return mh_floor((double)((y + 180.0F) * 4.0F / 360.0F) - 0.5) & 3;
    if (!strcmp(kind, "q16")) return mh_floor((double)(y * 16.0F / 360.0F) + 0.5) & 15;
    if (!strcmp(kind, "q16s")) return mh_floor((double)((y + 180.0F) * 16.0F / 360.0F) + 0.5) & 15;
    if (!strcmp(kind, "sin")) return bits_of(mh_sin(y * (float)3.141592653589793 / 180.0F));
    if (!strcmp(kind, "cos")) return bits_of(mh_cos(y * (float)3.141592653589793 / 180.0F));
    fprintf(stderr, "test_jmath: unknown kind %s\n", kind);
    exit(2);
}

static int check(const char *dir, int *cases)
{
    char path[4096], kind[16], inhex[32], want[32];
    snprintf(path, sizeof path, "%s/values.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "test_jmath: cannot open %s\n", path); return 1; }
    int bad = 0;
    while (fscanf(f, "%15s %31s %31s", kind, inhex, want) == 3)
    {
        uint64_t in = strtoull(inhex, NULL, 16);
        int hex = !strcmp(kind, "sin") || !strcmp(kind, "cos");
        long w = hex ? (long)strtoul(want, NULL, 16) : strtol(want, NULL, 10);
        long got = answer(kind, in);
        ++*cases;
        if (got != w)
        {
            if (bad < 20) printf("FAIL %s %s %s: native %ld, MathHelper %ld\n", dir, kind, inhex, got, w);
            ++bad;
        }
    }
    fclose(f);
    return bad;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: test_jmath DIR\n"); return 2; }
    jmath_init();
    int cases = 0, bad = check(argv[argc - 1], &cases);
    printf("jmath: %d cases, %d differ from MathHelper\n", cases, bad);
    return bad != 0;
}
