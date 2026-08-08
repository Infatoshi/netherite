/* game/sky.c - MC 1.11.2 sky for the magma software rasterizer. See sky.h.
 *
 * Color math is ported straight from REAL MC (the oracle at java/oracle-src):
 *   - WorldProvider.calculateCelestialAngle  -> mc_celestial_angle
 *   - Biome.getSkyColorByTemp + MathHelper.hsvToRGB -> mc_sky_base_color
 *   - World.getSkyColorBody day/night factor f1 (cos(angle*2PI)*2+0.5)
 *   - WorldProvider.getFogColor              -> mc_fog_color   (the horizon color)
 *   - WorldProvider.calcSunriseSunsetColors  -> mc_sunset_colors
 *   - World.getStarBrightnessBody            -> mc_star_brightness
 *   - RenderGlobal.renderSky sun/moon celestial placement (rotate -90 about Y, then
 *     celestialAngle*360 about X of the +Y=(0,100,0) quad) -> sun_dir = (-sin,cos,0).
 *   - EntityRenderer.updateFogColor + setupFog(-1) sky fog over RenderGlobal's
 *     flat sky planes.
 * The normal (non-anaglyph) world render uses renderWorldPass(2), so RenderGlobal's
 * "if (pass != 2)" luminance desaturation is SKIPPED -> we use the raw sky/sunset colors.
 */
#include "game/sky.h"
#define MAGMA_POTION_RENDER_FOG_ONLY
#include "game/potion_render.h"
#undef MAGMA_POTION_RENDER_FOG_ONLY
#include "assets/sky_atlas.h"   /* CR_SUN_RGBA / CR_MOON_RGBA / CR_CLOUDS_RGBA (real MC PNGs) */
#include "world/lightmap.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>   /* getenv/atoi for the MAGMA_FOG gate */

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

/* Base biome temperature used for the overworld sky hue. Plains == 0.8 (the classic
 * MC daytime blue). getSkyBlendColour blends neighbours, but for a uniform biome the
 * value is unchanged, so a single temperature is the faithful v1 anchor. */
#define SKY_TEMP 0.8f
#define SKY_RENDER_DISTANCE_CHUNKS 8.0f   /* goldens captured at renderDistance:8 (run/options.txt) */
#define SKY_FOG_END (SKY_RENDER_DISTANCE_CHUNKS * 16.0f)

/* ---------------- small pure helpers (host + device) ---------------- */

static CR_HD float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static CR_HD float clamp01(float x) { return clampf(x, 0.0f, 1.0f); }

