/* The renderer's precision (lane/renderprec, Elliot 2026-09-29): exact, the
 * arithmetic the frames are judged against Java with (the default), or fast,
 * reduced precision per stage. A frame's mode is a mask of the stages drawn
 * fast; the C renderer (raster.c, raster_entities.c, raster_tileent.c) and
 * the device renderer (csrc/cuda/render) take the same mask and do the
 * same operations in the same order, so a device frame is byte for byte the
 * C frame in either mode, and an exact frame against a fast one differs by
 * floating point alone, stage by stage.
 *
 *   RP_XFORM  the terrain vertex's camera offset in float (exact: double)
 *   RP_EDGE   the entity and end portal edge tests in float (exact: double)
 *   RP_LIGHT  the light coordinates' interpolation and the lightmap's
 *             filter in float16
 *   RP_COLOR  the vertex colour's interpolation in float16 (the entity's
 *             glColor too)
 *   RP_FOG    the fog blend (value * fog + fog colour * (1 - fog)) in float16
 *   RP_SHADE  the shading products (texel * colour * light; the entity's
 *             material * diffuse, its diffuse's interpolation, the alpha
 *             test's product) in float16
 *   RP_RCP    the terrain's and the entities' perspective weights in float
 *             by reciprocals: 1 / area and 1 / w once a triangle, one
 *             division a pixel (1 / iw) where exact divides about ten times
 *
 * Screen positions, depth and texture coordinates are float32 in both modes
 * (they already are in exact). The sky, the clouds, the block crack and the
 * outline draw the same in both. float16 is IEEE binary16, round to nearest
 * even, no flush: the C side computes each operation in float and rounds
 * the result to half (rp_h), which is the correctly rounded half operation
 * for + - * / since float's 24 bits are at least twice half's 11 plus 2; the
 * device uses its half operations (never contracted) and rounds a float
 * division the same way. */
#ifndef NETHERITE_RASTER_PREC_H
#define NETHERITE_RASTER_PREC_H

#include <stdint.h>
#include <string.h>

enum {
    RP_XFORM = 1,
    RP_EDGE = 2,
    RP_LIGHT = 4,
    RP_COLOR = 8,
    RP_FOG = 16,
    RP_SHADE = 32,
    RP_RCP = 64,
    RP_EXACT = 0,
    RP_FAST = 127,
    RP_HALF = RP_LIGHT | RP_COLOR | RP_FOG | RP_SHADE,   /* the stages in float16 */
};

/* X rounded to the nearest float16 (ties to even; past 65504 infinity,
 * below 2^-14 the subnormal steps of 2^-24), as a float */
static inline float rp_h(float x)
{
    uint32_t u;
    memcpy(&u, &x, 4);
    uint32_t sign = u & 0x80000000u, a = u & 0x7fffffffu;
    if (a >= 0x7f800000u) return x;                          /* inf, NaN */
    if (a >= 0x477ff000u) { u = sign | 0x7f800000u; }        /* >= 65520: rounds past 65504 */
    else if (a < 0x38800000u) {                              /* < 2^-14: steps of 2^-24 */
        float f;
        memcpy(&f, &a, 4);
        f = f * 16777216.0f;                                 /* exact: a power of two */
        /* round to nearest even by the float magic number (f < 2^10) */
        f = (f + 8388608.0f) - 8388608.0f;
        f = f * (1.0f / 16777216.0f);
        memcpy(&a, &f, 4);
        u = sign | a;
    } else {
        a += 0x0fffu + ((a >> 13) & 1u);
        a &= ~0x1fffu;
        u = sign | a;
    }
    float r;
    memcpy(&r, &u, 4);
    return r;
}

/* "exact", "fast", or "fast:" with stage names joined by commas (xform,
 * edge, light, color, fog, shade, rcp): the mask; -1 for anything else */
static inline int rp_parse(const char *s)
{
    if (!strcmp(s, "exact")) return RP_EXACT;
    if (!strcmp(s, "fast")) return RP_FAST;
    if (strncmp(s, "fast:", 5)) return -1;
    static const char *names[7] = {"xform", "edge", "light", "color", "fog", "shade", "rcp"};
    int m = 0;
    for (const char *p = s + 5; *p;) {
        size_t n = strcspn(p, ",");
        int k = 0;
        while (k < 7 && !(strlen(names[k]) == n && !strncmp(p, names[k], n))) ++k;
        if (k == 7) return -1;
        m |= 1 << k;
        p += n + (p[n] == ',');
    }
    return m;
}

#endif
