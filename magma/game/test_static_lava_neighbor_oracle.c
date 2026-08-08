#include "game/runtime.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int parse_case(const char *text, int *out) {
    char *end = NULL;
    long value;
    errno = 0;
    value = strtol(text, &end, 0);
    if (errno || !text[0] || !end || *end || value < 0 || value > 57)
        return 0;
    *out = (int)value;
    return 1;
}

static int parse_coord(const char *text, int *out) {
    char *end = NULL;
    long value;
    errno = 0;
    value = strtol(text, &end, 0);
    if (errno || !text[0] || !end || *end
            || value < -30000000L || value > 30000000L)
        return 0;
    *out = (int)value;
    return 1;
}

static void print_double_bits(double value) {
    union { double d; uint64_t u; } bits = {value};
    printf("\"%016llx\"", (unsigned long long)bits.u);
}

static void print_float_bits(float value) {
    union { float f; uint32_t u; } bits = {value};
    printf("\"%08x\"", bits.u);
}

static void print_blocks(const GmRuntime *runtime, int x, int y, int z) {
    int first = 1;
    putchar('[');
    for (int dy = -2; dy <= 2; ++dy)
        for (int dz = -2; dz <= 2; ++dz)
            for (int dx = -2; dx <= 2; ++dx) {
                int state = gm_world_block(
                        runtime->world, x + dx, y + dy, z + dz) << 4
                    | (gm_world_meta(
                        runtime->world, x + dx, y + dy, z + dz) & 15);
                if (!first) putchar(',');
                printf("%d", state);
                first = 0;
            }
    putchar(']');
}

static void print_scheduled(
        const GmRuntime *runtime, int x, int y, int z) {
    int count = gm_runtime_scheduled_tick_count(runtime);
    putchar('[');
    for (int index = 0; index < count; ++index) {
        GmRuntimeScheduledTick entry;
        if (!gm_runtime_scheduled_tick_get(runtime, index, &entry)) exit(5);
        if (index) putchar(',');
        printf("[%d,%d,%d,%d,%lld,%d,%d]",
               entry.x - x, entry.y - y, entry.z - z, entry.block,
               entry.time - runtime->clock.total_time, entry.priority, index);
    }
    putchar(']');
}

static void print_sounds(const GmRuntime *runtime, int x, int y, int z) {
    int count = gm_runtime_sound_event_count(runtime);
    putchar('[');
    for (int index = 0; index < count; ++index) {
        GmRuntimeSoundEvent event;
        if (!gm_runtime_sound_event_get(runtime, index, &event)) exit(5);
        if (index) putchar(',');
        printf("{\"seq\":%d,\"sound\":\"%s\",\"category\":\"%s\","
               "\"x_bits\":", index,
               event.sound == GM_SOUND_LAVA_EXTINGUISH
                   ? "minecraft:block.lava.extinguish" : "unknown",
               event.category == GM_SOUND_CATEGORY_BLOCKS ? "block" : "unknown");
        print_double_bits(event.x - x);
        fputs(",\"y_bits\":", stdout);
        print_double_bits(event.y - y);
        fputs(",\"z_bits\":", stdout);
        print_double_bits(event.z - z);
        fputs(",\"volume_bits\":", stdout);
        print_float_bits(event.volume);
        fputs(",\"pitch_bits\":", stdout);
        print_float_bits(event.pitch);
        putchar('}');
    }
    putchar(']');
}

static void print_particles(const GmRuntime *runtime, int x, int y, int z) {
    int count = gm_runtime_particle_event_count(runtime);
    putchar('[');
    for (int index = 0; index < count; ++index) {
        GmRuntimeParticleEvent event;
        if (!gm_runtime_particle_event_get(runtime, index, &event)) exit(5);
        if (index) putchar(',');
        printf("{\"seq\":%d,\"id\":%d,\"ignore_range\":false,"
               "\"parameters\":[],\"payload_bits\":[",
               index, event.kind);
        print_double_bits(event.x - x);
        putchar(','); print_double_bits(event.y - y);
        putchar(','); print_double_bits(event.z - z);
        putchar(','); print_double_bits(event.motion_x);
        putchar(','); print_double_bits(event.motion_y);
        putchar(','); print_double_bits(event.motion_z);
        fputs("]}", stdout);
    }
    putchar(']');
}

