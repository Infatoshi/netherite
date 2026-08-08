#include "game/runtime.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int(const char *text, int *out) {
    char *end = NULL;
    long value;
    errno = 0;
    value = strtol(text, &end, 0);
    if (errno || !text[0] || !end || *end)
        return 0;
    *out = (int)value;
    return (long)*out == value;
}

static int parse_seed(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value;
    errno = 0;
    value = strtoull(text, &end, 0);
    if (errno || !text[0] || !end || *end
            || value >= (UINT64_C(1) << 48))
        return 0;
    *out = (uint64_t)value;
    return 1;
}

static void put(
        GmRuntime *r, int x, int y, int z, int id, int meta) {
    gm_world_set_block_meta(r->world, x, y, z, id, meta);
}

static unsigned float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (unsigned)bits;
}

static void print_item(
        const GmLiveEnt *item, int origin_x, int origin_y, int origin_z) {
    printf("{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
           "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
           "\"item\":%d,\"count\":%d,\"meta\":%d,"
           "\"age\":%d,\"pickup_delay\":%d,\"health\":%d,"
           "\"lifespan\":%d,\"yaw_bits\":\"%08x\","
           "\"hover_start_bits\":\"%08x\","
           "\"on_ground\":%s,\"is_dead\":false}",
           item->eid,
           item->x - origin_x, item->y - origin_y,
           item->z - origin_z,
           item->mx, item->my, item->mz,
           item->item, item->count, item->meta, item->age,
           item->pickup_delay, item->health, item->lifespan,
           float_bits(item->yaw), float_bits(item->hover_start),
           item->on_ground ? "true" : "false");
}

int main(int argc, char **argv) {
    const int x = 12, y = 78, z = 8;
    const uint64_t math_seed = UINT64_C(0x3456789ABCDE);
    const uint64_t block_seed = UINT64_C(0x123456789ABC);
    const int next_entity_id = 760000;
    GmConfig config;
    GmRuntime runtime;
    uint64_t world_seed;
    int fixture;
    char err[256];
    if (argc != 3 || !parse_int(argv[1], &fixture)
            || fixture < 0 || fixture > 12
            || !parse_seed(argv[2], &world_seed))
        return 2;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    config.render = GM_RENDER_OFF;
    if (!gm_runtime_init(&runtime, &config, err, sizeof err))
        return 3;
    for (int dy = -5; dy <= 5; ++dy)
        for (int dz = -5; dz <= 5; ++dz)
            for (int dx = -5; dx <= 5; ++dx)
                put(&runtime, x + dx, y + dy, z + dz, 0, 0);
    switch (fixture) {
    case 0:
        put(&runtime, x, y, z, 18, 0);
        break;
    case 1:
        put(&runtime, x, y, z, 18, 12);
        break;
    case 2:
        put(&runtime, x, y, z, 18, 8);
        put(&runtime, x + 1, y, z, 18, 8);
        put(&runtime, x + 2, y, z, 161, 8);
        put(&runtime, x + 3, y, z, 18, 8);
        put(&runtime, x + 4, y, z, 17, 0);
        break;
    case 3:
        put(&runtime, x, y, z, 161, 9);
        put(&runtime, x + 1, y, z, 18, 8);
        put(&runtime, x + 2, y, z, 161, 8);
        put(&runtime, x + 3, y, z, 18, 8);
        put(&runtime, x + 4, y, z, 162, 0);
        break;
    case 4:
        put(&runtime, x, y, z, 18, 11);
        put(&runtime, x + 1, y, z, 18, 8);
        put(&runtime, x + 2, y, z, 161, 8);
        put(&runtime, x + 3, y, z, 18, 8);
        put(&runtime, x + 4, y, z, 161, 8);
        put(&runtime, x + 5, y, z, 17, 0);
        break;
    case 5:
        put(&runtime, x, y, z, 18, 11);
        put(&runtime, x + 1, y + 1, z, 17, 0);
        break;
    case 6:
        put(&runtime, x, y, z, 18, 8);
        break;
    case 7:
        put(&runtime, x, y, z, 18, 11);
        break;
    case 8:
        put(&runtime, x, y, z, 161, 8);
        break;
    case 9:
        put(&runtime, x, y, z, 161, 9);
        break;
    case 10:
        put(&runtime, x, y, z, 18, 11);
        put(&runtime, x, y + 1, z, 18, 4);
        put(&runtime, x - 1, y + 1, z + 1, 161, 5);
        put(&runtime, x + 1, y, z, 106, 2);
        break;
    case 11:
        put(&runtime, x, y, z, 17, 0);
        put(&runtime, x + 4, y, z, 18, 0);
        put(&runtime, x + 4, y + 4, z + 4, 161, 5);
        put(&runtime, x + 5, y, z, 18, 0);
        break;
    default:
        put(&runtime, x, y, z, 18, 0);
        put(&runtime, x + 1, y, z, 18, 4);
        put(&runtime, x - 1, y + 1, z + 1, 161, 5);
        put(&runtime, x + 2, y, z, 18, 0);
        break;
    }
    memset(&runtime.entities, 0, sizeof runtime.entities);
    if (!gm_runtime_set_world_random_seed48(&runtime, world_seed)
            || !gm_runtime_set_math_random_seed48(&runtime, math_seed)
            || !gm_runtime_set_block_random_seed48(&runtime, block_seed)
            || !gm_runtime_set_entity_id_cursor(&runtime, next_entity_id)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (fixture >= 11) {
        if (!gm_runtime_set_block(&runtime, x, y, z, 0, 0)) {
            gm_runtime_destroy(&runtime);
            return 4;
        }
    } else {
        int leaf = gm_world_block(runtime.world, x, y, z);
        if (!gm_runtime_random_tick_block(&runtime, x, y, z, leaf)) {
            gm_runtime_destroy(&runtime);
            return 4;
        }
    }
    printf("{\"ok\":true,\"case\":%d,\"blocks\":[", fixture);
    int first = 1;
    for (int dy = -5; dy <= 5; ++dy)
        for (int dz = -5; dz <= 5; ++dz)
            for (int dx = -5; dx <= 5; ++dx) {
                int state = gm_world_block(
                        runtime.world, x + dx, y + dy, z + dz) << 4
                    | (gm_world_meta(
                        runtime.world, x + dx, y + dy, z + dz) & 15);
                if (!first) putchar(',');
                printf("%d", state);
                first = 0;
            }
    fputs("],\"items\":[", stdout);
    first = 1;
    for (int eid = next_entity_id; eid < runtime.next_entity_id; ++eid)
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
            const GmLiveEnt *item = &runtime.entities.ents[slot];
            if (!item->active || item->eid != eid || item->type != 0)
                continue;
            if (!first) putchar(',');
            print_item(item, x, y, z);
            first = 0;
        }
    printf("],\"world_seed48\":%llu,\"math_seed48\":%llu,"
           "\"block_seed48\":%llu,\"next_entity_id\":%d}\n",
           (unsigned long long)runtime.world_random_seed48,
           (unsigned long long)runtime.math_random_seed48,
           (unsigned long long)runtime.block_random_seed48,
           runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return 0;
}
