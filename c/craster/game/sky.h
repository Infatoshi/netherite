/* game/sky.h - full-frame sky for the craster software rasterizer.
 *
 * Replaces the flat blue cr_fb_clear() with a real Minecraft 1.11.2 sky:
 *   - the day sky-color -> horizon fog-color gradient (by view-ray elevation and the
 *     celestial angle), derived from the same math as REAL MC (World.getSkyColorBody,
 *     WorldProvider.getFogColor, Biome.getSkyColorByTemp, calculateCelestialAngle).
 *   - a sun disc and a moon disc at the MC celestial position.
 *   - a soft sunrise/sunset horizon glow (WorldProvider.calcSunriseSunsetColors).
 *   - a faint night star field (World.getStarBrightnessBody) and a simple cloud band.
 *
 * Intended call order: FIRST, as a full-frame fill BEFORE terrain (depth left at far).
 * It only writes pixels whose depth == far (1.0), so it is safe to call after terrain
 * too (it will not overwrite it), but the intended use replaces cr_fb_clear().
 *
 * Pure math (no libm beyond sinf/cosf/sqrtf/floorf), no globals, so a CUDA port of the
 * per-pixel shader stays a straight lift of gm_sky_ray_color().
 */
#ifndef CRASTER_GAME_SKY_H
#define CRASTER_GAME_SKY_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Full-frame sky fill. Reconstructs a world-space view ray per pixel from `cam`
 * (inverse of the transform's proj*view) and shades the sky for every pixel whose
 * fb->depth == 1.0 (far). `time_of_day` is the raw MC day fraction in [0,1)
 * (worldTime/24000): noon ~= 0.25, sunset ~= 0.5, midnight ~= 0.75, sunrise ~= 0.0. */
void gm_sky_draw(CrFramebuffer *fb, const CrCamera *cam, float time_of_day);

/* Frame-constant part of the sky shader (hoisted trig; see sky.c gm_sky_ctx).
 * Public so the CUDA backend can build it ONCE on the host with glibc libm
 * (device sinf/cosf differ in the last ulps) and pass it to the sky kernel. */
typedef struct {
    CrVec3 sky_top, fog;
    int    sunset_active;  /* sunset[] valid AND sun azimuth well-defined */
    float  sunset[4];
    CrVec3 sun_h;          /* unit horizontal dir toward the sun */
    float  starB;
    float  cA, sA;         /* cos/sin of celestial angle * 2pi (sun/moon basis) */
    /* Eye-in-fluid override (EntityRenderer.setupFog(-1) fluid branch, which
     * ignores startCoords): uw != 0 replaces the linear sky-plane fog with
     * GL_EXP(uw_density) toward uw_fog and drops sunset/stars/sun/moon (all
     * at dist >= 100 -> factor <= e^-10 at water density 0.1). Filled from
     * the host state set by gm_sky_set_fluid_fog. */
    int    uw;
    CrVec3 uw_fog;
    float  uw_density;
} GmSkyCtx;

/* Host-side (cc-compiled, -ffp-contract=off, glibc) computation of everything
 * gm_sky_draw hoists out of its pixel loop: the frame ctx and the camera ray
 * basis. basis[0..2]=F, [3..5]=R, [6..8]=U, [9]=tanH, [10]=aspect. A pixel ray
 * is  su*R + sv*U + F  with su = ndc_x*tanH*aspect, sv = ndc_y*tanH. */
void gm_sky_frame_args(const CrCamera *cam, float time_of_day,
                       GmSkyCtx *sc, float basis[11]);

/* RenderGlobal.renderSkyEnd: six camera-centered cube faces at +/-100, the real
 * end_sky.png repeated over UV 0..16, vertex colour (40,40,40), no fog/depth write. */
void gm_end_sky_draw(CrFramebuffer *fb, const CrCamera *cam);

/* Pure ray form used by the deterministic End-sky unit test. */
CR_HD CrRgba gm_end_sky_ray_color(CrVec3 dir);

/* Shade a single normalized world-space ray direction to an RGBA sky color.
 * Exposed for tests / a CUDA port. `dir` need not be unit length. */
CR_HD CrRgba gm_sky_ray_color(CrVec3 dir, float time_of_day);

/* Terrain-pass fog (EntityRenderer.setupFog(0), oracle EntityRenderer.java:2023-2050,
 * clear-weather / non-water branch): GL_LINEAR with
 *   farPlaneDistance = renderDistanceChunks * 16                    (line 714, 2025)
 *   fogStart = farPlaneDistance * 0.75                              (line 2035)
 *   fogEnd   = farPlaneDistance                                     (line 2036)
 * The goldens were captured with the REAL game at renderDistance 8 (see
 * java/Minecraft/run/options.txt renderDistance:8), so farPlaneDistance = 8*16 = 128,
 * fogStart = 96, fogEnd = 128. (Note: game/sky.c still hardcodes 12 for the SKY pass -
 * a separate discrepancy owned by that pass; the terrain pass here uses the true 8.)
 * The fog COLOR is EntityRenderer.updateFogColor's view-fog color - the SAME color
 * the sky pass fades its horizon to (mc_view_fog_color(sky*daylight, providerFog)).
 * gm_terrain_fog_color returns that RGBA at a given raw MC day fraction. */
#define GM_TERRAIN_FOG_FAR   128.0f
#define GM_TERRAIN_FOG_START (GM_TERRAIN_FOG_FAR * 0.75f)
#define GM_TERRAIN_FOG_END   (GM_TERRAIN_FOG_FAR)
CrRgba gm_terrain_fog_color(float time_of_day);

/* Terrain fog: DEFAULT ON (EntityRenderer.setupFog(0) always runs in Java).
 * Escape: CRASTER_FOG=0. Params: start=96 end=128 at RD8. On hard-scene seed0
 * this is live (horizon band); short verify poses may still be a near no-op
 * because occlusion caps eye depth below fog start. */
int gm_terrain_fog_enabled(void);

/* Per-frame eye-in-fluid fog override for the SKY pass (EntityRenderer
 * setupFog water/lava branch). `on` != 0 makes the next gm_sky_ctx /
 * gm_sky_frame_args build a ctx whose rays are EXP-fogged toward fog01
 * (linear 0..1 RGB, already scaled by the fogColor1 brightness factor) with
 * the given GL_EXP density (water 0.1, lava 2.0). Call once per frame BEFORE
 * gm_sky_draw / gm_sky_frame_args; on == 0 restores the normal sky. Host
 * state only: the CUDA sky kernel receives it through the host-built ctx. */
void gm_sky_set_fluid_fog(int on, CrVec3 fog01, float density);

/* EntityRenderer.updateFogColor f13 (= fogColor1 at partialTicks 1): multiplies
 * the clear/view-fog colour. Call each frame before gm_sky_draw /
 * gm_sky_frame_args / gm_terrain_fog_color. Host state; default 1.0. */
void gm_sky_set_fog_c1(float fog_c1);

#ifdef __cplusplus
}
#endif
#endif /* CRASTER_GAME_SKY_H */
