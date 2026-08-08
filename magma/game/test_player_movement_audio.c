#include "game/player_movement_audio.h"

#include <stdint.h>
#include <stdio.h>

static uint32_t float_bits(float value) {
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

static uint64_t double_bits(double value) {
    union { double d; uint64_t u; } bits;
    bits.d = value;
    return bits.u;
}

static void print_particles(const GmPlayerSplashParticle *particles, int count) {
    for (int i = 0; i < count; ++i) {
        const GmPlayerSplashParticle *p = &particles[i];
        printf("P %02d %d %016llx %016llx %016llx %016llx %016llx %016llx\n",
               i, p->kind,
               (unsigned long long)double_bits(p->x),
               (unsigned long long)double_bits(p->y),
               (unsigned long long)double_bits(p->z),
               (unsigned long long)double_bits(p->motion_x),
               (unsigned long long)double_bits(p->motion_y),
               (unsigned long long)double_bits(p->motion_z));
    }
}

int main(void) {
    JavaRandom random;
    float volume, pitch;

    jrand_set_seed48(&random, UINT64_C(0x123456789abc));
    volume = gm_player_movement_audio_volume(
        GM_PLAYER_MOVEMENT_AUDIO_SWIM,
        0.125, -0.0784000015258789, 0.75);
    pitch = gm_player_movement_audio_pitch(
        GM_PLAYER_MOVEMENT_AUDIO_SWIM, &random);
    printf("A %08x %08x %08x\n", float_bits(volume), float_bits(pitch),
           float_bits(jrand_float(&random)));

    jrand_set_seed48(&random, UINT64_C(0x0fedcba98765));
    volume = gm_player_movement_audio_volume(
        GM_PLAYER_MOVEMENT_AUDIO_SPLASH, 0.25, -0.5, 7.0);
    pitch = gm_player_movement_audio_pitch(
        GM_PLAYER_MOVEMENT_AUDIO_SPLASH, &random);
    GmPlayerSplashParticle particles[GM_PLAYER_SPLASH_PARTICLE_CAP];
    int particle_count = gm_player_splash_particles(
        &random, 8.5, 7.0, 8.5, 0.6F, 0.25, -0.5, 7.0,
        particles, GM_PLAYER_SPLASH_PARTICLE_CAP);
    print_particles(particles, particle_count);
    float swim_volume = gm_player_movement_audio_volume(
        GM_PLAYER_MOVEMENT_AUDIO_SWIM, 0.0, 0.0, 7.0);
    float swim_pitch = gm_player_movement_audio_pitch(
        GM_PLAYER_MOVEMENT_AUDIO_SWIM, &random);
    printf("B %08x %08x %08x %08x %08x\n",
           float_bits(volume), float_bits(pitch),
           float_bits(swim_volume), float_bits(swim_pitch),
           float_bits(jrand_float(&random)));

    jrand_set_seed48(&random, UINT64_C(1));
    volume = gm_player_movement_audio_volume(
        GM_PLAYER_MOVEMENT_AUDIO_SWIM, 100.0, -100.0, 100.0);
    pitch = gm_player_movement_audio_pitch(
        GM_PLAYER_MOVEMENT_AUDIO_SWIM, &random);
    printf("C %08x %08x %08x\n", float_bits(volume), float_bits(pitch),
           float_bits(jrand_float(&random)));
    return 0;
}
