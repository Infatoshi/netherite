#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *sound_name(int sound) {
    switch (sound) {
    case GM_SOUND_FIREWORK_BLAST:
        return "minecraft:entity.firework.blast";
    case GM_SOUND_FIREWORK_BLAST_FAR:
        return "minecraft:entity.firework.blast_far";
    case GM_SOUND_FIREWORK_LARGE_BLAST:
        return "minecraft:entity.firework.large_blast";
    case GM_SOUND_FIREWORK_LARGE_BLAST_FAR:
        return "minecraft:entity.firework.large_blast_far";
    case GM_SOUND_FIREWORK_TWINKLE:
        return "minecraft:entity.firework.twinkle";
    case GM_SOUND_FIREWORK_TWINKLE_FAR:
        return "minecraft:entity.firework.twinkle_far";
    default:
        return "";
    }
}

static unsigned float_bits(float value) {
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

int main(int argc, char **argv) {
    GmConfig config;
    GmRuntime runtime;
    char err[256];
    int count, large, flicker, max_age;
    double distance;
    uint64_t blast_seed, twinkle_seed;
    if (argc != 7) return 2;
    count = atoi(argv[1]);
    large = atoi(argv[2]);
    flicker = atoi(argv[3]);
    distance = strtod(argv[4], NULL);
    blast_seed = strtoull(argv[5], NULL, 10);
    twinkle_seed = strtoull(argv[6], NULL, 10);
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, err, sizeof err)) return 3;
    gm_runtime_set_pose(&runtime, 8.5, 30.0, 8.5, 0.0F, 0.0F);
    if (!gm_runtime_firework_audio_fixture(
            &runtime, 7001, 8.5 + distance, 30.0, 8.5,
            count, large, flicker, blast_seed, twinkle_seed))
        return 4;
    max_age = flicker ? count * 2 + 14 : count * 2 - 1;
    for (int tick = 0; tick < (flicker ? max_age : 0); ++tick)
        gm_runtime_tick_fireworks(&runtime);
    printf("{\"explosion_count\":%d,\"large\":%s,"
           "\"flicker\":%s,\"max_age\":%d,\"events\":[",
           count, large || count >= 3 ? "true" : "false",
           flicker ? "true" : "false", max_age);
    for (int i = 0; i < gm_runtime_sound_event_count(&runtime); ++i) {
        GmRuntimeSoundEvent event;
        if (!gm_runtime_sound_event_get(&runtime, i, &event)) return 5;
        if (i) putchar(',');
        printf("{\"tick\":%d,\"sound\":\"%s\","
               "\"category\":\"ambient\",\"volume\":%.9g,"
               "\"pitch_bits\":\"%08x\","
               "\"distance_delay\":true,\"delay_ticks\":%d}",
               i == 0 ? 0 : max_age, sound_name(event.sound),
               (double)event.volume, float_bits(event.pitch),
               event.delay_ticks);
    }
    puts("]}");
    gm_runtime_destroy(&runtime);
    return 0;
}
