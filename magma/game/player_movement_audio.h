#ifndef MAGMA_GAME_PLAYER_MOVEMENT_AUDIO_H
#define MAGMA_GAME_PLAYER_MOVEMENT_AUDIO_H

#include "mc_rng.h"

enum {
    GM_PLAYER_MOVEMENT_AUDIO_SWIM = 1,
    GM_PLAYER_MOVEMENT_AUDIO_SPLASH = 2
};

enum {
    GM_PLAYER_SPLASH_PARTICLE_BUBBLE = 4,
    GM_PLAYER_SPLASH_PARTICLE_SPLASH = 5,
    GM_PLAYER_SPLASH_PARTICLE_CAP = 32
};

typedef struct {
    int kind;
    double x, y, z;
    double motion_x, motion_y, motion_z;
} GmPlayerSplashParticle;

/* Entity.move swim and Entity.resetHeight splash scalar math. */
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
    (void)kind;
    float pitch = 1.0F
        + (jrand_float(random) - jrand_float(random)) * 0.4F;
    return pitch;
}

/* Entity.resetHeight's exact World.spawnParticle argument stream. Particle
 * constructors own separate client-only entropy; this function covers the
 * entity Random cursor, spawn order, positions, and supplied velocities. */
static inline int gm_player_splash_particles(
        JavaRandom *random, double x, double bb_min_y, double z, float width,
        double motion_x, double motion_y, double motion_z,
        GmPlayerSplashParticle *out, int cap) {
    int count = 0;
    float y = (float)floor(bb_min_y) + 1.0F;
    float limit = 1.0F + width * 20.0F;
    for (int i = 0; (float)i < limit; ++i) {
        float dx = (jrand_float(random) * 2.0F - 1.0F) * width;
        float dz = (jrand_float(random) * 2.0F - 1.0F) * width;
        float down = jrand_float(random) * 0.2F;
        if (out && count < cap) {
            out[count] = (GmPlayerSplashParticle) {
                GM_PLAYER_SPLASH_PARTICLE_BUBBLE,
                x + (double)dx, (double)y, z + (double)dz,
                motion_x, motion_y - (double)down, motion_z
            };
        }
        ++count;
    }
    for (int i = 0; (float)i < limit; ++i) {
        float dx = (jrand_float(random) * 2.0F - 1.0F) * width;
        float dz = (jrand_float(random) * 2.0F - 1.0F) * width;
        if (out && count < cap) {
            out[count] = (GmPlayerSplashParticle) {
                GM_PLAYER_SPLASH_PARTICLE_SPLASH,
                x + (double)dx, (double)y, z + (double)dz,
                motion_x, motion_y, motion_z
            };
        }
        ++count;
    }
    return count;
}

#endif
