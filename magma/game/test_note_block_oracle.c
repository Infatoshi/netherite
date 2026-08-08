#include "game/runtime.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_case(const char *text, int *out) {
    char *end = NULL;
    long value;
    errno = 0;
    value = strtol(text, &end, 0);
    if (errno || !text[0] || !end || *end || value < 0 || value > 11)
        return 0;
    *out = (int)value;
    return 1;
}

static unsigned float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (unsigned)bits;
}

static unsigned long long double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (unsigned long long)bits;
}

int main(int argc, char **argv) {
    static const char *const sound_names[5] = {
        "minecraft:block.note.harp", "minecraft:block.note.basedrum",
        "minecraft:block.note.snare", "minecraft:block.note.hat",
        "minecraft:block.note.bass"
    };
    const uint64_t world_seed = UINT64_C(0x123456789ABC);
    const uint64_t math_seed = UINT64_C(0x0FEDCBA98765);
    const int x = 12, y = 220, z = 8;
    int fixture;
    GmConfig config;
    GmRuntime runtime;
    char err[256];
    if (argc != 2 || !parse_case(argv[1], &fixture)) return 2;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    config.render = GM_RENDER_OFF;
    if (!gm_runtime_init(&runtime, &config, err, sizeof err)) return 3;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dz = -2; dz <= 2; ++dz)
            for (int dx = -2; dx <= 2; ++dx)
                gm_world_set_block_meta(
                    runtime.world, x + dx, y + dy, z + dz, 0, 0);
    int below = fixture == 4 || fixture == 9 ? 5 : fixture == 6 ? 12
        : fixture == 7 ? 20 : fixture == 8 || fixture == 11 ? 42 : 1;
    int note = fixture == 4 || fixture == 9 ? 24 : fixture == 6 ? 12
        : fixture == 7 ? 6 : fixture == 8 || fixture == 11 ? 18
        : fixture == 10 ? 11 : 0;
    int previous_powered = fixture == 2 || fixture == 3;
    int powered = fixture == 1 || fixture == 2
        || (fixture >= 4 && fixture <= 8);
    gm_world_set_block_meta(runtime.world, x, y - 1, z, below, 0);
    gm_world_set_block_meta(runtime.world, x, y, z, 25, 0);
    if (powered)
        gm_world_set_block_meta(runtime.world, x + 1, y, z, 152, 0);
    if (fixture == 5 || fixture == 10)
        gm_world_set_block_meta(runtime.world, x, y + 1, z, 1, 0);
    if (!gm_runtime_note_block_set(
            &runtime, runtime.dimension, x, y, z,
            note, previous_powered)
            || !gm_runtime_set_world_random_seed48(&runtime, world_seed)
            || !gm_runtime_set_math_random_seed48(&runtime, math_seed)
            || !gm_runtime_set_block(
                &runtime, x + 1, y, z, powered ? 152 : 0, 0)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (fixture == 9 || fixture == 10) {
        gm_runtime_set_pose(&runtime, x + 0.5, y, z - 1.5, 0.0F, 0.0F);
        if (!gm_runtime_use_block(&runtime, x, y, z)) {
            gm_runtime_destroy(&runtime);
            return 5;
        }
    } else if (fixture == 11
            && !gm_runtime_note_block_play(&runtime, x, y, z)) {
        gm_runtime_destroy(&runtime);
        return 5;
    }
    GmRuntimeNoteBlock tile;
    if (!gm_runtime_note_block_get(&runtime, 0, &tile)) {
        gm_runtime_destroy(&runtime);
        return 6;
    }
    printf("{\"ok\":true,\"case\":%d,\"note\":%d,"
           "\"powered\":%s,\"sounds\":[",
           fixture, tile.note, tile.powered ? "true" : "false");
    int sound_count = gm_runtime_sound_event_count(&runtime);
    for (int i = 0; i < sound_count; ++i) {
        GmRuntimeSoundEvent event;
        if (!gm_runtime_sound_event_get(&runtime, i, &event)) return 7;
        int instrument = event.sound - GM_SOUND_NOTE_HARP;
        if (instrument < 0 || instrument >= 5) return 8;
        if (i) putchar(',');
        printf("{\"seq\":%d,\"sound\":\"%s\","
               "\"category\":\"record\","
               "\"x_bits\":\"%016llx\",\"y_bits\":\"%016llx\","
               "\"z_bits\":\"%016llx\",\"volume_bits\":\"%08x\","
               "\"pitch_bits\":\"%08x\"}",
               i, sound_names[instrument],
               double_bits(event.x - x), double_bits(event.y - y),
               double_bits(event.z - z), float_bits(event.volume),
               float_bits(event.pitch));
    }
    printf("],\"particles\":[");
    int particle_count = gm_runtime_particle_event_count(&runtime);
    for (int i = 0; i < particle_count; ++i) {
        GmRuntimeParticleEvent event;
        if (!gm_runtime_particle_event_get(&runtime, i, &event)) return 9;
        if (i) putchar(',');
        printf("{\"seq\":%d,\"id\":%d,\"ignore_range\":false,"
               "\"payload_bits\":[\"%016llx\",\"%016llx\","
               "\"%016llx\",\"%016llx\",\"%016llx\","
               "\"%016llx\"],\"parameters\":[]}",
               i, event.kind,
               double_bits(event.x - x), double_bits(event.y - y),
               double_bits(event.z - z), double_bits(event.motion_x),
               double_bits(event.motion_y), double_bits(event.motion_z));
    }
    printf("],\"world_seed48\":%llu,\"math_seed48\":%llu}\n",
           (unsigned long long)runtime.world_random_seed48,
           (unsigned long long)runtime.math_random_seed48);
    gm_runtime_destroy(&runtime);
    return 0;
}
