#include "game/player_movement_audio.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    printf("\"%08x\"", bits);
}

static void print_double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    printf("\"%016llx\"", (unsigned long long)bits);
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char *end = NULL;
    unsigned long long seed = strtoull(argv[1], &end, 10);
    if (!end || *end || seed >= (1ULL << 48)) return 2;
    JavaRandom random = {(uint64_t)seed};
    float volume = gm_player_movement_audio_volume(
        GM_PLAYER_MOVEMENT_AUDIO_SPLASH, 0.125, -0.25, 0.375);
    float pitch = gm_player_movement_audio_pitch(
        GM_PLAYER_MOVEMENT_AUDIO_SPLASH, &random);
    int particles = gm_player_splash_particles(
        &random, 0.0, 220.0, 0.0, 0.6F,
        0.125, -0.25, 0.375, NULL, 0);

    printf("{\"ok\":true,\"in_water\":true,\"fire\":0,"
           "\"fall_distance_bits\":\"00000000\",\"motion_bits\":[");
    print_double_bits(0.125); putchar(',');
    print_double_bits(-0.25); putchar(',');
    print_double_bits(0.375);
    printf("],\"sound\":\"minecraft:entity.hostile.splash\","
           "\"category\":\"hostile\",\"sound_position_bits\":["
           "\"0000000000000000\",\"0000000000000000\","
           "\"0000000000000000\"],\"volume_bits\":");
    print_float_bits(volume);
    printf(",\"pitch_bits\":");
    print_float_bits(pitch);
    printf(",\"particle_count\":%d,\"entity_seed48\":%llu}\n",
        particles, (unsigned long long)random.seed);
    return 0;
}