int main(int argc, char **argv) {
    static const int levels[58] = {
        0, 4, 5, 15, 0, 1, 4, 5, 15, 0, 0, 4, 2, 0, 0, 0, 0,
        0, 0, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    const int y = 220;
    int fixture, x = 12, z = 8, dimension = 0, dynamic;
    GmConfig config;
    GmRuntime runtime;
    char err[256];
    if ((argc != 2 && argc != 4 && argc != 5)
            || !parse_case(argv[1], &fixture))
        return 2;
    if (argc == 2)
        dimension = fixture == 13 || fixture == 15 || fixture == 17
                || fixture == 19 || fixture == 21 || fixture == 22
                || fixture == 24 || fixture == 26 || fixture == 28
                || fixture == 30 || fixture >= 32 ? -1
            : fixture == 14 || fixture == 16 || fixture == 18
                || fixture == 20 || fixture == 23 || fixture == 25
                || fixture == 27 || fixture == 29 || fixture == 31 ? 1 : 0;
    if (argc >= 4
            && (!parse_coord(argv[2], &x) || !parse_coord(argv[3], &z)))
        return 2;
    if (argc == 5
            && (!parse_coord(argv[4], &dimension)
                || dimension < -1 || dimension > 1))
        return 2;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 2;
    config.mobs = 0;
    config.weather = 0;
    config.render = GM_RENDER_OFF;
    if (!gm_runtime_init(&runtime, &config, err, sizeof err)) return 3;
    if (!gm_runtime_set_dimension(&runtime, dimension)) return 3;
    dynamic = fixture >= 15;
    gm_world_ensure(runtime.world, x >> 4, z >> 4, 1);
    if (!gm_runtime_set_world_random_seed48(
                &runtime, UINT64_C(0x23456789ABCD))
            || !gm_runtime_set_math_random_seed48(
                &runtime, UINT64_C(0x3456789ABCDE))
            || !gm_runtime_set_block_random_seed48(
                &runtime, UINT64_C(0x123456789ABC))
            || !gm_runtime_set_entity_id_cursor(&runtime, 761000)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (dynamic) {
        for (int dz = -5; dz <= 5; ++dz)
            for (int dx = -5; dx <= 5; ++dx)
                if (abs(dx) + abs(dz) <= 5
                        && !((fixture == 17 || fixture == 18)
                            && dx == 0 && dz == -4)
                        && !((fixture == 22 || fixture == 23
                                || fixture == 28 || fixture == 29)
                            && dx == 0 && dz == 0)
                        && !gm_runtime_load_block(
                            &runtime, x + dx, y - 1, z + dz, 1, 0))
                    return 4;
        if (!gm_runtime_load_block(
                &runtime, x, y, z, 10, levels[fixture]))
            return 4;
        if (fixture == 19 || fixture == 20) {
            if (!gm_runtime_load_block(&runtime, x, y, z - 1, 10, 2)
                    || !gm_runtime_load_block(&runtime, x, y, z + 1, 1, 0)
                    || !gm_runtime_load_block(&runtime, x - 1, y, z, 1, 0)
                    || !gm_runtime_load_block(&runtime, x + 1, y, z, 1, 0))
                return 4;
        } else if (fixture == 21) {
            if (!gm_runtime_load_block(&runtime, x, y, z - 1, 11, 0)
                    || !gm_runtime_load_block(&runtime, x, y, z + 1, 11, 0)
                    || !gm_runtime_load_block(&runtime, x - 1, y, z, 1, 0)
                    || !gm_runtime_load_block(&runtime, x + 1, y, z, 1, 0))
                return 4;
        } else if (fixture == 24 || fixture == 25) {
            if (!gm_runtime_load_block(&runtime, x, y, z - 1, 51, 0)
                    || !gm_runtime_load_block(&runtime, x, y, z + 1, 1, 0)
                    || !gm_runtime_load_block(&runtime, x - 1, y, z, 1, 0)
                    || !gm_runtime_load_block(&runtime, x + 1, y, z, 1, 0))
                return 4;
        } else if (fixture == 26 || fixture == 27
                || fixture == 30 || fixture == 31) {
            int target = fixture >= 30 ? 8 : 9;
            int target_meta = fixture >= 30 ? 4 : 0;
            if (!gm_runtime_load_block(
                        &runtime, x, y, z - 1, target, target_meta)
                    || !gm_runtime_load_block(&runtime, x, y, z + 1, 1, 0)
                    || !gm_runtime_load_block(&runtime, x - 1, y, z, 1, 0)
                    || !gm_runtime_load_block(&runtime, x + 1, y, z, 1, 0))
                return 4;
        } else if (fixture == 28 || fixture == 29) {
            if (!gm_runtime_load_block(&runtime, x, y - 1, z, 9, 0)
                    || !gm_runtime_load_block(&runtime, x, y - 2, z, 1, 0))
                return 4;
        } else if (fixture >= 32) {
            static const int targets[26] = {
                6, 30, 50, 78, 106, 171, 65, 66,
                27, 55, 31, 59, 32, 39, 76, 83,
                37, 38, 40, 104, 105, 111, 115, 141, 142, 207
            };
            static const int metas[26] = {
                0, 0, 5, 0, 4, 0, 3, 0,
                0, 0, 1, 3, 0, 0, 5, 0,
                0, 0, 0, 4, 6, 0, 2, 5, 7, 2
            };
            static const int supports[26] = {
                3, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 2, 60, 12, 110, 1, 12,
                2, 2, 110, 60, 60, 9, 88, 60, 60, 60
            };
            int index = fixture - 32;
            if (!gm_runtime_load_block(
                        &runtime, x, y, z - 1,
                        targets[index], metas[index])
                    || !gm_runtime_load_block(&runtime, x, y, z + 1, 1, 0)
                    || !gm_runtime_load_block(&runtime, x - 1, y, z, 1, 0)
                    || !gm_runtime_load_block(&runtime, x + 1, y, z, 1, 0)
                    || (supports[index] != 1 && !gm_runtime_load_block(
                        &runtime, x, y - 1, z - 1,
                        supports[index], 0))
                    || ((fixture == 36 || fixture == 38)
                        && !gm_runtime_load_block(
                            &runtime, x, y, z - 2, 1, 0))
                    || (fixture == 47 && !gm_runtime_load_block(
                        &runtime, x, y - 1, z - 2, 9, 0)))
                return 4;
        }
        if (!gm_runtime_schedule_tick(
                    &runtime, x, y, z, 10,
                    runtime.clock.total_time + 1, 0, 0))
            return 4;
    } else if (!gm_runtime_load_block(
            &runtime, x, y, z, 11, levels[fixture])) {
        return 4;
    }
    if (fixture == 4 || fixture == 5 || fixture == 7) {
        if (!gm_runtime_load_block(&runtime, x, y + 1, z, 9, 0)) return 4;
    } else if (fixture == 8) {
        if (!gm_runtime_load_block(&runtime, x, y + 1, z, 8, 0)) return 4;
    } else if (fixture == 6) {
        if (!gm_runtime_load_block(&runtime, x, y, z - 1, 8, 0)) return 4;
    } else if (fixture == 9) {
        if (!gm_runtime_load_block(&runtime, x, y - 1, z, 9, 0)) return 4;
    } else if (fixture == 10) {
        if (!gm_runtime_load_block(&runtime, x, y, z - 1, 9, 0)) return 4;
    } else if (fixture == 11) {
        if (!gm_runtime_load_block(&runtime, x - 1, y, z, 8, 0)) return 4;
    } else if (fixture == 12) {
        if (!gm_runtime_load_block(&runtime, x, y + 1, z, 9, 0)
                || !gm_runtime_load_block(&runtime, x, y, z - 1, 9, 0))
            return 4;
    }
    printf("{\"ok\":true,\"case\":%d,\"origin_x\":%d,"
           "\"origin_y\":%d,\"origin_z\":%d,\"dimension\":%d,"
           "\"before_scheduled\":", fixture, x, y, z, dimension);
    print_scheduled(&runtime, x, y, z);
    fputs(",\"before_blocks\":", stdout);
    print_blocks(&runtime, x, y, z);
    if (dynamic) {
        GmAction idle = {0};
        idle.hotbar_sel = -1;
        gm_runtime_tick(&runtime, idle);
    } else if (!gm_runtime_set_block(
            &runtime, x + 1, y, z, 1, 0)) {
        return 4;
    }
    fputs(",\"after_scheduled\":", stdout);
    print_scheduled(&runtime, x, y, z);
    fputs(",\"after_blocks\":", stdout);
    print_blocks(&runtime, x, y, z);
    fputs(",\"sounds\":", stdout);
    print_sounds(&runtime, x, y, z);
    fputs(",\"particles\":", stdout);
    print_particles(&runtime, x, y, z);
    printf(",\"world_seed48\":%llu,\"math_seed48\":%llu,"
           "\"block_seed48\":%llu,\"next_entity_id\":%d}\n",
           (unsigned long long)runtime.world_random_seed48,
           (unsigned long long)runtime.math_random_seed48,
           (unsigned long long)runtime.block_random_seed48,
           runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return 0;
}