static CR_HD float smoothstepf(float a, float b, float x) {
    if (a == b) return x < a ? 0.0f : 1.0f;
    float t = clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

static CR_HD CrVec3 v3(float x, float y, float z) { CrVec3 r; r.x = x; r.y = y; r.z = z; return r; }
static CR_HD float  v3dot(CrVec3 a, CrVec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static CR_HD CrVec3 v3norm(CrVec3 a) {
    float l = sqrtf(a.x*a.x + a.y*a.y + a.z*a.z);
    if (l <= 1e-12f) return v3(0.0f, 1.0f, 0.0f);
    float inv = 1.0f / l; return v3(a.x*inv, a.y*inv, a.z*inv);
}
static CR_HD CrVec3 v3mix(CrVec3 a, CrVec3 b, float t) {
    return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

/* 2D hash -> [0,1), deterministic, no libm state. */
static CR_HD float hash21(float x, float y) {
    float s = sinf(x * 127.1f + y * 311.7f) * 43758.5453f;
    return s - floorf(s);
}

/* ---------------- real-texture sampling (MC sky PNGs, RGBA byte arrays) ----------------
 * Nearest-neighbour sample of one of the generated RGBA arrays in sky_atlas.h.
 * `u,v` in [0,1] (v=0 is the top row, matching the PNG byte order). Out-of-range
 * UVs are clamped so a body's quad sample outside its texture yields the border. */
static CR_HD CrVec4 tex_sample(const unsigned char *rgba, int w, int h, float u, float v) {
    int ix = (int)(u * (float)w); if (ix < 0) ix = 0; if (ix >= w) ix = w - 1;
    int iy = (int)(v * (float)h); if (iy < 0) iy = 0; if (iy >= h) iy = h - 1;
    const unsigned char *p = rgba + ((size_t)iy * w + ix) * 4;
    CrVec4 c; c.x = p[0] / 255.0f; c.y = p[1] / 255.0f; c.z = p[2] / 255.0f; c.w = p[3] / 255.0f;
    return c;
}

/* Repeat-wrapped nearest sample used by renderSkyEnd's UV 0..16 cube faces. */
static CR_HD CrRgba end_tex_sample(float u, float v) {
    u = u - floorf(u);
    v = v - floorf(v);
    int ix = (int)(u * (float)CR_END_SKY_W);
    int iy = (int)(v * (float)CR_END_SKY_H);
    if (ix < 0) ix = 0; else if (ix >= CR_END_SKY_W) ix = CR_END_SKY_W - 1;
    if (iy < 0) iy = 0; else if (iy >= CR_END_SKY_H) iy = CR_END_SKY_H - 1;
    const unsigned char *p = CR_END_SKY_RGBA + ((size_t)iy * CR_END_SKY_W + ix) * 4;
    CrRgba out;
    /* GL_MODULATE with RenderGlobal's byte vertex colour (40,40,40,255). */
    out.r = (u8)(((int)p[0] * 40 + 127) / 255);
    out.g = (u8)(((int)p[1] * 40 + 127) / 255);
    out.b = (u8)(((int)p[2] * 40 + 127) / 255);
    out.a = 255;
    return out;
}

/* RenderGlobal.renderSkyEnd cube mapping. The sky cube is camera-relative in X/Z,
 * but its model-view translation is relative to player feet, so the eye begins at
 * y=eyeHeight inside the cube. EntityRenderer also applies a 0.05-block first-person
 * camera translation before the view rotations. Those sub-block offsets matter because
 * every face repeats the 128px texture 16 times. */
static CR_HD CrRgba end_sky_ray_from(CrVec3 origin, CrVec3 d) {
    float tx = fabsf(d.x) > 1e-20f
             ? (((d.x > 0.0f ? 100.0f : -100.0f) - origin.x) / d.x) : INFINITY;
    float ty = fabsf(d.y) > 1e-20f
             ? (((d.y > 0.0f ? 100.0f : -100.0f) - origin.y) / d.y) : INFINITY;
    float tz = fabsf(d.z) > 1e-20f
             ? (((d.z > 0.0f ? 100.0f : -100.0f) - origin.z) / d.z) : INFINITY;
    float t = tx;
    int axis = 0;
    if (ty < t) { t = ty; axis = 1; }
    if (tz < t) { t = tz; axis = 2; }
    if (!isfinite(t) || t <= 0.0f) {
        CrRgba black = {0, 0, 0, 255};
        return black;
    }
    CrVec3 p = v3(origin.x + d.x * t,
                  origin.y + d.y * t,
                  origin.z + d.z * t);
    float u, v;
    if (axis == 0) {
        u = 8.0f + (d.x > 0.0f ? 0.08f : -0.08f) * p.y;
        v = 8.0f + 0.08f * p.z;
    } else if (axis == 1) {
        u = 8.0f + 0.08f * p.x;
        v = 8.0f + (d.y > 0.0f ? -0.08f : 0.08f) * p.z;
    } else {
        u = 8.0f + 0.08f * p.x;
        v = 8.0f + (d.z > 0.0f ? 0.08f : -0.08f) * p.y;
    }
    return end_tex_sample(u, v);
}

CR_HD CrRgba gm_end_sky_ray_color(CrVec3 d) {
    CrVec3 origin = {0.0f, 0.0f, 0.0f};
    return end_sky_ray_from(origin, d);
}

/* ---------------- MC color math (ported verbatim) ---------------- */

/* MathHelper.hsvToRGB (value hard-coded caller uses value=1.0, but keep general). */
static CR_HD CrVec3 hsv2rgb(float hue, float sat, float val) {
    int i = (int)(hue * 6.0f) % 6;
    if (i < 0) i += 6;
    float f  = hue * 6.0f - (float)((int)(hue * 6.0f));
    float f1 = val * (1.0f - sat);
    float f2 = val * (1.0f - f * sat);
    float f3 = val * (1.0f - (1.0f - f) * sat);
    switch (i) {
        case 0:  return v3(val, f3, f1);
        case 1:  return v3(f2, val, f1);
        case 2:  return v3(f1, val, f3);
        case 3:  return v3(f1, f2, val);
        case 4:  return v3(f3, f1, val);
        default: return v3(val, f1, f2);
    }
}

/* WorldProvider.calculateCelestialAngle, with tod = (worldTime%24000)/24000 in [0,1). */
static CR_HD float mc_celestial_angle(float tod) {
    float f = tod - 0.25f;
    if (f < 0.0f) f += 1.0f;
    if (f > 1.0f) f -= 1.0f;
    float f1 = 1.0f - (cosf(f * M_PIf) + 1.0f) * 0.5f;
    return f + (f1 - f) / 3.0f;
}

/* Biome.getSkyColorByTemp. */
static CR_HD CrVec3 mc_sky_base_color(float temp) {
    float t = clampf(temp / 3.0f, -1.0f, 1.0f);
    CrVec3 c = hsv2rgb(0.62222224f - t * 0.05f, 0.5f + t * 0.1f, 1.0f);
    int r = (int)(c.x * 255.0f);
    int g = (int)(c.y * 255.0f);
    int b = (int)(c.z * 255.0f);
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return v3((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f);
}

/* World.getSkyColorBody day/night scale f1 (clear weather, no lightning). */
static CR_HD float mc_sky_daynight(float celestial) {
    return clamp01(cosf(celestial * 2.0f * M_PIf) * 2.0f + 0.5f);
}

/* WorldProvider.getFogColor -> the horizon atmosphere color (overworld, clear). */
static CR_HD CrVec3 mc_fog_color(float celestial) {
    float f = clamp01(cosf(celestial * 2.0f * M_PIf) * 2.0f + 0.5f);
    float f1 = 0.7529412f  * (f * 0.94f + 0.06f);
    float f2 = 0.84705883f * (f * 0.94f + 0.06f);
    float f3 = 1.0f        * (f * 0.91f + 0.09f);
    return v3(f1, f2, f3);
}

/* EntityRenderer.updateFogColor, clear weather/no potion/no void-fog branch.
 * Render distance is fixed at the capture harness/default value 12 chunks:
 *   f = 1 - pow(0.25 + 0.75 * renderDistanceChunks / 32, 0.25)
 *   fog = world.getFogColor + (world.getSkyColor - world.getFogColor) * f
 * computed with two sqrtf calls rather than powf to keep the CR_HD shader pure. */
static CR_HD CrVec3 mc_view_fog_color(CrVec3 sky, CrVec3 provider_fog) {
    float x = 0.25f + 0.75f * SKY_RENDER_DISTANCE_CHUNKS / 32.0f;
    float f = 1.0f - sqrtf(sqrtf(x));
    return v3mix(provider_fog, sky, f);
}

/* RenderGlobal.renderSky first draws glSkyList, a tiled flat plane at y=+16 in
 * FEET-relative space (generateSky / renderSky(posY=16), tiles of size 64 from
 * -384..384), with vertex color world.getSkyColor. EntityRenderer.orientCamera
 * then applies
 *   GlStateManager.translate(0.0F, -entity.getEyeHeight(), 0.0F)
 * before the sky draw, so the plane sits at y = 16 - eyeHeight in the
 * camera-centered frame (standing 1.62 -> 14.38). setupFog(-1) has GL_LINEAR
 * fog start=0 and end=farPlaneDistance (renderDistanceChunks*16).
 *
 * Fixed-function GL fog is evaluated PER VERTEX on those 64x64 tiles, then
 * Gouraud-interpolated across the fragment. Per-pixel ray fog (t = plane_y/d.y)
 * is close at the horizon but too bright at the zenith: tile corners are farther
 * than the centre hit, so vertex fog darkens the disc. We reproduce the vertex
 * path: hit the plane, bilinear-sample the four surrounding tile corners.
 *
 * The under-horizon glSkyList2 uses y=-16 and black vertices, but RenderGlobal
 * only draws it when eyeY < world.getHorizon(). Downward rays remain clear/fog. */
#define SKY_TILE 64.0f
static CR_HD CrVec3 mc_sky_corner_fog(CrVec3 vertex_color, CrVec3 fog_color,
                                      float cx, float plane_y, float cz,
                                      float fog_end) {
    float dist = sqrtf(cx * cx + plane_y * plane_y + cz * cz);
    float fog_factor = clamp01((fog_end - dist) / fog_end);
    return v3mix(fog_color, vertex_color, fog_factor);
}
static CR_HD CrVec3 mc_sky_plane_fog(CrVec3 vertex_color, CrVec3 fog_color,
                                     CrVec3 dir, float plane_y, float fog_end) {
    float dir_y = dir.y;
    if ((plane_y > 0.0f && dir_y <= 0.0f) || (plane_y < 0.0f && dir_y >= 0.0f)) {
        return fog_color;
    }
    float t = plane_y / dir_y;
    if (t <= 0.0f) return fog_color;
    /* Hit in the camera-centred frame (origin at the eye, Y up). */
    float px = dir.x * t;
    float pz = dir.z * t;
    float tx0 = floorf(px / SKY_TILE) * SKY_TILE;
    float tz0 = floorf(pz / SKY_TILE) * SKY_TILE;
    float fx = (px - tx0) / SKY_TILE;
    float fz = (pz - tz0) / SKY_TILE;
    CrVec3 c00 = mc_sky_corner_fog(
        vertex_color, fog_color, tx0, plane_y, tz0, fog_end);
    CrVec3 c10 = mc_sky_corner_fog(
        vertex_color, fog_color, tx0 + SKY_TILE, plane_y, tz0, fog_end);
    CrVec3 c01 = mc_sky_corner_fog(
        vertex_color, fog_color, tx0, plane_y, tz0 + SKY_TILE, fog_end);
    CrVec3 c11 = mc_sky_corner_fog(vertex_color, fog_color, tx0 + SKY_TILE, plane_y,
                                   tz0 + SKY_TILE, fog_end);
    CrVec3 c0 = v3mix(c00, c10, fx);
    CrVec3 c1 = v3mix(c01, c11, fx);
    return v3mix(c0, c1, fz);
}

/* WorldProvider.calcSunriseSunsetColors. Returns 1 if a glow is active (fills rgba). */
static CR_HD int mc_sunset_colors(float celestial, float out_rgba[4]) {
    float f1 = cosf(celestial * 2.0f * M_PIf);
    if (f1 >= -0.4f && f1 <= 0.4f) {
        float f3 = (f1 + 0.0f) / 0.4f * 0.5f + 0.5f;
        float f4 = 1.0f - (1.0f - sinf(f3 * M_PIf)) * 0.99f;
        f4 = f4 * f4;
        out_rgba[0] = f3 * 0.3f + 0.7f;
        out_rgba[1] = f3 * f3 * 0.7f + 0.2f;
        out_rgba[2] = 0.2f;
        out_rgba[3] = f4;
        return 1;
    }
    return 0;
}

/* World.getStarBrightnessBody. */
static CR_HD float mc_star_brightness(float celestial) {
    float f1 = clamp01(1.0f - (cosf(celestial * 2.0f * M_PIf) * 2.0f + 0.25f));
    return f1 * f1 * 0.5f;
}

/* ---------------- eye-in-fluid fog override (host frame state) ---------------- */

#if !defined(__CUDA_ARCH__)
static int    g_fluid_fog_on = 0;
static CrVec3 g_fluid_fog_col;
static float  g_fluid_fog_density = 0.0f;
/* updateFogColor f13: light-at-feet fog brightness smoother (fogColor1). */
static float  g_fog_c1 = 1.0f;
static float  g_rain_strength = 0.0f;
static float  g_thunder_strength = 0.0f;
static float  g_night_vision = 0.0f;
static int    g_blindness_duration = 0;
static double g_fog_feet_y = 64.0;
static double g_void_fog_y_factor = 0.03125;
/* Entity.getEyeHeight for orientCamera's -eyeHeight translate (default standing). */
static float  g_eye_height = 1.62f;
#endif

void gm_sky_set_fluid_fog(int on, CrVec3 fog01, float density) {
#if !defined(__CUDA_ARCH__)
    g_fluid_fog_on = on;
    g_fluid_fog_col = fog01;
    g_fluid_fog_density = density;
#else
    (void)on; (void)fog01; (void)density;
#endif
}

void gm_sky_set_fog_c1(float fog_c1) {
#if !defined(__CUDA_ARCH__)
    g_fog_c1 = fog_c1 < 0.0f ? 0.0f : (fog_c1 > 1.0f ? 1.0f : fog_c1);
#else
    (void)fog_c1;
#endif
}

void gm_sky_set_weather(float rain_strength, float thunder_strength) {
#if !defined(__CUDA_ARCH__)
    g_rain_strength = clamp01(rain_strength);
    g_thunder_strength = clamp01(thunder_strength);
#else
    (void)rain_strength; (void)thunder_strength;
#endif
}

CrTexture gm_weather_rain_texture(void) {
    CrTexture t = {0};
    t.w = CR_RAIN_W; t.h = CR_RAIN_H;
    t.texels = (const CrRgba *)CR_RAIN_RGBA;
    return t;
}

CrTexture gm_weather_snow_texture(void) {
    CrTexture t = {0};
    t.w = CR_SNOW_W; t.h = CR_SNOW_H;
    t.texels = (const CrRgba *)CR_SNOW_RGBA;
    return t;
}

void gm_sky_set_night_vision(float amount) {
#if !defined(__CUDA_ARCH__)
    g_night_vision = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
#else
    (void)amount;
#endif
}

void gm_sky_set_void_blindness(
        int blindness_duration, double feet_y, double void_fog_y_factor) {
#if !defined(__CUDA_ARCH__)
    g_blindness_duration = blindness_duration > 0
        ? blindness_duration : 0;
    g_fog_feet_y = feet_y;
    g_void_fog_y_factor = void_fog_y_factor;
#else
    (void)blindness_duration; (void)feet_y; (void)void_fog_y_factor;
#endif
}

void gm_sky_set_eye_height(float eye_height) {
#if !defined(__CUDA_ARCH__)
    /* Match Entity.getEyeHeight floor: ignore non-positive (keeps last/default). */
    if (eye_height > 0.01f) g_eye_height = eye_height;
#else
    (void)eye_height;
#endif
}

/* ---------------- the per-ray shader ---------------- */

/* Frame-constant part of the sky shader, hoisted so gm_sky_draw's per-pixel
 * loop does no trig for frame-level quantities. Runs the SAME operations in
 * the SAME order the per-ray path used to run them -> bit-identical output.
 * (GmSkyCtx struct lives in sky.h so the CUDA backend can receive it.) */
static CR_HD GmSkyCtx gm_sky_ctx(float time_of_day) {
    GmSkyCtx c;
    float celestial = mc_celestial_angle(time_of_day);
    float ang = celestial * 2.0f * M_PIf;
    float daylight = mc_sky_daynight(celestial);
    CrVec3 sky_top = mc_sky_base_color(SKY_TEMP);
    c.sky_top = v3(sky_top.x * daylight, sky_top.y * daylight, sky_top.z * daylight);
#if !defined(__CUDA_ARCH__)
    if (g_rain_strength > 0.0f) {
        float gray = (c.sky_top.x * 0.3f + c.sky_top.y * 0.59f
                    + c.sky_top.z * 0.11f) * 0.6f;
        float keep = 1.0f - g_rain_strength * 0.75f;
        c.sky_top = v3(c.sky_top.x * keep + gray * (1.0f - keep),
                       c.sky_top.y * keep + gray * (1.0f - keep),
                       c.sky_top.z * keep + gray * (1.0f - keep));
    }
    if (g_thunder_strength > 0.0f) {
        float gray = (c.sky_top.x * 0.3f + c.sky_top.y * 0.59f
                    + c.sky_top.z * 0.11f) * 0.2f;
        float keep = 1.0f - g_thunder_strength * 0.75f;
        c.sky_top = v3(c.sky_top.x * keep + gray * (1.0f - keep),
                       c.sky_top.y * keep + gray * (1.0f - keep),
                       c.sky_top.z * keep + gray * (1.0f - keep));
    }
#endif
    c.fog = mc_view_fog_color(c.sky_top, mc_fog_color(celestial));
#if !defined(__CUDA_ARCH__)
    if (g_rain_strength > 0.0f) {
        c.fog.x *= 1.0f - g_rain_strength * 0.5f;
        c.fog.y *= 1.0f - g_rain_strength * 0.5f;
        c.fog.z *= 1.0f - g_rain_strength * 0.4f;
    }
    if (g_thunder_strength > 0.0f) {
        float keep = 1.0f - g_thunder_strength * 0.5f;
        c.fog = v3(c.fog.x * keep, c.fog.y * keep, c.fog.z * keep);
    }
    /* updateFogColor: fogColor{Red,Green,Blue} *= f13 (fogColor1). Sky plane
     * vertices stay unscaled (getSkyColor); only the fog target is dimmed -
     * after long underwater stretches fogColor1 is low and the horizon band
     * (and low-elevation sky) darkens to match oracle swim frames. */
    c.fog = v3(c.fog.x * g_fog_c1, c.fog.y * g_fog_c1, c.fog.z * g_fog_c1);
    gm_void_blindness_rgb(
        &c.fog.x, &c.fog.y, &c.fog.z, g_blindness_duration,
        g_fog_feet_y, g_void_fog_y_factor);
    {
        CrLightmapRgb nv = cr_night_vision_rgb(
            c.fog.x, c.fog.y, c.fog.z, g_night_vision);
        c.fog = v3(nv.r, nv.g, nv.b);
    }
#endif
    c.sunset_active = 0;
    c.sun_h = v3(0.0f, 0.0f, 0.0f);
    if (mc_sunset_colors(celestial, c.sunset)) {
        /* sun azimuth direction in the horizontal plane (sun_dir with y removed). */
        CrVec3 sun_dir = v3(-sinf(ang), cosf(ang), 0.0f);
        float horiz_len = sqrtf(sun_dir.x*sun_dir.x + sun_dir.z*sun_dir.z);
        if (horiz_len > 1e-4f) {
            c.sunset_active = 1;
            c.sun_h = v3(sun_dir.x / horiz_len, 0.0f, sun_dir.z / horiz_len);
        }
    }
    c.celestial_alpha = 1.0f;
#if !defined(__CUDA_ARCH__)
    c.celestial_alpha = 1.0f - g_rain_strength;
#endif
    c.starB = mc_star_brightness(celestial) * c.celestial_alpha;
    c.cA = cosf(ang);
    c.sA = sinf(ang);
    c.uw = 0;
    c.uw_fog = v3(0.0f, 0.0f, 0.0f);
    c.uw_density = 0.0f;
    c.linear_fog_end = SKY_FOG_END;
    /* orientCamera: plane at y=+16 feet-relative, then translate(0,-eyeH,0).
     * Device path without host state keeps the standing default 1.62. */
    c.plane_y = 16.0f - 1.62f;
#if !defined(__CUDA_ARCH__)
    /* host frame state (gm_sky_set_fluid_fog); the CUDA kernel gets the ctx
     * pre-built on the host, so device-compiled copies keep uw = 0. */
    c.uw = g_fluid_fog_on && g_blindness_duration <= 0;
    c.uw_fog = g_fluid_fog_col;
    c.uw_density = g_fluid_fog_density;
    if (g_blindness_duration > 0)
        c.linear_fog_end = gm_blindness_fog_end(
            g_blindness_duration, SKY_FOG_END) * 0.8f;
    c.plane_y = 16.0f - g_eye_height;
#endif
    return c;
}

static CR_HD CrRgba gm_sky_ray_color_ctx(const GmSkyCtx *sc, CrVec3 dir_in) {
    CrVec3 dir = v3norm(dir_in);
    float ey = dir.y;                          /* sin(elevation) */

    if (sc->uw) {
        /* Eye in fluid: setupFog's fluid branch is GL_EXP(uw_density) toward
         * uw_fog for EVERY sky draw. Same orientCamera plane as air
         * (sc->plane_y = 16 - eyeHeight): factor = exp(-density * planeDist).
         * Sunset glow, stars and the sun/moon quads live at dist >= 100 ->
         * factor <= e^-10 at water density 0.1: invisible, skipped. Downward
         * rays never hit the plane -> pure fog color (matches clearColor). */
        CrVec3 col = sc->uw_fog;
        if (ey > 1e-4f) {
            float t = sc->plane_y / ey;
            float f = expf(-sc->uw_density * t);
            col = v3mix(sc->uw_fog, sc->sky_top, f);
        }
        CrRgba out;
        out.r = (u8)(clamp01(col.x) * 255.0f + 0.5f);
        out.g = (u8)(clamp01(col.y) * 255.0f + 0.5f);
        out.b = (u8)(clamp01(col.z) * 255.0f + 0.5f);
        out.a = 255;
        return out;
    }

    CrVec3 sky_top = sc->sky_top;
    CrVec3 fog = sc->fog;

    /* --- vertical gradient: GL linear fog on MC's 64-tile sky plane (Gouraud). --- */
    CrVec3 col = (ey >= 0.0f) ? mc_sky_plane_fog(
                                   sky_top, fog, dir, sc->plane_y,
                                   sc->linear_fog_end)
                              : fog;

    /* --- sunrise / sunset horizon glow, centered on the sun's azimuth. --- */
    if (sc->sunset_active) {
        float az = clamp01(v3dot(v3(dir.x, 0.0f, dir.z), sc->sun_h));  /* toward sun */
        float aey = ey < 0.0f ? -ey : ey;
        float low = 1.0f - smoothstepf(0.0f, 0.35f, aey);             /* hug horizon */
        float w = sc->sunset[3] * low * az * az * (ey > -0.15f ? 1.0f : 0.0f);
        CrVec3 glow = v3(sc->sunset[0], sc->sunset[1], sc->sunset[2]);
        col = v3mix(col, glow, clamp01(w));
    }

    /* --- night stars (before the sun/moon so discs sit on top). --- */
    float starB = sc->starB;
    if (starB > 0.001f && ey > 0.02f) {
        /* project the ray onto a plane and quantize to a sparse grid of points. */
        float u = (dir.x / (ey + 0.25f)) * 26.0f, v = (dir.z / (ey + 0.25f)) * 26.0f;
        float gx = floorf(u), gy = floorf(v);
        float h = hash21(gx, gy);
        if (h > 0.985f) {
            /* a single small point at a random spot inside the cell */
            float px = hash21(gx + 1.3f, gy), py = hash21(gx, gy + 2.7f);
            float dx = (u - gx) - px, dy = (v - gy) - py;
            float d2 = dx * dx + dy * dy;
            float pt = 1.0f - smoothstepf(0.0f, 0.020f, d2);   /* small round dot */
            float tw = 0.5f + 0.5f * hash21(gy, gx);
            float s = starB * tw * pt * smoothstepf(0.02f, 0.15f, ey);
            col = v3mix(col, v3(1.0f, 1.0f, 1.0f), clamp01(s));
        }
    }

    /* Clouds are not part of RenderGlobal.renderSky; vanilla draws them in the world
     * pass with a world-positioned mesh. Keep this sky fill to the actual sky pass. */

    /* --- sun: the REAL sun.png on MC's celestial quad, ADDITIVE (SRC_ALPHA,ONE). ---
     * RenderGlobal.renderSky (oracle :1376-1385): rotate(-90,Y) then rotate(cel*360,X)
     * of a flat quad at y=100 spanning [-30,30] in x,z with tex (0,0)-(1,1), f17=30.
     * That maps to a billboard perpendicular to the sun direction Csun=(-sinA,cosA,0)
     * at distance 100, half-extent 30 (angular half-size atan(30/100)=16.7deg), with
     * world basis  u_axis=(0,0,1), v_axis=(-cosA,-sinA,0)  (see kernel 36 / DEVLOG).
     * The additive black (0,0,0) background contributes nothing -> a crisp textured
     * disc. sun.png already carries its own glow, so no procedural halo. */
    {
        float cA = sc->cA, sA = sc->sA;
        CrVec3 csun = v3(-sA, cA, 0.0f);          /* unit dir to sun center */
        float denom = v3dot(dir, csun);
        if (denom > 1e-4f) {
            float t = 100.0f / denom;             /* plane dot(X,csun)=100 */
            CrVec3 P = v3(dir.x * t, dir.y * t, dir.z * t);
            float lx = P.z;                       /* dot(P, u_axis=(0,0,1)) */
            float lz = -cA * P.x - sA * P.y;      /* dot(P, v_axis=(-cosA,-sinA,0)) */
            if (lx >= -30.0f && lx <= 30.0f && lz >= -30.0f && lz <= 30.0f) {
                float tu = (lx + 30.0f) / 60.0f, tv = (lz + 30.0f) / 60.0f;
                CrVec4 s = tex_sample(CR_SUN_RGBA, CR_SUN_W, CR_SUN_H, tu, tv);
                col = v3(col.x + s.x * sc->celestial_alpha,
                         col.y + s.y * sc->celestial_alpha,
                         col.z + s.z * sc->celestial_alpha);       /* additive */
            }
        }
    }

    /* --- moon: the REAL full-moon cell of moon_phases.png on the opposite quad. ---
     * oracle :1387-1400: y=-100 (Cmoon=(sinA,-cosA,0)), f17=20, tex = phase-0 cell
     * (top-left 32x32). Additive, same basis. Half-extent 20 (atan(20/100)=11.3deg). */
    {
        float cA = sc->cA, sA = sc->sA;
        CrVec3 cmoon = v3(sA, -cA, 0.0f);
        float denom = v3dot(dir, cmoon);
        if (denom > 1e-4f) {
            float t = 100.0f / denom;
            CrVec3 P = v3(dir.x * t, dir.y * t, dir.z * t);
            float lx = P.z;
            float lz = -cA * P.x - sA * P.y;
            if (lx >= -20.0f && lx <= 20.0f && lz >= -20.0f && lz <= 20.0f) {
                float tu = (20.0f - lx) / 40.0f;      /* cell u: +20->0, -20->1 */
                float tv = (lz + 20.0f) / 40.0f;      /* cell v: -20->0, +20->1 */
                CrVec4 m = tex_sample(CR_MOON_RGBA, CR_MOON_W, CR_MOON_H, tu, tv);
                col = v3(col.x + m.x * sc->celestial_alpha,
                         col.y + m.y * sc->celestial_alpha,
                         col.z + m.z * sc->celestial_alpha);       /* additive */
            }
        }
    }

    CrRgba out;
    out.r = (u8)(clamp01(col.x) * 255.0f + 0.5f);
    out.g = (u8)(clamp01(col.y) * 255.0f + 0.5f);
    out.b = (u8)(clamp01(col.z) * 255.0f + 0.5f);
    out.a = 255;
    return out;
}

CR_HD CrRgba gm_sky_ray_color(CrVec3 dir_in, float time_of_day) {
    GmSkyCtx sc = gm_sky_ctx(time_of_day);
    return gm_sky_ray_color_ctx(&sc, dir_in);
}

/* ---------------- terrain-pass fog ---------------- */

/* Default ON (Java always runs setupFog(0) on terrain). MAGMA_FOG=0 disables.
 * Same cached-getenv pattern as MAGMA_SMOOTH. */
int gm_terrain_fog_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *s = getenv("MAGMA_FOG");
        /* unset -> ON (MC-faithful); explicit 0 disables; any non-zero enables */
        cached = (!s || atoi(s) != 0) ? 1 : 0;
    }
    return cached;
}

/* EntityRenderer.updateFogColor's view-fog color at `time_of_day`. Identical to the
 * `fog` variable gm_sky_ray_color fades the horizon to, so far terrain fogged with
 * this color meets the sky at the same color. See sky.h for the setupFog(0) params. */
CrRgba gm_terrain_fog_color(float time_of_day) {
    float celestial = mc_celestial_angle(time_of_day);
    float daylight  = mc_sky_daynight(celestial);
    CrVec3 sky_top  = mc_sky_base_color(SKY_TEMP);
    sky_top = v3(sky_top.x * daylight, sky_top.y * daylight, sky_top.z * daylight);
#if !defined(__CUDA_ARCH__)
    if (g_rain_strength > 0.0f) {
        float gray = (sky_top.x * 0.3f + sky_top.y * 0.59f
                    + sky_top.z * 0.11f) * 0.6f;
        float keep = 1.0f - g_rain_strength * 0.75f;
        sky_top = v3(sky_top.x * keep + gray * (1.0f - keep),
                     sky_top.y * keep + gray * (1.0f - keep),
                     sky_top.z * keep + gray * (1.0f - keep));
    }
    if (g_thunder_strength > 0.0f) {
        float gray = (sky_top.x * 0.3f + sky_top.y * 0.59f
                    + sky_top.z * 0.11f) * 0.2f;
        float keep = 1.0f - g_thunder_strength * 0.75f;
        sky_top = v3(sky_top.x * keep + gray * (1.0f - keep),
                     sky_top.y * keep + gray * (1.0f - keep),
                     sky_top.z * keep + gray * (1.0f - keep));
    }
#endif
    CrVec3 fog = mc_view_fog_color(sky_top, mc_fog_color(celestial));
    /* fogColor1 is applied in gm_sky_ctx for the sky fog target; terrain
     * fog color is also scaled here so horizon terrain meets the same
     * clearColor (updateFogColor f13). */
#if !defined(__CUDA_ARCH__)
    if (g_rain_strength > 0.0f) {
        fog.x *= 1.0f - g_rain_strength * 0.5f;
        fog.y *= 1.0f - g_rain_strength * 0.5f;
        fog.z *= 1.0f - g_rain_strength * 0.4f;
    }
    if (g_thunder_strength > 0.0f) {
        float keep = 1.0f - g_thunder_strength * 0.5f;
        fog = v3(fog.x * keep, fog.y * keep, fog.z * keep);
    }
    fog = v3(fog.x * g_fog_c1, fog.y * g_fog_c1, fog.z * g_fog_c1);
    gm_void_blindness_rgb(
        &fog.x, &fog.y, &fog.z, g_blindness_duration,
        g_fog_feet_y, g_void_fog_y_factor);
    {
        CrLightmapRgb nv = cr_night_vision_rgb(
            fog.x, fog.y, fog.z, g_night_vision);
        fog = v3(nv.r, nv.g, nv.b);
    }
#endif
    CrRgba out;
    out.r = (u8)(clamp01(fog.x) * 255.0f + 0.5f);
    out.g = (u8)(clamp01(fog.y) * 255.0f + 0.5f);
    out.b = (u8)(clamp01(fog.z) * 255.0f + 0.5f);
    out.a = 255;
    return out;
}

/* ---------------- full-frame fill ---------------- */

/* Camera basis (inverse of the transform's view rotation; see core/math.c header):
 *   forward F = (-sin yaw cos pitch, sin pitch, -cos yaw cos pitch)
 *   right   R = ( cos yaw, 0, -sin yaw)
 *   up      U = ( sin yaw sin pitch, cos pitch, cos yaw sin pitch)
 * A pixel ray (eye z=-1) is  ndc_x*tanH*aspect*R + ndc_y*tanH*U + F, which is exactly
 * the inverse of cr_perspective*cr_look_yaw_pitch, with no matrix inverse needed. */
static void sky_cam_basis(const CrCamera *cam, float b[11]) {
    if (cam->hurt_roll_deg == 0.0f) {
        /* Preserve the original operation sequence bit-for-bit. End-sky cube
         * selection is sensitive to even sub-ulp ray changes. */
        float cy = cosf(cam->yaw), sy = sinf(cam->yaw);
        float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
        b[0] = -sy * cp; b[1] = sp;   b[2] = -cy * cp; /* F */
        b[3] =  cy;      b[4] = 0.0f; b[5] = -sy;      /* R */
        b[6] =  sy * sp; b[7] = cp;   b[8] =  cy * sp; /* U */
    } else {
        /* Invert the complete hurt-camera view rotation by transposing its
         * orthonormal 3x3. */
        CrMat4 view = cr_camera_view(cam);
        b[0] = -view.m[2]; b[1] = -view.m[6]; b[2] = -view.m[10]; /* F */
        b[3] =  view.m[0]; b[4] =  view.m[4]; b[5] =  view.m[8];  /* R */
        b[6] =  view.m[1]; b[7] =  view.m[5]; b[8] =  view.m[9];  /* U */
    }
    float half_fov = cam->fov_deg * (M_PIf / 180.0f) * 0.5f;
    b[9] = sinf(half_fov) / cosf(half_fov);             /* tanH */
    b[10] = cam->aspect;
}

void gm_sky_frame_args(const CrCamera *cam, float time_of_day,
                       GmSkyCtx *sc, float basis[11]) {
    *sc = gm_sky_ctx(time_of_day);
    sky_cam_basis(cam, basis);
}

void gm_sky_draw(CrFramebuffer *fb, const CrCamera *cam, float time_of_day) {
    if (!fb || !fb->color || !cam) return;

    float b[11];
    sky_cam_basis(cam, b);
    CrVec3 F = v3(b[0], b[1], b[2]);
    CrVec3 R = v3(b[3], b[4], b[5]);
    CrVec3 U = v3(b[6], b[7], b[8]);
    float tanH = b[9];
    float aspect = b[10];
    int W = fb->w, H = fb->h;
    GmSkyCtx sc = gm_sky_ctx(time_of_day);

    for (int py = 0; py < H; ++py) {
        float ndc_y = 1.0f - 2.0f * ((float)py + 0.5f) / (float)H;
        float sv = ndc_y * tanH;
        for (int px = 0; px < W; ++px) {
            int idx = py * W + px;
            if (fb->depth && fb->depth[idx] < 1.0f) continue;   /* keep terrain */
            float ndc_x = 2.0f * ((float)px + 0.5f) / (float)W - 1.0f;
            float su = ndc_x * tanH * aspect;
            CrVec3 dir = v3(su * R.x + sv * U.x + F.x,
                            su * R.y + sv * U.y + F.y,
                            su * R.z + sv * U.z + F.z);
            fb->color[idx] = gm_sky_ray_color_ctx(&sc, dir);
        }
    }
}

void gm_end_sky_draw(CrFramebuffer *fb, const CrCamera *cam) {
    if (!fb || !fb->color || !cam) return;
    float b[11];
    sky_cam_basis(cam, b);
    CrVec3 F = v3(b[0], b[1], b[2]);
    CrVec3 R = v3(b[3], b[4], b[5]);
    CrVec3 U = v3(b[6], b[7], b[8]);
    float tanH = b[9];
    int W = fb->w, H = fb->h;
    float aspect = (float)W / (float)H;
    /* Live first-person capture: player feet are the cube origin; eye height is 1.62. */
    CrVec3 origin = v3(0.05f * F.x, 1.62f + 0.05f * F.y, 0.05f * F.z);
    for (int py = 0; py < H; ++py) {
        float ndc_y = 1.0f - 2.0f * ((float)py + 0.5f) / (float)H;
        float sv = ndc_y * tanH;
        for (int px = 0; px < W; ++px) {
            int idx = py * W + px;
            if (fb->depth && fb->depth[idx] < 1.0f) continue;
            float ndc_x = 2.0f * ((float)px + 0.5f) / (float)W - 1.0f;
            float su = ndc_x * tanH * aspect;
            CrVec3 dir = v3(su * R.x + sv * U.x + F.x,
                            su * R.y + sv * U.y + F.y,
                            su * R.z + sv * U.z + F.z);
            fb->color[idx] = end_sky_ray_from(origin, dir);
        }
    }
}
