/* CANDIDATE: C port of MC's animated-texture frame interpolation.
 * Source: TextureAtlasSprite.interpolateColor() + the per-pixel assembly in
 * updateAnimationInterpolated(). Must BITWISE-match golden/Golden.java.
 * Reads records of (double d0, int j1, int k1); prints the final ARGB int (signed decimal). */
#include <stdio.h>
#include <stdint.h>

static int interpolate_color(double p1, int p3, int p4) {
    return (int)(p1 * (double)p3 + (1.0 - p1) * (double)p4);
}

static int interp_pixel(double d0, int j1, int k1) {
    int l1 = interpolate_color(d0, j1 >> 16 & 255, k1 >> 16 & 255);
    int i2 = interpolate_color(d0, j1 >> 8 & 255, k1 >> 8 & 255);
    int j2 = interpolate_color(d0, j1 & 255, k1 & 255);
    uint32_t out = ((uint32_t)j1 & 0xFF000000u) | ((uint32_t)l1 << 16)
                 | ((uint32_t)i2 << 8) | (uint32_t)j2;
    return (int)out;
}

int main(void) {
    double d0; int j1, k1;
    while (scanf("%lf %d %d", &d0, &j1, &k1) == 3) {
        printf("%d\n", interp_pixel(d0, j1, k1));
    }
    return 0;
}
