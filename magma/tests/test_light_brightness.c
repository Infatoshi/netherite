/* Bit-exact C vs Java verification of world/lightmap.h for all dimensions. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "world/lightmap.h"

static int f2bits(float f) { int b; memcpy(&b, &f, sizeof b); return b; }

int main(void) {
    const char *path = "tests/golden_light.txt";
    FILE *fg = fopen(path, "r");
    if (!fg) { fg = fopen("golden_light.txt", "r"); path = "golden_light.txt"; }
    if (!fg) { printf("FAIL: cannot open golden_light.txt\n"); return 2; }

    int fails = 0, ntable = 0, nrgb = 0;
    char kind[16];
    while (fscanf(fg, "%15s", kind) == 1) {
        if (strcmp(kind, "TABLE") == 0) {
            int dim, level, bits;
            if (fscanf(fg, "%d %d %d", &dim, &level, &bits) != 3) {
                printf("FAIL: parse TABLE\n"); fails++; break;
            }
            int got = f2bits(cr_light_brightness(dim, level));
            if (got != bits) {
                printf("FAIL: TABLE dim=%d level=%d java=%d c=%d\n",
                       dim, level, bits, got);
                fails++;
            }
            ntable++;
        } else if (strcmp(kind, "RGB") == 0) {
            int dim, sky, block, rb, gb, bb, argb;
            if (fscanf(fg, "%d %d %d %d %d %d %d",
                       &dim, &sky, &block, &rb, &gb, &bb, &argb) != 7) {
                printf("FAIL: parse RGB\n"); fails++; break;
            }
            CrLightmapRgb c = cr_lightmap_rgb(dim, sky, block,
                cr_dimension_sun_brightness(dim), 0.0f, 0.0f);
            CrRgba q = cr_lightmap_rgba8(c);
            int32_t got_argb = (int32_t)(UINT32_C(0xff000000)
                | (uint32_t)q.r << 16 | (uint32_t)q.g << 8 | q.b);
            if (f2bits(c.r) != rb || f2bits(c.g) != gb || f2bits(c.b) != bb
                    || got_argb != (int32_t)argb) {
                printf("FAIL: RGB dim=%d sky=%d block=%d bits/argb diverged\n",
                       dim, sky, block);
                fails++;
            }
            nrgb++;
        } else {
            printf("FAIL: unknown token %s\n", kind); fails++; break;
        }
    }
    fclose(fg);

    if (ntable != 48 || nrgb != 768) {
        printf("FAIL: expected 48 TABLE + 768 RGB, got %d + %d\n", ntable, nrgb);
        fails++;
    }
    /* High-value sentinels: dark Nether ambient and dark End ambient. */
    CrRgba nether0 = cr_lightmap_rgba8(cr_lightmap_rgb(-1, 0, 0, 0.2f, 0.0f, 0.0f));
    CrRgba end0 = cr_lightmap_rgba8(cr_lightmap_rgb(1, 0, 0, 1.0f, 0.0f, 0.0f));
    CrRgba night0 = cr_lightmap_rgba8(cr_lightmap_rgb(0, 0, 0, 0.2f, 0.0f, 0.0f));
    CrRgba night15 = cr_lightmap_rgba8(cr_lightmap_rgb(0, 15, 0, 0.2f, 0.0f, 0.0f));
    if (nether0.r != 52 || nether0.g != 42 || nether0.b != 35) {
        printf("FAIL: Nether dark texel got %u,%u,%u\n", nether0.r, nether0.g, nether0.b);
        fails++;
    }
    if (end0.r != 61 || end0.g != 76 || end0.b != 68) {
        printf("FAIL: End dark texel got %u,%u,%u\n", end0.r, end0.g, end0.b);
        fails++;
    }
    if (night0.r != 14 || night0.g != 14 || night0.b != 14) {
        printf("FAIL: overworld midnight sky-0 texel got %u,%u,%u\n",
               night0.r, night0.g, night0.b);
        fails++;
    }
    if (night15.r != 42 || night15.g != 42 || night15.b != 71) {
        printf("FAIL: overworld midnight sky-15 texel got %u,%u,%u\n",
               night15.r, night15.g, night15.b);
        fails++;
    }
    printf("checked %d table + %d RGB values against %s\n", ntable, nrgb, path);
    printf(fails ? "RESULT: FAIL (%d)\n" : "RESULT: PASS (0 LSB divergence)\n", fails);
    return fails ? 1 : 0;
}
