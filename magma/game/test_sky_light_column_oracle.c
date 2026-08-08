#include "game/runtime.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

enum { X_RADIUS = 5, Z_RADIUS = 5, Y_MIN = -8, Y_MAX = 3 };

static int parse_case(const char *text, int *out) {
    char *end = NULL;
    long value;
    errno = 0;
    value = strtol(text, &end, 0);
    if (errno || !text[0] || !end || *end || value < 0 || value > 12)
        return 0;
    *out = (int)value;
    return 1;
}

static int edit(GmRuntime *runtime, int x, int y, int z, int id) {
    return gm_runtime_set_block(runtime, x, y, z, id, 0);
}

static void print_blocks(const GmRuntime *runtime, int x, int y, int z) {
    int first = 1;
    putchar('[');
    for (int dy = Y_MIN; dy <= Y_MAX; ++dy)
        for (int dz = -Z_RADIUS; dz <= Z_RADIUS; ++dz)
            for (int dx = -X_RADIUS; dx <= X_RADIUS; ++dx) {
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

static void print_sky(const GmRuntime *runtime, int x, int y, int z) {
    int first = 1;
    putchar('[');
    for (int dy = Y_MIN; dy <= Y_MAX; ++dy)
        for (int dz = -Z_RADIUS; dz <= Z_RADIUS; ++dz)
            for (int dx = -X_RADIUS; dx <= X_RADIUS; ++dx) {
                if (!first) putchar(',');
                printf("%d", gm_world_sky_light(
                    runtime->world, x + dx, y + dy, z + dz));
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

int main(int argc, char **argv) {
    const int y = 220, z = 8;
    int fixture, x = 12;
    GmConfig config;
    GmRuntime runtime;
    char err[256];
    if (argc != 2 || !parse_case(argv[1], &fixture)) return 2;
    if (fixture == 9 || fixture == 10) x = 15;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 2;
    config.mobs = 0;
    config.weather = 0;
    config.render = GM_RENDER_OFF;
    if (!gm_runtime_init(&runtime, &config, err, sizeof err)) return 3;
    if (!gm_runtime_set_world_random_seed48(
                &runtime, UINT64_C(0x23456789ABCD))
            || !gm_runtime_set_math_random_seed48(
                &runtime, UINT64_C(0x3456789ABCDE))
            || !gm_runtime_set_block_random_seed48(
                &runtime, UINT64_C(0x123456789ABC))
            || !gm_runtime_set_entity_id_cursor(&runtime, 760000)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    switch (fixture) {
    case 1: case 3: case 5:
        if (!edit(&runtime, x, y, z,
                  fixture == 1 ? 1 : fixture == 3 ? 18 : 9)) return 4;
        break;
    case 7:
        if (!edit(&runtime, x, y, z, 9)) return 4;
        break;
    case 8:
        if (!edit(&runtime, x, y, z, 9)
                || !edit(&runtime, x, y - 1, z, 9)) return 4;
        break;
    case 11:
        if (!edit(&runtime, x, y, z, 1)) return 4;
        break;
    case 12:
        if (!edit(&runtime, x, y, z, 9)) return 4;
        break;
    default:
        break;
    }
    printf("{\"ok\":true,\"case\":%d,\"before_scheduled\":", fixture);
    print_scheduled(&runtime, x, y, z);
    fputs(",\"before_blocks\":", stdout);
    print_blocks(&runtime, x, y, z);
    fputs(",\"before_sky\":", stdout);
    print_sky(&runtime, x, y, z);
    switch (fixture) {
    case 0: case 9:
        if (!edit(&runtime, x, y, z, 1)) return 4;
        break;
    case 1: case 3: case 5:
        if (!edit(&runtime, x, y, z, 0)) return 4;
        break;
    case 2:
        if (!edit(&runtime, x, y, z, 18)) return 4;
        break;
    case 4: case 10:
        if (!edit(&runtime, x, y, z, 9)) return 4;
        break;
    case 6:
        if (!edit(&runtime, x, y, z, 20)) return 4;
        break;
    case 7: case 11: case 12:
        if (!edit(&runtime, x, y - 1, z,
                  fixture == 7 ? 9 : 18)) return 4;
        break;
    default:
        if (!edit(&runtime, x, y - 1, z, 0)) return 4;
        break;
    }
    fputs(",\"after_scheduled\":", stdout);
    print_scheduled(&runtime, x, y, z);
    fputs(",\"after_blocks\":", stdout);
    print_blocks(&runtime, x, y, z);
    fputs(",\"after_sky\":", stdout);
    print_sky(&runtime, x, y, z);
    printf(",\"world_seed48\":%llu,\"math_seed48\":%llu,"
           "\"block_seed48\":%llu,\"next_entity_id\":%d}\n",
           (unsigned long long)runtime.world_random_seed48,
           (unsigned long long)runtime.math_random_seed48,
           (unsigned long long)runtime.block_random_seed48,
           runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return 0;
}
