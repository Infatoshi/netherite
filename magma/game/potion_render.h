/* Player potion state used by the 1.11.2 renderer. */
#ifndef MAGMA_GAME_POTION_RENDER_H
#define MAGMA_GAME_POTION_RENDER_H

#ifndef MAGMA_POTION_RENDER_FOG_ONLY
#include "game/game.h"
#include "mc_math.h"

static inline int gm_potion_view_duration(
        const GmPlayerView *view, int potion_id) {
    if (!view) return 0;
    for (int i = 0; i < view->potion_count; ++i)
        if (view->potions[i].id == potion_id)
            return view->potions[i].duration;
    return 0;
}

/* EntityRenderer.getNightVisionBrightness. partial_ticks is the same camera
 * interpolation fraction used by updateLightmap/updateFogColor. */
static inline float gm_night_vision_brightness_duration(
        const McSinTable *sin_table, int duration, float partial_ticks) {
    if (!sin_table || duration <= 0) return 0.0f;
    if (duration > 200) return 1.0f;
    return 0.7f + mc_sin(
        sin_table,
        ((float)duration - partial_ticks) * (float)MC_PI * 0.2f) * 0.3f;
}

static inline float gm_night_vision_brightness(
        const GmPlayerView *view, const McSinTable *sin_table,
        float partial_ticks) {
    return gm_night_vision_brightness_duration(
        sin_table, gm_potion_view_duration(view, 16), partial_ticks);
}
#endif

/* EntityRenderer.setupFog BLINDNESS branch. A positive duration means the
 * effect is active; its last 19 ticks interpolate back to the ordinary far
 * plane exactly in float arithmetic. */
static inline float gm_blindness_fog_end(int duration, float far_plane) {
    if (duration <= 0) return far_plane;
    if (duration >= 20) return 5.0f;
    return 5.0f + (far_plane - 5.0f)
        * (1.0f - (float)duration / 20.0f);
}

/* EntityRenderer.updateFogColor's void/Blindness darkening. This runs after
 * fogColor1 and before boss tint / Night Vision. Keep the double multiply and
 * final float casts: the Java implementation deliberately crosses precision
 * at those points. */
static inline void gm_void_blindness_rgb(
        float *r, float *g, float *b, int blindness_duration,
        double feet_y, double void_fog_y_factor) {
    double d1 = feet_y * void_fog_y_factor;
    if (blindness_duration > 0) {
        if (blindness_duration < 20)
            d1 *= (double)(1.0f - (float)blindness_duration / 20.0f);
        else
            d1 = 0.0;
    }
    if (d1 < 1.0) {
        if (d1 < 0.0) d1 = 0.0;
        d1 *= d1;
        *r = (float)((double)*r * d1);
        *g = (float)((double)*g * d1);
        *b = (float)((double)*b * d1);
    }
}

#endif /* MAGMA_GAME_POTION_RENDER_H */
