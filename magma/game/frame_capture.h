#ifndef MAGMA_GAME_FRAME_CAPTURE_H
#define MAGMA_GAME_FRAME_CAPTURE_H

#include "game/runtime.h"
#include "game/entity_render.h"
#include "game/particles_live.h"
#include "game/underwater.h"

typedef struct GmFrameCapture GmFrameCapture;

GmFrameCapture *gm_frame_capture_open(const GmConfig *cfg, char *err, int err_cap);
void gm_frame_capture_bind_particles(GmFrameCapture *capture,
                                     GmParticlesLive *particles);
/* Oracle fixtures can carry the real client's GuiBossOverlay.createFog bit.
 * -1 restores the normal world/entity latch, 0/1 pins the captured value. */
void gm_frame_capture_set_boss_fog(GmFrameCapture *capture, int boss_fog);
/* Pin GuiBossOverlay's displayed fraction independently of a render-only
 * entity health pin. A negative value restores the live entity fraction. */
void gm_frame_capture_set_boss_fraction(GmFrameCapture *capture,
                                        float boss_fraction);
/* Call once per tick. Always advances hand-animation state; renders and
 * writes the tick-numbered PPM only when render is non-zero. */
int gm_frame_capture_write(GmFrameCapture *capture, GmRuntime *runtime,
                           const GmAction *action, int render,
                           char *err, int err_cap);
void gm_frame_capture_close(GmFrameCapture *capture);

/* Fill the 16x16 EntityRenderer.updateLightmap LUT for the given world time
 * (overworld texels; callers gate on lightmap mode + dimension 0). Shared by
 * the capture path and the interactive window loop. */
void gm_frame_lightmap_fill(const McSinTable *st, long long world_time,
                            CrRgba lut[256]);
void gm_frame_lightmap_fill_view(
    const McSinTable *st, long long world_time, float rain_strength,
    float thunder_strength, float night_vision, CrRgba lut[256]);
void gm_frame_lightmap_fill_view_boss(
    const McSinTable *st, long long world_time, float rain_strength,
    float thunder_strength, float night_vision, float boss_color_modifier,
    CrRgba lut[256]);

/* Fill per-entity light fields (lm_lit + coords/multiplier) from world light
 * at each entity's eye block, LUT path when lm is non-NULL, legacy fold
 * otherwise. Shared by the capture path and the interactive window loop. */
void gm_frame_entities_light(GmEntityView *ents, int n, GmWorld *world,
                             int dimension, const CrRgba *lm);
void gm_frame_entities_light_boss(
    GmEntityView *ents, int n, GmWorld *world, int dimension,
    float boss_color_modifier, const CrRgba *lm);

/* RenderMinecart rail reprojection and stable entity-id anti-z-fight jitter. */
void gm_frame_prepare_minecarts(GmEntityView *ents, int n, GmWorld *world);

/* EntityRenderer.setupFog linear world ramp. Dense in the Nether or while
 * BossInfo createFog is latched; shared by capture and window composition. */
void gm_frame_world_fog_params(int dimension, int boss_fog, int *enabled,
                               float *fog_start, float *fog_end);

/* EntityRenderer.updateFogColor clear/view-fog color, including Nether and
 * End provider formulas, void/Blindness, Wither boss darkening, Night Vision,
 * and the eye-in-fluid override. */
CrRgba gm_frame_clear_color(
    float time_of_day, int dimension, float fog_c1, float night_vision,
    float boss_color_modifier, int blindness_duration, double feet_y,
    double void_fog_y_factor, const GmUnderwater *uw);

#endif
