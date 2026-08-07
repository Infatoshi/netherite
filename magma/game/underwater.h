/* game/underwater.h - eye-in-fluid render state (fog, FOV, screen overlay).
 *
 * Vanilla sources (java/oracle-src, 1.11.2):
 *   ActiveRenderInfo.getBlockStateAtEntityViewpoint - the eye-in-fluid test
 *     used by updateFogColor / setupFog / getFOVModifier: block at the eye
 *     pos; if liquid, surface at (y+1) - (getLiquidHeightPercent(level) -
 *     0.11111111F); an eye at/above that reads the block ABOVE instead.
 *   ActiveRenderInfo.projectViewFromEntity - that "eye pos" is NOT posY +
 *     getEyeHeight: it is the interpolated FEET position plus the static
 *     `position` vector, which updateRenderInfo obtains by gluUnProject-ing
 *     the viewport centre at winZ 0 (the NEAR PLANE) through the finished
 *     modelview. First person, that modelview is
 *       [hurtCam] [bobbing] translate(0,0,0.05) rot(pitch) rot(yaw+180)
 *       translate(0,-eyeHeight,0)
 *     (EntityRenderer.orientCamera:681, :693-699, :702) and the projection is
 *     gluPerspective(..., zNear = 0.05F, ...) (EntityRenderer:730). Inverting:
 *     the camera sits 0.05 ahead of the eye and the near plane another 0.05
 *     ahead of that, so
 *       viewpoint = (x, y + eyeHeight, z) + 0.1 * getVectorForRotation(pitch,
 *                                                                      yaw)
 *     plus the bobbing/hurt-camera screen offsets. Both of those are zero on
 *     these tapes (EntityPlayer.onLivingUpdate zeroes cameraYaw's target
 *     whenever !onGround, and no tape frame is inside hurtTime at a fluid
 *     boundary), so only the 0.1 look-ahead is modelled. Without it magma
 *     leaves a waterfall a full tick late (elytra_dense t=78: the eye is at
 *     x 11.988, still cell 11, but the oracle viewpoint is 11.988 + 0.099 =
 *     cell 12 = air).
 *   EntityRenderer.updateFogColor - water branch overwrites the fog color
 *     with (0.02, 0.02, 0.2) (+ respiration/water-breathing, none here);
 *     lava (0.6, 0.1, 0.0); then EVERY branch multiplies by f13 =
 *     lerp(fogColor2, fogColor1, partialTicks) (== fogColor1 at the tick
 *     boundary), the light-at-feet brightness smoother from updateRenderer;
 *     Night Vision then normalizes the three channels toward max == 1.
 *   EntityRenderer.setupFog - water: GL_EXP density 0.1 (respiration -0.03/
 *     level, water breathing 0.01; none here); lava: GL_EXP density 2.0.
 *   EntityRenderer.getFOVModifier - eye in water scales fov by 60/70.
 *   ItemRenderer.renderOverlays -> renderWaterOverlayTexture - gated on
 *     player.isInsideOfMaterial(WATER) (ForgeHooks: eyes < y + 1 + filled),
 *     draws misc/underwater.png on a full-screen quad at view z = -0.5,
 *     UV 4x4 tiles shifted by (-yaw/64, pitch/64), color(brightness x3, 0.5),
 *     SRC_ALPHA/ONE_MINUS_SRC_ALPHA.
 */
#ifndef MAGMA_GAME_UNDERWATER_H
#define MAGMA_GAME_UNDERWATER_H

#include "core/types.h"
#include "game/game.h"
#include "mc_math.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   fluid;         /* eye viewpoint material: 0 none, 1 water, 2 lava */
    int   blindness;     /* active Blindness overrides every fluid fog mode */
    int   overlay;       /* ForgeHooks isInsideOfMaterial(WATER): draw overlay */
    CrVec3 fog01;        /* setupFog fluid color * fogColor1, linear 0..1 */
    CrRgba fog_rgba;     /* same, quantized for CrShadeCtx.fog_color */
    float density;       /* GL_EXP density (water 0.1, lava 2.0) */
    float fog_start;     /* setupFog(0) Blindness GL_LINEAR range */
    float fog_end;
    float fov_scale;     /* getFOVModifier: 60/70 in water, else 1.0 */
    float brightness;    /* Entity.getBrightness at the eye block (overlay) */
} GmUnderwater;

/* One tick of EntityRenderer.updateRenderer's fogColor1 smoothing:
 *   f3 = world.getLightBrightness(BlockPos(entity)) (feet), including
 *        useNeighborBrightness where the block registry enables it
 *   f4 = renderDistanceChunks / 32   (pinned RD 8 -> 0.25, see sky.h)
 *   c1 += (f3*(1-f4) + f4 - c1) * 0.1
 * Call once per game tick; seed the state with gm_uw_fog_c1_seed at start
 * (the oracle client has been running long before recstart, so its c1 has
 * converged to the steady state). */
float gm_uw_fog_c1_step(float c1, const GmWorld *w, int dim,
                        double feet_x, double feet_y, double feet_z);
float gm_uw_fog_c1_seed(const GmWorld *w, int dim,
                        double feet_x, double feet_y, double feet_z);

/* Entity.isInsideOfMaterial(WATER) through ForgeHooks. The eye block must be
 * water; Forge's positive BlockLiquid filled-height branch is then always
 * true for an eye inside that same integer cell. */
static inline int gm_uw_eye_inside_water(
        const GmWorld *w, double feet_x, double feet_y, double feet_z,
        float eye_height) {
    int x = mc_floor(feet_x);
    int y = mc_floor(feet_y + (double)eye_height);
    int z = mc_floor(feet_z);
    int id = gm_world_block(w, x, y, z);
    return id == 8 || id == 9;
}

/* Evaluate the frame's eye-in-fluid state from the live world + player view.
 * fog_c1 is the smoothed brightness state (f13 at partialTicks 1.0);
 * night_vision is EntityRenderer.getNightVisionBrightness. Blindness and the
 * world's void-fog factor feed both updateFogColor and setupFog. */
void gm_uw_eval(const GmWorld *w, int dim, const GmPlayerView *pv,
                float fog_c1, float night_vision, int blindness_duration,
                double void_fog_y_factor, GmUnderwater *out);

/* Apply setupFog(0) to any world-scene pass. Blindness wins over water/lava,
 * matching EntityRenderer's branch order. clear_color is updateFogColor's
 * final value for the frame. */
static inline void gm_view_fog_apply(
        CrShadeCtx *shade, const GmUnderwater *view, CrRgba clear_color) {
    if (!shade || !view) return;
    if (view->blindness) {
        shade->enable_fog = 1;
        shade->fog_color = clear_color;
        shade->fog_start = view->fog_start;
        shade->fog_end = view->fog_end;
        shade->fog_exp_density = 0.0f;
    } else if (view->fluid) {
        shade->enable_fog = 1;
        shade->fog_color = view->fog_rgba;
        shade->fog_exp_density = view->density;
    }
}

/* ItemRenderer.renderWaterOverlayTexture: full-screen underwater.png quad,
 * NEAREST + REPEAT, modulated by (brightness, brightness, brightness, 0.5),
 * src-over blend. fov_deg is the ACTIVE hand projection fov (60 in water). */
void gm_uw_overlay_draw(CrFramebuffer *fb, const GmPlayerView *pv,
                        float brightness, float fov_deg);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_UNDERWATER_H */
