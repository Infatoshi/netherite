/* game/underwater.h - eye-in-fluid render state (fog, FOV, screen overlay).
 *
 * Vanilla sources (java/oracle-src, 1.11.2):
 *   ActiveRenderInfo.getBlockStateAtEntityViewpoint - the eye-in-fluid test
 *     used by updateFogColor / setupFog / getFOVModifier: block at the eye
 *     pos; if liquid, surface at (y+1) - (getLiquidHeightPercent(level) -
 *     0.11111111F); an eye at/above that reads the block ABOVE instead.
 *   EntityRenderer.updateFogColor - water branch overwrites the fog color
 *     with (0.02, 0.02, 0.2) (+ respiration/water-breathing, none here);
 *     lava (0.6, 0.1, 0.0); then EVERY branch multiplies by f13 =
 *     lerp(fogColor2, fogColor1, partialTicks) (== fogColor1 at the tick
 *     boundary), the light-at-feet brightness smoother from updateRenderer.
 *   EntityRenderer.setupFog - water: GL_EXP density 0.1 (respiration -0.03/
 *     level, water breathing 0.01; none here); lava: GL_EXP density 2.0.
 *   EntityRenderer.getFOVModifier - eye in water scales fov by 60/70.
 *   ItemRenderer.renderOverlays -> renderWaterOverlayTexture - gated on
 *     player.isInsideOfMaterial(WATER) (ForgeHooks: eyes < y + 1 + filled),
 *     draws misc/underwater.png on a full-screen quad at view z = -0.5,
 *     UV 4x4 tiles shifted by (-yaw/64, pitch/64), color(brightness x3, 0.5),
 *     SRC_ALPHA/ONE_MINUS_SRC_ALPHA.
 */
#ifndef CRASTER_GAME_UNDERWATER_H
#define CRASTER_GAME_UNDERWATER_H

#include "core/types.h"
#include "game/game.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   fluid;         /* eye viewpoint material: 0 none, 1 water, 2 lava */
    int   overlay;       /* ForgeHooks isInsideOfMaterial(WATER): draw overlay */
    CrVec3 fog01;        /* setupFog fluid color * fogColor1, linear 0..1 */
    CrRgba fog_rgba;     /* same, quantized for CrShadeCtx.fog_color */
    float density;       /* GL_EXP density (water 0.1, lava 2.0) */
    float fov_scale;     /* getFOVModifier: 60/70 in water, else 1.0 */
    float brightness;    /* Entity.getBrightness at the eye block (overlay) */
} GmUnderwater;

/* One tick of EntityRenderer.updateRenderer's fogColor1 smoothing:
 *   f3 = lightBrightnessTable[max(sky, block) at BlockPos(entity)] (feet)
 *   f4 = renderDistanceChunks / 32   (pinned RD 8 -> 0.25, see sky.h)
 *   c1 += (f3*(1-f4) + f4 - c1) * 0.1
 * Call once per game tick; seed the state with gm_uw_fog_c1_seed at start
 * (the oracle client has been running long before recstart, so its c1 has
 * converged to the steady state). */
float gm_uw_fog_c1_step(float c1, const GmWorld *w, int dim,
                        double feet_x, double feet_y, double feet_z);
float gm_uw_fog_c1_seed(const GmWorld *w, int dim,
                        double feet_x, double feet_y, double feet_z);

/* Evaluate the frame's eye-in-fluid state from the live world + player view.
 * fog_c1 is the smoothed brightness state (f13 at partialTicks 1.0). */
void gm_uw_eval(const GmWorld *w, int dim, const GmPlayerView *pv,
                float fog_c1, GmUnderwater *out);

/* ItemRenderer.renderWaterOverlayTexture: full-screen underwater.png quad,
 * NEAREST + REPEAT, modulated by (brightness, brightness, brightness, 0.5),
 * src-over blend. fov_deg is the ACTIVE hand projection fov (60 in water). */
void gm_uw_overlay_draw(CrFramebuffer *fb, const GmPlayerView *pv,
                        float brightness, float fov_deg);

#ifdef __cplusplus
}
#endif
#endif /* CRASTER_GAME_UNDERWATER_H */
