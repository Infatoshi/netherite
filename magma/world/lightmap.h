/* world/lightmap.h - exact MC 1.11.2 lightmap transfer for all dimensions.
 *
 * This is header-only so the mesher and the standalone Java-golden test execute
 * the same float operation sequence. Compile with -ffp-contract=off.
 *
 * Sources (java/oracle-src):
 *   WorldProvider.java:56-64                 overworld/End brightness table
 *   WorldProviderHell.java:36-44             Nether brightness table
 *   EntityRenderer.java:892-1016             updateLightmap
 *   World.java:1572-1580                     sun brightness
 *
 * The End has no skylight in 1.11.2. Its ambient terrain colour is the explicit
 * dimId==1 updateLightmap override, not a fabricated block-light floor.
 */
#ifndef MAGMA_WORLD_LIGHTMAP_H
#define MAGMA_WORLD_LIGHTMAP_H

#include "core/types.h"

typedef struct { float r, g, b; } CrLightmapRgb;

static inline float cr_lm_clamp01(float v)
{
    if (v > 1.0f) return 1.0f;
    if (v < 0.0f) return 0.0f;
    return v;
}

/* WorldProvider.generateLightBrightnessTable. `dimension == -1` selects the
 * Hell override (ambient 0.1); overworld and End use the base table. */
static inline float cr_light_brightness(int dimension, int level)
{
    if (level < 0) level = 0;
    if (level > 15) level = 15;
    float f1 = 1.0f - (float)level / 15.0f;
    if (dimension == -1)
        return (1.0f - f1) / (f1 * 3.0f + 1.0f) * 0.9f + 0.1f;
    return (1.0f - f1) / (f1 * 3.0f + 1.0f) * 1.0f + 0.0f;
}

/* Frozen clear-weather portal captures use the providers' fixed celestial
 * angles: Nether 0.5 -> sunBrightness 0.2; End 0.0 -> 1.0. Overworld captures
 * are pinned to noon -> 1.0. */
static inline float cr_dimension_sun_brightness(int dimension)
{
    return dimension == -1 ? 0.2f : 1.0f;
}

static inline float cr_lm_gamma_finish(float v, float gamma)
{
    float inv = 1.0f - v;
    float bright = 1.0f - inv * inv * inv * inv;
    v = v * (1.0f - gamma) + bright * gamma;
    v = v * 0.96f + 0.03f;
    return cr_lm_clamp01(v);
}

/* EntityRenderer's Night Vision normalization, shared by updateLightmap and
 * updateFogColor. It scales all channels by the reciprocal of the largest
 * channel at full strength, retaining their relative color. */
static inline CrLightmapRgb cr_night_vision_rgb(
        float r, float g, float b, float amount)
{
    if (amount > 0.0f) {
        float scale = 1.0f / r;
        if (scale > 1.0f / g) scale = 1.0f / g;
        if (scale > 1.0f / b) scale = 1.0f / b;
        r = r * (1.0f - amount) + r * scale * amount;
        g = g * (1.0f - amount) + g * scale * amount;
        b = b * (1.0f - amount) + b * scale * amount;
    }
    CrLightmapRgb out = { r, g, b };
    return out;
}

/* EntityRenderer.updateLightmap for one (sky, block) texel. Inputs are the
 * actual 0..15 light levels, provider sunBrightness, torchFlickerX, and gamma.
 * Boss tint and lightning are intentionally absent. Night Vision is supplied
 * as EntityRenderer.getNightVisionBrightness for the rendered frame. */
static inline CrLightmapRgb cr_lightmap_rgb_night_vision(
        int dimension, int sky, int block, float sun_brightness,
        float torch_flicker_x, float gamma, float night_vision)
{
    float f = sun_brightness;
    float f1 = f * 0.95f + 0.05f;
    float f2 = cr_light_brightness(dimension, sky) * f1;
    float f3 = cr_light_brightness(dimension, block)
             * (torch_flicker_x * 0.1f + 1.5f);
    float sun_mix = f * 0.65f + 0.35f;
    float f4 = f2 * sun_mix;
    float f5 = f2 * sun_mix;
    float f6 = f3 * ((f3 * 0.6f + 0.4f) * 0.6f + 0.4f);
    float f7 = f3 * (f3 * f3 * 0.6f + 0.4f);
    float r = f4 + f3;
    float g = f5 + f6;
    float b = f2 + f7;

    /* Generic dimensions condition once before the special End replacement. */
    r = r * 0.96f + 0.03f;
    g = g * 0.96f + 0.03f;
    b = b * 0.96f + 0.03f;

    /* EntityRenderer's dimension-id 1 override replaces the conditioned values. */
    if (dimension == 1) {
        r = 0.22f + f3 * 0.75f;
        g = 0.28f + f6 * 0.75f;
        b = 0.25f + f7 * 0.75f;
    }

    {
        CrLightmapRgb nv = cr_night_vision_rgb(r, g, b, night_vision);
        r = nv.r; g = nv.g; b = nv.b;
    }

    r = cr_lm_clamp01(r);
    g = cr_lm_clamp01(g);
    b = cr_lm_clamp01(b);

    CrLightmapRgb out;
    out.r = cr_lm_gamma_finish(r, gamma);
    out.g = cr_lm_gamma_finish(g, gamma);
    out.b = cr_lm_gamma_finish(b, gamma);
    return out;
}

static inline CrLightmapRgb cr_lightmap_rgb(int dimension, int sky, int block,
                                            float sun_brightness,
                                            float torch_flicker_x,
                                            float gamma)
{
    return cr_lightmap_rgb_night_vision(
        dimension, sky, block, sun_brightness, torch_flicker_x, gamma, 0.0f);
}

/* DynamicTexture stores `(int)(channel * 255)`, i.e. truncation, not rounding. */
static inline CrRgba cr_lightmap_rgba8(CrLightmapRgb c)
{
    CrRgba out;
    out.r = (u8)(cr_lm_clamp01(c.r) * 255.0f);
    out.g = (u8)(cr_lm_clamp01(c.g) * 255.0f);
    out.b = (u8)(cr_lm_clamp01(c.b) * 255.0f);
    out.a = 255;
    return out;
}

#endif /* MAGMA_WORLD_LIGHTMAP_H */
