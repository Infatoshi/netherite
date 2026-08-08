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
    if (errno || !text[0] || !end || *end || value < 0 || value > 98)
        return 0;
    *out = (int)value;
    return 1;
}

static unsigned float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (unsigned)bits;
}

static void put(
        GmRuntime *runtime, int x, int y, int z, int id, int meta) {
    gm_world_set_block_meta(runtime->world, x, y, z, id, meta);
}

static void print_blocks(
        const GmRuntime *runtime, int x, int y, int z,
        int radius, int max_dy) {
    int first = 1;
    putchar('[');
    for (int dy = 0; dy <= max_dy; ++dy)
        for (int dz = -radius; dz <= radius; ++dz)
            for (int dx = -radius; dx <= radius; ++dx) {
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
               entry.time - runtime->clock.total_time,
               entry.priority, index);
    }
    putchar(']');
}

static void print_world_events(
        const GmRuntime *runtime, int x, int y, int z) {
    int count = gm_runtime_world_event_count(runtime);
    putchar('[');
    for (int index = 0; index < count; ++index) {
        GmRuntimeWorldEvent event;
        if (!gm_runtime_world_event_get(runtime, index, &event)) exit(6);
        if (index) putchar(',');
        printf("{\"seq\":%d,\"id\":%d,\"x\":%d,\"y\":%d,"
               "\"z\":%d,\"data\":%d}",
               index, event.id, event.x - x, event.y - y,
               event.z - z, event.data);
    }
    putchar(']');
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
           item->x - origin_x, item->y - origin_y, item->z - origin_z,
           item->mx, item->my, item->mz,
           item->item, item->count, item->meta, item->age,
           item->pickup_delay, item->health, item->lifespan,
           float_bits(item->yaw), float_bits(item->hover_start),
           item->on_ground ? "true" : "false");
}

