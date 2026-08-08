#ifndef MAGMA_GAME_PLAYER_MOVEMENT_AUDIO_H
#define MAGMA_GAME_PLAYER_MOVEMENT_AUDIO_H

#include "mc_rng.h"

enum {
    GM_PLAYER_MOVEMENT_AUDIO_SWIM = 1,
    GM_PLAYER_MOVEMENT_AUDIO_SPLASH = 2
};

/* Entity.move swim and Entity.resetHeight splash scalar math. The splash
 * particles are not rendered yet, but their 65 nextFloat calls must still
 * advance Entity.rand before the next observable sound. */
static inline float gm_player_movement_audio_volume(
        int kind, double motion_x, double motion_y, double motion_z) {
    float scale = kind == GM_PLAYER_MOVEMENT_AUDIO_SPLASH ? 0.2F : 0.35F;
    float volume = (float)sqrt(
        motion_x * motion_x * 0.20000000298023224
        + motion_y * motion_y
        + motion_z * motion_z * 0.20000000298023224) * scale;
    return volume > 1.0F ? 1.0F : volume;
}

static inline float gm_player_movement_audio_pitch(
        int kind, JavaRandom *random) {
    float pitch = 1.0F
        + (jrand_float(random) - jrand_float(random)) * 0.4F;
    if (kind == GM_PLAYER_MOVEMENT_AUDIO_SPLASH)
        for (int i = 0; i < 65; ++i)
            (void)jrand_float(random);
    return pitch;
}

#endif
