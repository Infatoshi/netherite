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

static int source_at(
        const GmRuntime *runtime, int x, int y, int z,
        GmRuntimeStaticContainer *out) {
    int count = gm_runtime_static_container_count(runtime);
    for (int i = 0; i < count; ++i) {
        GmRuntimeStaticContainer value;
        if (gm_runtime_static_container_get(runtime, i, &value)
                && value.wx == x && value.wy == y && value.wz == z) {
            *out = value;
            return 1;
        }
    }
    return 0;
}

static int bonemeal_age(int block, int meta, int *max_age) {
    if (block == 2 && meta == 0) {
        *max_age = 1;
        return 0;
    }
    if (block == 6 && meta >= 0 && meta <= 13 && (meta & 7) <= 5) {
        *max_age = 1;
        return 0;
    }
    if ((block == 39 || block == 40) && meta == 0) {
        *max_age = 1;
        return 0;
    }
    if (block == 31 && meta >= 0 && meta <= 2) {
        *max_age = 1;
        return meta == 0 ? 1 : 0;
    }
    if (block == 127 && meta >= 0 && meta <= 11 && (meta & 3) != 1) {
        *max_age = 2;
        return meta >> 2;
    }
    if (block == 207 && meta >= 0 && meta <= 3) {
        *max_age = 3;
        return meta;
    }
    if (block == 175
            && (meta == 0 || meta == 1 || meta == 4 || meta == 5)) {
        *max_age = 1;
        return 0;
    }
    if ((block == 59 || block == 104 || block == 105
            || block == 141 || block == 142)
            && meta >= 0 && meta <= 7) {
        *max_age = 7;
        return meta;
    }
    if ((block == 81 || block == 83)
            && meta >= 0 && meta <= 15) {
        *max_age = 15;
        return meta;
    }
    if (block == 115 && meta >= 0 && meta <= 3) {
        *max_age = 3;
        return meta;
    }
    if (block == 110 && meta == 0) {
        *max_age = 0;
        return 0;
    }
    if (block == 60 && meta >= 0 && meta <= 7) {
        *max_age = 7;
        return meta;
    }
    if (block == 78 && meta >= 0 && meta <= 7) {
        *max_age = 7;
        return meta;
    }
    if (block == 79 && meta == 0) {
        *max_age = 0;
        return 0;
    }
    if (block == 80 && meta == 0) {
        *max_age = 0;
        return 0;
    }
    if (block == 106 && meta >= 0 && meta <= 15) {
        *max_age = 15;
        return meta;
    }
    if (block == 212 && meta >= 0 && meta <= 3) {
        *max_age = 3;
        return meta;
    }
    if (block == 200 && meta >= 0 && meta <= 5) {
        *max_age = 5;
        return meta;
    }
    if (block == 11 && meta >= 0 && meta <= 6) {
        *max_age = 0;
        return 0;
    }
    return -1;
}

static int place_plant_column(
        GmRuntime *runtime, int x, int y, int z,
        int block, int height) {
    if (block == 199) {
        for (int dy = height - 1; dy >= 1; --dy)
            if (!gm_runtime_set_block(
                    runtime, x, y - dy, z, block, 0))
                return 0;
        return 1;
    }
    for (int dy = 1; dy < height; ++dy)
        if (!gm_runtime_set_block(
                runtime, x, y - dy, z, block, 0))
            return 0;
    return 1;
}

static const GmLiveEnt *item_by_eid(
        const GmRuntime *runtime, int eid) {
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *item = &runtime->entities.ents[i];
        if (item->active && item->type == 0 && item->eid == eid)
            return item;
    }
    return NULL;
}

static unsigned float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (unsigned)bits;
}

