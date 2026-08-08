#include "game/runtime.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int parse_case(const char *text, int *out) {
    char *end = NULL;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || !text[0] || !end || *end || value < 0 || value > 7)
        return 0;
    *out = (int)value;
    return 1;
}

static void put(GmRuntime *r, int x, int y, int z, int id, int meta) {
    gm_world_set_block_meta(r->world, x, y, z, id, meta);
}

int main(int argc, char **argv) {
    const int x = 12, y = 78, z = 8;
    const uint64_t world_seed = UINT64_C(0x23456789ABCD);
    const uint64_t math_seed = UINT64_C(0x3456789ABCDE);
    const int next_entity_id = 760000;
    GmConfig config;
    GmRuntime runtime;
    int fixture, edit_x = x, edit_y = y, edit_z = z;
    char err[256];
    if (argc != 2 || !parse_case(argv[1], &fixture))
        return 2;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    config.render = GM_RENDER_OFF;
    if (!gm_runtime_init(&runtime, &config, err, sizeof err))
        return 3;
    for (int dy = 0; dy <= 3; ++dy)
        for (int dz = -2; dz <= 2; ++dz)
            for (int dx = -2; dx <= 2; ++dx)
                put(&runtime, x + dx, y + dy, z + dz, 0, 0);
    switch (fixture) {
    case 0:
        put(&runtime, x, y, z, 106, 4);
        edit_z = z - 1;
        put(&runtime, edit_x, edit_y, edit_z, 1, 0);
        break;
    case 1:
        put(&runtime, x, y, z, 106, 5);
        edit_z = z - 1;
        put(&runtime, edit_x, edit_y, edit_z, 1, 0);
        put(&runtime, x, y, z + 1, 1, 0);
        break;
    case 2:
        put(&runtime, x, y, z, 106, 4);
        put(&runtime, x, y + 1, z, 106, 4);
        edit_z = z - 1;
        put(&runtime, edit_x, edit_y, edit_z, 1, 0);
        put(&runtime, x, y + 1, z - 1, 1, 0);
        break;
    case 3:
        put(&runtime, x, y, z, 106, 4);
        put(&runtime, x, y + 1, z, 106, 4);
        edit_y = y + 1;
        edit_z = z - 1;
        put(&runtime, edit_x, edit_y, edit_z, 1, 0);
        break;
    case 4:
        put(&runtime, x, y, z, 106, 15);
        put(&runtime, x, y + 1, z, 106, 12);
        put(&runtime, x, y, z + 1, 1, 0);
        edit_x = x + 1;
        put(&runtime, edit_x, edit_y, edit_z, 1, 0);
        break;
    case 5:
        put(&runtime, x, y, z, 106, 0);
        edit_y = y + 1;
        put(&runtime, edit_x, edit_y, edit_z, 1, 0);
        break;
    case 6:
        put(&runtime, x, y, z, 106, 4);
        put(&runtime, x, y + 1, z, 106, 1);
        put(&runtime, x, y + 1, z + 1, 1, 0);
        edit_z = z - 1;
        put(&runtime, edit_x, edit_y, edit_z, 1, 0);
        break;
    default:
        put(&runtime, x, y, z, 106, 4);
        put(&runtime, x, y + 1, z, 106, 4);
        put(&runtime, x, y + 2, z, 106, 4);
        edit_y = y + 2;
        edit_z = z - 1;
        put(&runtime, edit_x, edit_y, edit_z, 1, 0);
        break;
    }
    if (!gm_runtime_set_world_random_seed48(&runtime, world_seed)
            || !gm_runtime_set_math_random_seed48(&runtime, math_seed)
            || !gm_runtime_set_entity_id_cursor(&runtime, next_entity_id)
            || !gm_runtime_set_block(
                &runtime, edit_x, edit_y, edit_z, 0, 0)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    printf("{\"ok\":true,\"case\":%d,\"blocks\":[", fixture);
    int first = 1;
    for (int dy = 0; dy <= 3; ++dy)
        for (int dz = -2; dz <= 2; ++dz)
            for (int dx = -2; dx <= 2; ++dx) {
                int state = gm_world_block(
                        runtime.world, x + dx, y + dy, z + dz) << 4
                    | (gm_world_meta(
                        runtime.world, x + dx, y + dy, z + dz) & 15);
                if (!first) putchar(',');
                printf("%d", state);
                first = 0;
            }
    printf("],\"world_seed48\":%llu,\"math_seed48\":%llu,"
           "\"next_entity_id\":%d}\n",
           (unsigned long long)runtime.world_random_seed48,
           (unsigned long long)runtime.math_random_seed48,
           runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return 0;
}