int main(int argc, char **argv) {
    static const int plant_ids[19] = {
        6, 6, 31, 32, 37, 38, 39, 40, 59, 104,
        105, 111, 115, 141, 142, 207, 83, 81, 175
    };
    static const int plant_metas[19] = {
        0, 8, 1, 0, 0, 4, 0, 0, 3, 4,
        6, 0, 2, 5, 7, 2, 7, 7, 4
    };
    static const int support_ids[19] = {
        3, 3, 2, 12, 2, 2, 110, 110, 60, 60,
        60, 9, 88, 60, 60, 60, 3, 12, 2
    };
    const uint64_t world_seed = UINT64_C(0x123456789ABC);
    const uint64_t math_seed = UINT64_C(0x0FEDCBA98765);
    const uint64_t block_seed = UINT64_C(0);
    const int next_entity_id = 780000;
    const int x = 12, y = 78, z = 8;
    GmConfig config;
    GmRuntime runtime;
    char err[256];
    int fixture;
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
    for (int dy = 0; dy <= 4; ++dy)
        for (int dz = -2; dz <= 2; ++dz)
            for (int dx = -2; dx <= 2; ++dx)
                put(&runtime, x + dx, y + dy, z + dz, 0, 0);
    int support_x = x, support_y = y, support_z = z;
    if (fixture < 19) {
        put(&runtime, x, y, z, support_ids[fixture], 0);
        put(&runtime, x, y + 1, z,
            plant_ids[fixture], plant_metas[fixture]);
    } else if (fixture < 27) {
        put(&runtime, x, y, z, 1, 0);
        put(&runtime, x, y + 1, z, 78, fixture - 19);
    } else if (fixture < 43) {
        put(&runtime, x, y, z, 1, 0);
        put(&runtime, x, y + 1, z, 171, fixture - 27);
    } else if (fixture < 55) {
        static const int dx[4] = {0, -1, 0, 1};
        static const int dz[4] = {1, 0, -1, 0};
        int meta = fixture - 43;
        int facing = meta & 3;
        support_x = x + dx[facing];
        support_y = y + 1;
        support_z = z + dz[facing];
        put(&runtime, support_x, support_y, support_z, 17, 3);
        put(&runtime, x, y + 1, z, 127, meta);
    } else if (fixture < 62) {
        static const int retain_support_ids[7] = {
            1, 18, 161, 78, 79, 174, 20
        };
        static const int retain_support_metas[7] = {
            0, 0, 0, 7, 0, 0, 0
        };
        int row = fixture - 55;
        put(&runtime, x, y, z,
            retain_support_ids[row], retain_support_metas[row]);
        put(&runtime, x, y + 1, z, 78, 3);
        put(&runtime, x, y + 2, z, 1, 0);
        support_y = y + 2;
    } else if (fixture == 62) {
        put(&runtime, x, y, z, 9, 0);
        put(&runtime, x, y + 1, z, 171, 6);
        put(&runtime, x, y + 2, z, 1, 0);
        support_y = y + 2;
    } else if (fixture < 65) {
        int meta = fixture == 63 ? 11 : 0;
        int log_meta = fixture == 63 ? 15 : 0;
        static const int dx[4] = {0, -1, 0, 1};
        static const int dz[4] = {1, 0, -1, 0};
        int facing = meta & 3;
        put(&runtime, x + dx[facing], y + 1, z + dz[facing],
            17, log_meta);
        put(&runtime, x, y + 1, z, 127, meta);
        put(&runtime, x, y + 2, z, 1, 0);
        support_y = y + 2;
    } else if (fixture == 65) {
        put(&runtime, x, y + 1, z, 208, 0);
        put(&runtime, x, y + 2, z, 1, 0);
        support_x = x + 1;
        support_y = y + 1;
        put(&runtime, support_x, support_y, support_z, 1, 0);
    } else if (fixture < 68) {
        support_y = y + 1;
        if (fixture == 66)
            put(&runtime, x, y + 2, z, 1, 0);
    } else if (fixture < 71) {
        put(&runtime, x, y + 1, z, 212, 0);
        support_x = x + 1;
        support_y = y + 1;
        put(&runtime, support_x, support_y, support_z,
            fixture == 70 ? 1 : 212, fixture == 70 ? 0 : 3);
        if (fixture == 69) {
            put(&runtime, x - 1, y + 1, z, 212, 0);
            put(&runtime, x, y + 1, z - 1, 212, 0);
        }
    } else if (fixture < 75) {
        support_x = fixture == 73 || fixture == 74 ? x + 1 : x;
        support_y = y + 1;
        if (fixture == 73 || fixture == 74)
            put(&runtime, x, y + 1, z, 19, 0);
        if (fixture != 74)
            put(&runtime, x - 1, y + 1, z, 9, 0);
        if (fixture == 73)
            put(&runtime, support_x, support_y, support_z, 1, 0);
    } else if (fixture == 75) {
        support_y = y + 1;
        for (int dy = 0; dy <= 3; ++dy)
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx)
                    if (dx != 0 || dy != 1 || dz != 0)
                        put(&runtime, x + dx, y + dy, z + dz, 9, 0);
    } else if (fixture <= 80) {
        int axis_x = fixture <= 78;
        int portal_meta = axis_x ? 1 : 2;
        for (int span = 0; span < 2; ++span) {
            int bx = x + (axis_x ? span : 0);
            int bz = z + (axis_x ? 0 : span);
            put(&runtime, bx, y, bz, 49, 0);
            put(&runtime, bx, y + 4, bz, 49, 0);
            for (int rise = 1; rise <= 3; ++rise)
                put(&runtime, bx, y + rise, bz, 90, portal_meta);
        }
        for (int rise = 1; rise <= 3; ++rise) {
            put(&runtime, x + (axis_x ? -1 : 0), y + rise,
                z + (axis_x ? 0 : -1), 49, 0);
            put(&runtime, x + (axis_x ? 2 : 0), y + rise,
                z + (axis_x ? 0 : 2), 49, 0);
        }
        if (fixture == 76) {
            support_z = z - 1;
            support_y = y + 1;
            put(&runtime, support_x, support_y, support_z, 1, 0);
        } else if (fixture == 77) {
            support_x = x - 1;
            support_y = y + 2;
        } else if (fixture == 78) {
            support_x = x + 1;
            support_y = y + 2;
        } else if (fixture == 79) {
            support_x = x - 1;
            support_y = y + 1;
            put(&runtime, support_x, support_y, support_z, 1, 0);
        } else {
            support_y = y + 2;
            support_z = z - 1;
        }
    } else if (fixture < 83) {
        put(&runtime, x, y, z, 1, 0);
        put(&runtime, x, y + 1, z, 51, 7);
        if (fixture == 82)
            put(&runtime, x + 1, y + 1, z, 5, 0);
    } else if (fixture < 85) {
        put(&runtime, x, y, z, 1, 0);
        put(&runtime, x, y + 1, z, 92, fixture == 83 ? 3 : 5);
        if (fixture == 84) {
            support_x = x + 1;
            support_y = y + 1;
            put(&runtime, support_x, support_y, support_z, 1, 0);
        }
    } else if (fixture < 87) {
        support_x = fixture == 85 ? x : x - 1;
        support_y = fixture == 85 ? y : y + 1;
        put(&runtime, support_x, support_y, support_z, 1, 0);
        put(&runtime, x, y + 1, z, 50, fixture == 85 ? 5 : 1);
    } else if (fixture < 89) {
        int attachment_x = fixture == 87 ? x : x - 1;
        int attachment_z = fixture == 87 ? z + 1 : z;
        put(&runtime, attachment_x, y + 1, attachment_z, 1, 0);
        put(&runtime, x, y + 1, z, 65, fixture == 87 ? 2 : 5);
        support_x = fixture == 87 ? attachment_x : x;
        support_y = y + 1;
        support_z = fixture == 87 ? attachment_z : z - 1;
        if (fixture == 88)
            put(&runtime, support_x, support_y, support_z, 1, 0);
    } else if (fixture < 91) {
        put(&runtime, x, y, z, 1, 0);
        put(&runtime, x, y + 1, z, 63, 7);
        if (fixture == 90) {
            support_x = x + 1;
            support_y = y + 1;
            put(&runtime, support_x, support_y, support_z, 1, 0);
        }
    } else if (fixture < 93) {
        int attachment_x = fixture == 91 ? x : x - 1;
        int attachment_z = fixture == 91 ? z + 1 : z;
        put(&runtime, attachment_x, y + 1, attachment_z, 1, 0);
        put(&runtime, x, y + 1, z, 68, fixture == 91 ? 2 : 5);
        support_x = fixture == 91 ? attachment_x : x;
        support_y = y + 1;
        support_z = fixture == 91 ? attachment_z : z - 1;
        if (fixture == 92)
            put(&runtime, support_x, support_y, support_z, 1, 0);
    } else if (fixture < 95) {
        put(&runtime, x, y, z, 1, 0);
        put(&runtime, x, y + 1, z, 176, 7);
        if (fixture == 94) {
            support_x = x + 1;
            support_y = y + 1;
            put(&runtime, support_x, support_y, support_z, 1, 0);
        }
    } else if (fixture < 97) {
        int attachment_x = fixture == 95 ? x : x - 1;
        int attachment_z = fixture == 95 ? z + 1 : z;
        put(&runtime, attachment_x, y + 1, attachment_z, 1, 0);
        put(&runtime, x, y + 1, z, 177, fixture == 95 ? 2 : 5);
        support_x = fixture == 95 ? attachment_x : x;
        support_y = y + 1;
        support_z = fixture == 95 ? attachment_z : z - 1;
        if (fixture == 96)
            put(&runtime, support_x, support_y, support_z, 1, 0);
    } else {
        put(&runtime, x, y, z, 1, 0);
        put(&runtime, x, y + 1, z, 140, 0);
        if (fixture == 98) {
            support_x = x + 1;
            support_y = y + 1;
            put(&runtime, support_x, support_y, support_z, 1, 0);
        }
    }
    if (fixture == 16) {
        put(&runtime, x + 1, y, z, 9, 0);
        put(&runtime, x, y + 2, z, 83, 9);
    } else if (fixture == 17) {
        put(&runtime, x, y + 2, z, 81, 8);
    } else if (fixture == 18) {
        put(&runtime, x, y + 2, z, 175, 10);
    }
    memset(&runtime.entities, 0, sizeof runtime.entities);
    if (!gm_runtime_set_world_random_seed48(&runtime, world_seed)
            || !gm_runtime_set_math_random_seed48(&runtime, math_seed)
            || !gm_runtime_set_block_random_seed48(&runtime, block_seed)
            || !gm_runtime_set_entity_id_cursor(
                &runtime, next_entity_id)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    int edit_id = 0;
    int edit_meta = 0;
    if (fixture >= 66 && fixture < 68) edit_id = 208;
    else if (fixture == 71 || fixture == 75) edit_id = 19;
    else if (fixture == 72) { edit_id = 19; edit_meta = 1; }
    else if (fixture == 74) edit_id = 9;
    int capture_radius = fixture >= 75 ? 2 : 1;
    int capture_height = fixture >= 76 ? 4 : 3;
    printf("{\"ok\":true,\"case\":%d,\"before_blocks\":", fixture);
    print_blocks(&runtime, x, y, z, capture_radius, capture_height);
    if (!gm_runtime_set_block(
            &runtime, support_x, support_y, support_z,
            edit_id, edit_meta)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    fputs(",\"after_blocks\":", stdout);
    print_blocks(&runtime, x, y, z, capture_radius, capture_height);
    fputs(",\"items\":[", stdout);
    {
        int first = 1;
        for (int eid = next_entity_id; eid < runtime.next_entity_id; ++eid)
            for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
                const GmLiveEnt *item = &runtime.entities.ents[slot];
                if (!item->active || item->type != 0 || item->eid != eid)
                    continue;
                if (!first) putchar(',');
                print_item(item, x, y, z);
                first = 0;
            }
    }
    putchar(']');
    if (fixture >= 68) {
        fputs(",\"scheduled\":", stdout);
        print_scheduled(&runtime, x, y, z);
    }
    if (fixture >= 71) {
        fputs(",\"world_events\":", stdout);
        print_world_events(&runtime, x, y, z);
    }
    printf(",\"world_seed48\":%llu,\"math_seed48\":%llu,"
           "\"block_seed48\":%llu,\"next_entity_id\":%d}\n",
           (unsigned long long)runtime.world_random_seed48,
           (unsigned long long)runtime.math_random_seed48,
           (unsigned long long)runtime.block_random_seed48,
           runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return 0;
}