static void print_drop(
        const GmLiveEnt *item, int origin_x, int origin_y, int origin_z) {
    if (!item) {
        fputs("null", stdout);
        return;
    }
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
    const int x = 12, z = 8;
    int y;
    GmConfig config;
    GmRuntime runtime;
    GmRuntimeStaticContainer source;
    ICStack source_stack;
    GmAction action;
    uint64_t seed48, math_seed48;
    int crop, meta, count, max_age, age, next_entity_id, upper, blocked, mega;
    int biome, player, natural, offhand, adjacent, column, dense, hydrated;
    int raining, lit, dimension, occupant;
    int tick_block_light = -1, tick_light = -1;
    char err[256];
    if (argc != 22
            || !parse_int(argv[1], &crop)
            || !parse_int(argv[2], &meta)
            || !parse_int(argv[3], &count)
            || !parse_seed(argv[4], &seed48)
            || !parse_seed(argv[5], &math_seed48)
            || !parse_int(argv[6], &next_entity_id)
            || !parse_int(argv[7], &upper)
            || !parse_int(argv[8], &blocked)
            || !parse_int(argv[9], &mega)
            || !parse_int(argv[10], &biome)
            || !parse_int(argv[11], &player)
            || !parse_int(argv[12], &natural)
            || !parse_int(argv[13], &offhand)
            || !parse_int(argv[14], &adjacent)
            || !parse_int(argv[15], &column)
            || !parse_int(argv[16], &dense)
            || !parse_int(argv[17], &hydrated)
            || !parse_int(argv[18], &raining)
            || !parse_int(argv[19], &lit)
            || !parse_int(argv[20], &dimension)
            || !parse_int(argv[21], &occupant))
        return 2;
    age = bonemeal_age(crop, meta, &max_age);
    if (age < 0 || count < 1 || count > 64 || next_entity_id <= 0
            || (upper != 0 && upper != 1) || (upper && crop != 175)
            || (blocked != 0 && blocked != 1)
            || (mega != 0 && mega != 1)
            || (player != 0 && player != 1)
            || (natural != 0 && natural != 1)
            || (offhand != 0 && offhand != 1)
            || (offhand && !player)
            || (adjacent != 0 && adjacent != 1)
            || (adjacent && (!natural
                || !(((crop == 104 || crop == 105)
                        && meta == 7 && !blocked)
                    || crop == 106
                    || (crop == 212 && meta == 3 && lit))))
            || (natural && (player
                || (crop != 6 && crop != 59 && crop != 104
                    && crop != 105 && crop != 127 && crop != 141
                    && crop != 142 && crop != 207 && crop != 81
                    && crop != 83 && crop != 115 && crop != 39
                    && crop != 40 && crop != 2 && crop != 110
                    && crop != 60 && crop != 78 && crop != 79
                    && crop != 80 && crop != 106 && crop != 11
                    && crop != 200 && crop != 212)))
            || ((crop == 110 || crop == 60 || crop == 78 || crop == 79
                    || crop == 80 || crop == 11
                    || crop == 200 || crop == 212)
                && !natural)
            || biome < 0 || biome >= 255
            || (blocked && crop != 39 && crop != 40
                && !(crop == 6 && ((meta & 8) || natural))
                && !(natural && (crop == 104 || crop == 105)
                    && meta == 7)
                && !(natural && (crop == 81 || crop == 83))
                && !(natural
                    && (crop == 2 || crop == 110 || crop == 60
                        || crop == 106 || crop == 11 || crop == 200)))
            || column < 1 || column > 3
            || (column != 1 && (!natural
                || (crop != 81 && crop != 83
                    && crop != 106 && crop != 11 && crop != 200)))
            || (dense != 0 && dense != 1)
            || (dense && (!natural
                || !((crop == 39 || crop == 40)
                    || crop == 106
                    || (crop == 212 && meta == 3 && lit))))
            || (hydrated != 0 && hydrated != 1)
            || (hydrated && (!natural || crop != 60))
            || (raining != 0 && raining != 1)
            || (raining && (!natural || crop != 60))
            || (lit != 0 && lit != 1)
            || (lit && (!natural
                || (crop != 78 && crop != 79 && crop != 80
                    && crop != 212)))
            || (dimension != 0
                && !(dimension == -1 && natural && lit
                    && (crop == 79 || crop == 212))
                && !(dimension == 1 && natural && crop == 60
                    && (occupant == 14 || occupant == 15)))
            || occupant < 0 || occupant > 15
            || (occupant && !(next_entity_id > 1
                && natural && crop == 60 && meta == 0
                && !blocked && !hydrated && !raining))
            || (mega && !(crop == 6 && (meta & 8)
                && ((meta & 7) == 1 || (meta & 7) == 3
                    || (meta & 7) == 5))))
        return 2;

    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, err, sizeof err))
        return 3;
    if (!gm_runtime_set_dimension(&runtime, dimension)) {
        gm_runtime_destroy(&runtime);
        return 3;
    }
    y = 78;
    if (raining)
        gm_runtime_set_weather(&runtime, 1, 0, 100, 100);
    gm_runtime_set_pose(&runtime, 8.5, 78.0, 8.5, 0.0F, 0.0F);
    if (occupant == 14 || occupant == 15)
        gm_dragon_init(&runtime.dragon, runtime.world, config.seed);
    if (dimension == -1 && crop == 212)
        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx)
                    gm_world_set_block_meta(
                        runtime.world, x + 1 + dx, y + dy, z + dz, 0, 0);
    if (crop == 2 || (natural && crop == 110))
        for (int dz = -7; dz <= 7; ++dz)
            for (int dx = -7; dx <= 7; ++dx)
                if ((dx != 0 || dz != 0)
                        && (natural || dx != -1 || dz != 0)
                        && !gm_runtime_set_block(
                            &runtime, x + 1 + dx, y, z + dz,
                            natural ? 3 : 2, 0)) {
                    gm_runtime_destroy(&runtime);
                    return 4;
                }
    if (crop == 2)
        for (int dz = -7; dz <= 7; ++dz)
            for (int dx = -7; dx <= 7; ++dx)
                if (!gm_world_debug_set_biome(
                        runtime.world, x + 1 + dx, z + dz, biome)) {
                    gm_runtime_destroy(&runtime);
                    return 4;
                }
    if (mega)
        for (int dz = 0; dz <= 1; ++dz)
            for (int dx = 0; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) continue;
                if (!gm_runtime_set_block(
                            &runtime, x + 1 + dx, y - 1, z + dz,
                            3, 0)
                        || !gm_runtime_set_block(
                            &runtime, x + 1 + dx, y, z + dz,
                            6, meta)) {
                    gm_runtime_destroy(&runtime);
                    return 4;
                }
            }
    if (natural && (crop == 104 || crop == 105))
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
                if ((dx != 0 || dz != 0)
                        && !gm_runtime_set_block(
                            &runtime, x + 1 + dx, y - 1, z + dz,
                            3, 0)) {
                    gm_runtime_destroy(&runtime);
                    return 4;
                }
    if (adjacent && crop != 106 && !gm_runtime_set_block(
            &runtime, x + 2, y, z, crop == 104 ? 86 : 103,
            crop == 104 ? 2 : 0)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (natural && (crop == 39 || crop == 40))
        for (int dz = -6; dz <= 6; ++dz)
            for (int dx = -6; dx <= 6; ++dx)
                if ((dx != 0 || dz != 0)
                        && !gm_runtime_set_block(
                            &runtime, x + 1 + dx, y - 1, z + dz,
                            110, 0)) {
                    gm_runtime_destroy(&runtime);
                    return 4;
                }
    if (dense && (crop == 39 || crop == 40))
        for (int dx = -4; dx <= -1; ++dx)
            if (!gm_runtime_set_block(
                    &runtime, x + 1 + dx, y, z, crop, 0)) {
                gm_runtime_destroy(&runtime);
                return 4;
            }
    if (dense && crop == 212)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                int neighbor_meta;
                if (dx == 0 && dz == 0) continue;
                neighbor_meta = dx == -1 && dz == 0 ? 2
                    : dx == 0 && dz == 1 ? 1 : 0;
                if (!gm_runtime_set_block(
                        &runtime, x + 1 + dx, y, z + dz,
                        212, neighbor_meta)) {
                    gm_runtime_destroy(&runtime);
                    return 4;
                }
            }
    if (crop == 106) {
        static const int dx[4] = {0, 1, 0, -1};
        static const int dz[4] = {-1, 0, 1, 0};
        static const int bits[4] = {4, 8, 1, 2};
        for (int i = 0; i < 4; ++i) {
            if (((meta & bits[i]) || column == 3)
                    && !gm_runtime_set_block(
                        &runtime, x + 1 + dx[i], y, z + dz[i],
                        1, 0)) {
                gm_runtime_destroy(&runtime);
                return 4;
            }
            if (column == 2 && (meta & bits[i])
                    && !gm_runtime_set_block(
                        &runtime, x + 1 + dx[i], y + 1, z + dz[i],
                        1, 0)) {
                gm_runtime_destroy(&runtime);
                return 4;
            }
        }
        if (blocked && !gm_runtime_set_block(
                &runtime, x + 1, y - 1, z, 106, 1)) {
            gm_runtime_destroy(&runtime);
            return 4;
        }
        if (dense) {
            static const int density_x[4] = {-4, -3, 3, 4};
            /* Java stages these deliberately unattached density counters with
             * update flag two. Raw writes preserve that fixture boundary now
             * that ordinary flag-three edits correctly prune invalid vines. */
            for (int i = 0; i < 4; ++i) {
                gm_world_set_block_meta(
                    runtime.world, x + 1 + density_x[i], y, z, 106, 0);
                gm_world_set_block_meta(
                    runtime.world, x + 1 + density_x[i], y + 1, z, 1, 0);
            }
        }
        if (adjacent) {
            static const int support_dx[3] = {1, 1, 1};
            static const int support_dy[3] = {0, 0, 1};
            static const int support_dz[3] = {-1, 1, 0};
            for (int i = 0; i < 3; ++i)
                if (!gm_runtime_set_block(
                        &runtime, x + 1 + support_dx[i],
                        y + support_dy[i], z + support_dz[i], 1, 0)) {
                    gm_runtime_destroy(&runtime);
                    return 4;
                }
            if (meta == 0 && !gm_runtime_set_block(
                    &runtime, x + 1, y + 1, z, 1, 0)) {
                gm_runtime_destroy(&runtime);
                return 4;
            }
        }
    }
    if (crop == 11 && (meta == 4 || meta == 5)) {
        int fuel_x = x + 1 + (column == 1 ? 2 : 1);
        int fuel_y = y + 1;
        int fuel_z = z - 1;
        int support_x = meta == 4 ? fuel_x + 1 : fuel_x;
        int support_y = meta == 4 ? fuel_y : fuel_y - 1;
        if (!gm_runtime_set_block(
                    &runtime, support_x, support_y, fuel_z, 1, 0)
                || !gm_runtime_set_block(
                    &runtime, fuel_x, fuel_y, fuel_z,
                    meta == 4 ? 106 : 171,
                    meta == 4 ? 8 : 0)) {
            gm_runtime_destroy(&runtime);
            return 4;
        }
    } else if (crop == 11)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                static const int fuels[7] = {
                    5, 17, 18, 35, 106, 171, 46
                };
                static const int fuel_metas[7] = {
                    0, 0, 8, 0, 0, 0, 0
                };
                int block;
                int by;
                if (column == 2 && dx == 0 && dz == 0)
                    continue;
                block = column == 3 && meta == 0 ? 1 : fuels[meta];
                by = column == 1 ? y + 2
                    : column == 2 ? y : y + 1;
                if (!gm_runtime_set_block(
                        &runtime, x + 1 + dx, by, z + dz,
                        block, column == 3 && meta == 0
                            ? 0 : fuel_metas[meta])) {
                    gm_runtime_destroy(&runtime);
                    return 4;
                }
            }
    if (hydrated && !gm_runtime_set_block(
            &runtime, x + 5, y + 1, z + 4, 9, 0)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (lit && crop != 80 && crop != 212 && !gm_runtime_set_block(
            &runtime, crop == 79 ? x + 1 : x + 2,
            crop == 79 ? y + 4 : y, z, 89, 0)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (natural && crop == 79)
        for (int dz = -4; dz <= 4; ++dz)
            for (int dx = -4; dx <= 4; ++dx)
                if (abs(dx) + abs(dz) <= 4
                        && !gm_runtime_set_block(
                            &runtime, x + 1 + dx, y - 1, z + dz,
                            1, 0)) {
                    gm_runtime_destroy(&runtime);
                    return 4;
                }
    if ((crop != 106 && !gm_runtime_set_block(
                &runtime, x + 1,
                crop == 11 ? y - 1 : y - column - upper, z,
                crop == 81 ? 12
                    : crop == 83 ? 3
                    : crop == 115 ? 88
                    : crop == 11 ? 1
                    : natural && (crop == 39 || crop == 40) ? 110
                    : crop == 79 ? 1
                    : crop == 212 && dimension == -1 ? 1
                    : crop == 110 || crop == 60 || crop == 78
                    || crop == 80 || crop == 212 ? 3
                    : crop == 200 ? 121
                    : crop == 2 || crop == 39 || crop == 40
                    || (crop == 6 && (meta & 8)) ? 3 : 60,
                crop == 81 || crop == 83 || crop == 115
                    || crop == 110 || crop == 60 || crop == 78
                    || crop == 80 || crop == 212
                    || crop == 79 || crop == 11 || crop == 200 ? 0
                    : crop == 2 || crop == 39 || crop == 40
                    || (crop == 6 && (meta & 8)) ? 0 : 7))
            || (crop == 83 && !gm_runtime_set_block(
                &runtime, x + 2, y - column, z, 9, 0))
            || ((crop == 81 || crop == 83 || crop == 200) && column > 1
                && !place_plant_column(
                    &runtime, x + 1, y, z,
                    crop == 200 ? 199 : crop, column))
            || (crop == 127 && !gm_runtime_set_block(
                &runtime,
                x + 1 + (int[4]){0, -1, 0, 1}[meta & 3], y,
                z + (int[4]){1, 0, -1, 0}[meta & 3], 17, 3))
            || (crop == 175
                ? (!gm_runtime_set_block(
                        &runtime, x + 1, y - upper, z, 175, meta)
                    || !gm_runtime_set_block(
                        &runtime, x + 1, y + 1 - upper, z, 175, 10))
                : !natural && (crop == 39 || crop == 40) ? 0
                : !gm_runtime_set_block(
                    &runtime, x + 1, y, z, crop,
                    crop == 11 ? 0 : meta))
            || (blocked && crop != 11 && crop != 106
                && !gm_runtime_set_block(
                    &runtime, x + 1,
                    natural && (crop == 104 || crop == 105)
                        ? y : natural && (crop == 81 || crop == 83)
                        ? y + 1 : natural && crop == 200
                        ? y + 2 : natural && !(meta & 8) ? y + 1 : y + 4,
                    natural && (crop == 104 || crop == 105)
                        ? z - 1 : z, crop == 60 ? 59 : 1, 0))
            || (!player && !natural
                && !gm_runtime_set_block(&runtime, x, y, z, 23, 13))
            || (!player && !natural
                && !gm_runtime_static_container_set_slot(
                &runtime, 0, x, y, z, 0, 351, count, 15))
            || (!player && !natural
                && !gm_runtime_set_dispenser_random_seed48(&runtime, 0))
            || !gm_runtime_set_world_random_seed48(&runtime, seed48)
            || !gm_runtime_set_world_random_gaussian(&runtime, 0, 0.0)
            || !gm_runtime_set_math_random_seed48(&runtime, math_seed48)
            || !gm_runtime_set_entity_id_cursor(&runtime, next_entity_id)
            || (crop == 11
                && !gm_runtime_set_do_fire_tick(&runtime, !blocked))) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (!natural && (crop == 39 || crop == 40))
        gm_world_set_block_meta(
            runtime.world, x + 1, y, z, crop, meta);
    if (occupant == 1 && !gm_live_spawn_item_exact(
            &runtime.entities, next_entity_id - 1,
            x + 1.5, y + 0.94, z + 0.5,
            0.0, 0.0, 0.0, 0.0F,
            1, 1, 0, 0, 32767, 1)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (occupant == 2) {
        GmRuntimeFallingBlock *falling = &runtime.falling_blocks[0];
        memset(falling, 0, sizeof *falling);
        falling->active = 1;
        falling->eid = next_entity_id - 1;
        falling->block = 12;
        falling->x = x + 1.5;
        falling->y = y + 0.5;
        falling->z = z + 0.5;
        falling->no_gravity = 1;
        runtime.falling_block_count = 1;
    }
    if (occupant == 3 && gm_mobs_spawn_exact(
            &runtime.mobs, GM_MOB_ZOMBIE, next_entity_id - 1,
            x + 1.5, y + 0.5, z + 0.5,
            0.0, 0.0, 0.0, 0.0F, 20.0F, 1, 0, 0, 0) < 0) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (occupant == 4 && gm_mobs_spawn_boat_exact(
            &runtime.mobs, next_entity_id - 1,
            x + 1.5, y + 0.5, z + 0.5, 0.0F) < 0) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (occupant == 5 && !gm_mobs_spawn_xp_exact(
            &runtime.mobs, x + 1.5, y + 0.5, z + 0.5,
            0.0, 0.0, 0.0, 1, next_entity_id - 1,
            0, 32767, 0, 0)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (occupant == 6 && !gm_runtime_spawn_arrow_fixture(
            &runtime, next_entity_id - 1,
            x + 1.5, y + 0.5, z + 0.5,
            0.0, 0.0, 0.0, 1, 0)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (occupant == 7 && !gm_runtime_spawn_primed_tnt_fixture(
            &runtime, next_entity_id - 1,
            x + 1.5, y + 0.5, z + 0.5,
            0.0, 0.0, 0.0, 80)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (occupant == 8 && !gm_runtime_spawn_minecart_fixture(
            &runtime, GM_MINECART_RIDEABLE, next_entity_id - 1,
            x + 1.5, y + 0.5, z + 0.5,
            0.0, 0.0, 0.0, 0.0F)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (occupant == 9) {
        int eid;
        if (!gm_runtime_set_entity_id_cursor(
                    &runtime, next_entity_id - 1)
                || (eid = gm_runtime_spawn_firework_payload(
                    &runtime, x + 1.5, y + 0.5, z + 0.5,
                    0, 0, 0, 0, 0)) != next_entity_id - 1) {
            gm_runtime_destroy(&runtime);
            return 4;
        }
        runtime.fireworks[0].vx = 0.0;
        runtime.fireworks[0].vy = 0.0;
        runtime.fireworks[0].vz = 0.0;
    }
    if (occupant == 10 && !gm_runtime_spawn_fish_hook_fixture(
            &runtime, next_entity_id - 1,
            x + 1.5, y + 0.5, z + 0.5,
            0.0, 0.0, 0.0, 0.0F, 0.0F,
            0, 0, 0, 0, 0, 0, 0, 0.0F, 0, 0, 0,
            0, 0, 0.0)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (occupant == 11 && !gm_runtime_spawn_end_crystal_fixture(
            &runtime, next_entity_id - 1,
            x + 1.5, y + 0.5, z + 0.5,
            0, 1, 0, 0, 0, 0)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (occupant == 12 && !gm_runtime_spawn_area_effect_cloud_fixture(
            &runtime, next_entity_id - 1, 0, /* TB_PT_EMPTY */
            x + 1.5, y + 0.5, z + 0.5,
            0, 600, 20, 20, 3.0F, 0.0F, 0.0F, 0)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (occupant == 13) {
        runtime.server_player.ent.posX =
            x + 1.5 - (double)runtime.ox;
        runtime.server_player.ent.posY = y + 0.5;
        runtime.server_player.ent.posZ =
            z + 0.5 - (double)runtime.oz;
        runtime.server_player.ent.box = psv_player_box(
            runtime.server_player.ent.posX,
            runtime.server_player.ent.posY,
            runtime.server_player.ent.posZ);
        runtime.server_player.ent.motionX = 0.0;
        runtime.server_player.ent.motionY = 0.0;
        runtime.server_player.ent.motionZ = 0.0;
    }
    if (occupant == 14) {
        EdDragon *dragon = &runtime.dragon.state.arena.dragon;
        dragon->x = x + 1.5;
        dragon->y = y + 0.5;
        dragon->z = z + 0.5;
        dragon->alive = 1;
    }
    if (occupant == 15) {
        EdCrystal *crystal = &runtime.dragon.state.arena.crystals[0];
        crystal->x = x + 1.5;
        crystal->y = y + 0.5;
        crystal->z = z + 0.5;
        crystal->alive = 1;
    }
    if (!player && !natural && !gm_runtime_schedule_tick(
            &runtime, x, y, z, 23, 1, 0, 0)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (crop == 80) {
        if (!gm_world_debug_set_block_light(
                    runtime.world, x + 1, y, z, lit ? 12 : 11)) {
            gm_runtime_destroy(&runtime);
            return 4;
        }
        tick_block_light =
            gm_world_block_light(runtime.world, x + 1, y, z);
    }
    if (crop == 79 && dimension == -1
            && !gm_world_debug_set_block_light(
                runtime.world, x + 1, y, z, 15)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (crop == 79 && dimension == -1)
        tick_block_light =
            gm_world_block_light(runtime.world, x + 1, y, z);
    if ((crop == 78 || (crop == 79 && dimension != -1))
            && !gm_world_debug_set_block_light(
                runtime.world, x + 1, y, z, lit ? 15 : 0)) {
        gm_runtime_destroy(&runtime);
        return 4;
    }
    if (crop == 78 || (crop == 79 && dimension != -1))
        tick_block_light =
            gm_world_block_light(runtime.world, x + 1, y, z);
    if (crop == 212) {
        static const int dx[5] = {0, 1, -1, 0, 0};
        static const int dy[5] = {1, 0, 0, 0, 0};
        static const int dz[5] = {0, 0, 0, 1, -1};
        if (!lit)
            for (int i = 0; i < 5; ++i)
                if (!gm_runtime_set_block(
                        &runtime, x + 1 + dx[i], y + dy[i], z + dz[i],
                        1, 0)) {
                    gm_runtime_destroy(&runtime);
                    return 4;
                }
        if (adjacent) {
            static const int ax[5] = {0, 0, 0, -1, 1};
            static const int ay[5] = {1, 0, 0, 0, 0};
            static const int az[5] = {0, -1, 1, 0, 0};
            static const int am[5] = {0, 1, 2, 3, 0};
            for (int i = 0; i < 5; ++i)
                if (!gm_runtime_set_block(
                        &runtime, x + 1 + ax[i], y + ay[i], z + az[i],
                        212, am[i])) {
                    gm_runtime_destroy(&runtime);
                    return 4;
                }
        }
        if (!gm_world_debug_set_block_light(
                runtime.world, x + 1, y, z, lit ? 15 : 0)) {
            gm_runtime_destroy(&runtime);
            return 4;
        }
        tick_light = gm_runtime_frosted_ice_light(
            &runtime, x + 1, y, z);
    }
    if (natural) {
        source_stack = ic_empty();
        (void)gm_runtime_random_tick_block(
            &runtime, x + 1, y, z, crop);
    } else if (player) {
        isr_set_stack(
            &runtime.player.inv,
            offhand ? ISR_OFFHAND_SLOT : runtime.player.inv.current_item,
            ic_mk(351, count, 15));
        (void)gm_runtime_player_apply_bonemeal(
            &runtime, x + 1, y, z,
            offhand ? ISR_OFFHAND_SLOT : runtime.player.inv.current_item, 0);
        source_stack = isr_get_stack(
            &runtime.player.inv,
            offhand ? ISR_OFFHAND_SLOT : runtime.player.inv.current_item);
    } else {
        memset(&action, 0, sizeof action);
        action.hotbar_sel = -1;
        gm_runtime_tick(&runtime, action);
        if (!source_at(&runtime, x, y, z, &source)) {
            gm_runtime_destroy(&runtime);
            return 5;
        }
        source_stack = source.slots[0];
    }

    printf("{\"ok\":true,\"source_item\":%d,\"source_count\":%d,"
           "\"source_meta\":%d,\"crop_id\":%d,\"crop_meta\":%d,"
           "\"above_id\":%d,\"above_meta\":%d,"
           "\"below_id\":%d,\"below_meta\":%d,"
           "\"world_seed48\":%llu,\"drop\":",
           source_stack.item, source_stack.count, source_stack.meta,
           gm_world_block(runtime.world, x + 1, y, z),
           gm_world_meta(runtime.world, x + 1, y, z),
           gm_world_block(runtime.world, x + 1, y + 1, z),
           gm_world_meta(runtime.world, x + 1, y + 1, z),
           gm_world_block(runtime.world, x + 1, y - 1, z),
           gm_world_meta(runtime.world, x + 1, y - 1, z),
           (unsigned long long)runtime.world_random_seed48);
    print_drop(crop == 80 ? NULL
        : item_by_eid(&runtime, next_entity_id), x, y, z);
    if (occupant) {
        double occupant_y;
        if (occupant == 1) {
            const GmLiveEnt *entity =
                item_by_eid(&runtime, next_entity_id - 1);
            if (!entity) {
                gm_runtime_destroy(&runtime);
                return 5;
            }
            occupant_y = entity->y;
        } else if (occupant == 2 && runtime.falling_block_count == 1
                && runtime.falling_blocks[0].active
                && runtime.falling_blocks[0].eid == next_entity_id - 1) {
            occupant_y = runtime.falling_blocks[0].y;
        } else if (occupant == 3 || occupant == 4) {
            const EwStore *store = runtime.mobs.current
                ? &runtime.mobs.b : &runtime.mobs.a;
            int slot = gm_mobs_find_slot_by_eid(
                &runtime.mobs, next_entity_id - 1);
            if (slot <= 0 || !store->alive[slot]) {
                gm_runtime_destroy(&runtime);
                return 5;
            }
            occupant_y = store->y[slot];
        } else if (occupant == 5) {
            int found = 0;
            occupant_y = 0.0;
            for (int slot = 0; slot < GM_XP_ORBS; ++slot)
                if (!runtime.mobs.xp_orbs[slot].dead
                        && runtime.mobs.xp_orbs[slot].xpValue > 0
                        && runtime.mobs.xp_orbs[slot].eid
                            == next_entity_id - 1) {
                    occupant_y = runtime.mobs.xp_orbs[slot].posY;
                    found = 1;
                    break;
                }
            if (!found) {
                gm_runtime_destroy(&runtime);
                return 5;
            }
        } else if (occupant == 6) {
            int found = 0;
            occupant_y = 0.0;
            for (int slot = 0; slot < GM_RUNTIME_PROJECTILES; ++slot)
                if (runtime.projectiles[slot].active
                        && runtime.projectiles[slot].eid
                            == next_entity_id - 1) {
                    occupant_y = runtime.projectiles[slot].y;
                    found = 1;
                    break;
                }
            if (!found) {
                gm_runtime_destroy(&runtime);
                return 5;
            }
        } else if (occupant == 7) {
            occupant_y = runtime.primed_tnt[0].y;
        } else if (occupant == 8) {
            occupant_y = runtime.minecarts[0].y;
        } else if (occupant == 9) {
            occupant_y = runtime.fireworks[0].y;
        } else if (occupant == 10) {
            occupant_y = runtime.fish_hook.y;
        } else if (occupant == 11) {
            occupant_y = runtime.end_crystals[0].y;
        } else if (occupant == 12) {
            occupant_y = runtime.area_effect_clouds[0].y;
        } else if (occupant == 13) {
            occupant_y = runtime.server_player.ent.posY;
        } else if (occupant == 14) {
            occupant_y = runtime.dragon.state.arena.dragon.y;
        } else if (occupant == 15) {
            occupant_y = runtime.dragon.state.arena.crystals[0].y;
        } else {
            gm_runtime_destroy(&runtime);
            return 5;
        }
        printf(",\"occupant_y\":%.17g", occupant_y - (double)y);
    }
    if (crop == 78 || crop == 79 || crop == 80)
        printf(",\"block_light\":%d",
               crop == 80 || crop == 78 || crop == 79 ? tick_block_light
                   : gm_world_block_light(runtime.world, x + 1, y, z));
    if (crop == 212)
        printf(",\"light\":%d", tick_light);
    if (crop == 11 || crop == 79 || crop == 212) {
        fputs(",\"scheduled\":[", stdout);
        int first = 1, rank = 0;
        for (int i = 0; i < gm_runtime_scheduled_tick_count(&runtime); ++i) {
            GmRuntimeScheduledTick entry;
            if (!gm_runtime_scheduled_tick_get(&runtime, i, &entry)) {
                gm_runtime_destroy(&runtime);
                return 5;
            }
            if (crop == 79 && (entry.x != x + 1
                    || entry.y != y || entry.z != z))
                continue;
            if (!first) putchar(',');
            printf("[%d,%d,%d,%d,%lld,%d,%d]",
                   entry.x - (x + 1), entry.y - y, entry.z - z,
                   entry.block, entry.time - runtime.clock.total_time,
                   entry.priority, rank++);
            first = 0;
        }
        putchar(']');
    }
    if (crop == 80) {
        fputs(",\"drops\":[", stdout);
        int first = 1;
        for (int i = 0; i < 4; ++i) {
            const GmLiveEnt *item =
                item_by_eid(&runtime, next_entity_id + i);
            if (!item) continue;
            if (!first) putchar(',');
            print_drop(item, x, y, z);
            first = 0;
        }
        putchar(']');
    }
    if (natural && crop == 11) {
        fputs(",\"blocks\":[", stdout);
        int first = 1;
        for (int dy = -1; dy <= 3; ++dy)
            for (int dz = -3; dz <= 3; ++dz)
                for (int dx = -3; dx <= 3; ++dx) {
                    int state = gm_world_block(
                            runtime.world, x + 1 + dx, y + dy, z + dz)
                            << 4
                        | (gm_world_meta(
                            runtime.world, x + 1 + dx, y + dy,
                            z + dz) & 15);
                    if (!first) putchar(',');
                    printf("%d", state);
                    first = 0;
        }
        putchar(']');
    } else if (natural && crop == 106) {
        fputs(",\"blocks\":[", stdout);
        int first = 1;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -4; dz <= 4; ++dz)
                for (int dx = -4; dx <= 4; ++dx) {
                    int state = gm_world_block(
                            runtime.world, x + 1 + dx, y + dy, z + dz)
                            << 4
                        | (gm_world_meta(
                            runtime.world, x + 1 + dx, y + dy,
                            z + dz) & 15);
                    if (!first) putchar(',');
                    printf("%d", state);
                    first = 0;
                }
        putchar(']');
    } else if (natural && crop == 200) {
        fputs(",\"blocks\":[", stdout);
        int first = 1;
        for (int dy = -3; dy <= 2; ++dy)
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx) {
                    int state = gm_world_block(
                            runtime.world, x + 1 + dx, y + dy, z + dz)
                            << 4
                        | (gm_world_meta(
                            runtime.world, x + 1 + dx, y + dy,
                            z + dz) & 15);
                    if (!first) putchar(',');
                    printf("%d", state);
                    first = 0;
                }
        putchar(']');
    } else if (natural && crop == 212) {
        static const int dx[11] = {
            0, 0, 0, -1, 0, 1, -1, 1, -1, 0, 1
        };
        static const int dy[11] = {
            0, -1, 1, 0, 0, 0, 0, 0, 0, 0, 0
        };
        static const int dz[11] = {
            0, 0, 0, -1, -1, -1, 0, 0, 1, 1, 1
        };
        fputs(",\"blocks\":[", stdout);
        for (int i = 0; i < 11; ++i) {
            int state = gm_world_block(
                    runtime.world, x + 1 + dx[i], y + dy[i], z + dz[i])
                    << 4
                | (gm_world_meta(
                    runtime.world, x + 1 + dx[i], y + dy[i],
                    z + dz[i]) & 15);
            if (i) putchar(',');
            printf("%d", state);
        }
        putchar(']');
    } else if (natural && (crop == 104 || crop == 105)) {
        fputs(",\"blocks\":[", stdout);
        int first = 1;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                int state = gm_world_block(
                    runtime.world, x + 1 + dx, y, z + dz) << 4
                    | (gm_world_meta(
                        runtime.world, x + 1 + dx, y, z + dz) & 15);
                if (!first) putchar(',');
                printf("%d", state);
                first = 0;
            }
        putchar(']');
    } else if (natural && (crop == 39 || crop == 40)) {
        fputs(",\"blocks\":[", stdout);
        int first = 1;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -6; dz <= 6; ++dz)
                for (int dx = -6; dx <= 6; ++dx) {
                    int state = gm_world_block(
                        runtime.world, x + 1 + dx, y + dy, z + dz) << 4
                        | (gm_world_meta(
                            runtime.world, x + 1 + dx, y + dy,
                            z + dz) & 15);
                    if (!first) putchar(',');
                    printf("%d", state);
                    first = 0;
                }
        putchar(']');
    } else if (crop == 6 && (meta & 8)) {
        fputs(",\"blocks\":[", stdout);
        int first = 1;
        for (int dy = -1; dy <= 34; ++dy)
            for (int dz = -15; dz <= 15; ++dz)
                for (int dx = -15; dx <= 15; ++dx) {
                    int bx = x + 1 + dx, by = y + dy, bz = z + dz;
                    int state = dx == -1 && dy == 0 && dz == 0 ? 0
                        : gm_world_block(runtime.world, bx, by, bz) << 4
                            | (gm_world_meta(
                                runtime.world, bx, by, bz) & 15);
                    if (!first) putchar(',');
                    printf("%d", state);
                    first = 0;
                }
        putchar(']');
    } else if (crop == 39 || crop == 40) {
        fputs(",\"blocks\":[", stdout);
        int first = 1;
        for (int dy = -1; dy <= 13; ++dy)
            for (int dz = -3; dz <= 3; ++dz)
                for (int dx = -3; dx <= 3; ++dx) {
                    int bx = x + 1 + dx, by = y + dy, bz = z + dz;
                    int state = dx == -1 && dy == 0 && dz == 0 ? 0
                        : gm_world_block(runtime.world, bx, by, bz) << 4
                            | (gm_world_meta(
                                runtime.world, bx, by, bz) & 15);
                    if (!first) putchar(',');
                    printf("%d", state);
                    first = 0;
        }
        putchar(']');
    } else if (crop == 2 || crop == 110) {
        printf(",\"biome\":%d,\"blocks\":[",
               gm_world_biome(runtime.world, x + 1, z));
        int first = 1;
        for (int dy = 0; dy <= 1; ++dy)
            for (int dz = -7; dz <= 7; ++dz)
                for (int dx = -7; dx <= 7; ++dx) {
                    int state = !natural && dx == -1
                            && dy == 0 && dz == 0 ? 0
                        : gm_world_block(
                            runtime.world, x + 1 + dx, y + dy, z + dz) << 4
                            | (gm_world_meta(
                                runtime.world, x + 1 + dx, y + dy,
                                z + dz) & 15);
                    if (!first) putchar(',');
                    printf("%d", state);
                    first = 0;
        }
        putchar(']');
    } else if (natural && crop == 60) {
        fputs(",\"blocks\":[", stdout);
        int first = 1;
        for (int dy = 0; dy <= 1; ++dy)
            for (int dz = -4; dz <= 4; ++dz)
                for (int dx = -4; dx <= 4; ++dx) {
                    int state = gm_world_block(
                        runtime.world, x + 1 + dx, y + dy, z + dz) << 4
                        | (gm_world_meta(
                            runtime.world, x + 1 + dx, y + dy,
                            z + dz) & 15);
                    if (!first) putchar(',');
                    printf("%d", state);
                    first = 0;
                }
        putchar(']');
    }
    printf(",\"math_seed48\":%llu,\"next_entity_id\":%d,\"events\":[",
           (unsigned long long)runtime.math_random_seed48,
           runtime.next_entity_id);
    for (int i = 0; i < gm_runtime_world_event_count(&runtime); ++i) {
        GmRuntimeWorldEvent event;
        if (!gm_runtime_world_event_get(&runtime, i, &event)) {
            gm_runtime_destroy(&runtime);
            return 6;
        }
        if (i) putchar(',');
        printf("[%d,%d,%d,%d,%d]", event.id,
               event.x - x, event.y - y, event.z - z, event.data);
    }
    puts("]}");
    gm_runtime_destroy(&runtime);
    return 0;
}
