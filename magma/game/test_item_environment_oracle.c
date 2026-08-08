#include "game/runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t dbits(double value) {
    union { double d; uint64_t u; } bits;
    bits.d = value;
    return bits.u;
}

int main(int argc, char **argv) {
    const char *kind;
    uint64_t seed48;
    GmConfig config;
    GmRuntime runtime;
    GmAction idle;
    GmLiveEnt *item;
    char error[256];
    int y = 220;
    int age = 100;
    int ticks = 10;
    int fire = -1;
    int first_update = 1;
    double item_x = 0.5;
    double item_z = 0.5;
    double item_mx = 0.0;
    double item_my = 0.0;
    double item_mz = 0.0;
    if (argc != 3) return 2;
    kind = argv[1];
    seed48 = strtoull(argv[2], NULL, 10);
    if (seed48 >= (UINT64_C(1) << 48)) return 2;
    if (!strcmp(kind, "lava_scan")) ticks = 24;
    else if (!strcmp(kind, "lava_no_scan")) ticks = 23;
    else if (!strcmp(kind, "water_flow_first")) { }
    else if (!strcmp(kind, "water_flow_entry")) first_update = 0;
    else if (!strcmp(kind, "water_still_entry")) first_update = 0;
    else if (!strcmp(kind, "fire_contact")) { }
    else if (!strcmp(kind, "cactus_contact")) {
        item_x = 1.2;
        item_mx = -0.15;
    }
    else if (!strcmp(kind, "pushout_west")) item_x = 0.2;
    else if (!strcmp(kind, "pushout_east")) item_x = 0.8;
    else if (!strcmp(kind, "pushout_north")) item_z = 0.2;
    else if (!strcmp(kind, "pushout_south")) item_z = 0.8;
    else if (!strcmp(kind, "pushout_up")) { }
    else if (!strcmp(kind, "periodic_fire")) fire = 20;
    else if (!strcmp(kind, "expire")) age = 5999;
    else if (!strcmp(kind, "void")) y = -65;
    else return 2;
    if (!strncmp(kind, "pushout_", 8)) {
        item_mx = 0.02;
        item_my = 0.03;
        item_mz = 0.04;
    }

    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.daylight = 0;
    config.render = GM_RENDER_OFF;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "runtime init: %s\n", error);
        return 1;
    }
    runtime.randtick_enabled = 0;
    memset(&runtime.entities, 0, sizeof runtime.entities);
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    if (y >= 0) {
        for (int by = y - 2; by <= y + 2; ++by)
            for (int bz = -2; bz <= 2; ++bz)
                for (int bx = -2; bx <= 2; ++bx)
                    gm_world_set_block_meta(
                        runtime.world, bx, by, bz, 0, 0);
    }
    if (!strncmp(kind, "lava", 4))
        gm_world_set_block_meta(runtime.world, 0, y, 0, BLK_LAVA, 0);
    else if (!strcmp(kind, "water_flow_first")
            || !strcmp(kind, "water_flow_entry")) {
        gm_world_set_block_meta(
            runtime.world, 0, y, 0, BLK_FLOWING_WATER, 1);
        gm_world_set_block_meta(
            runtime.world, -1, y, 0, BLK_WATER, 0);
    } else if (!strcmp(kind, "water_still_entry")) {
        gm_world_set_block_meta(runtime.world, 0, y, 0, BLK_WATER, 0);
    } else if (!strcmp(kind, "fire_contact")) {
        gm_world_set_block_meta(runtime.world, 0, y - 1, 0, 87, 0);
        gm_world_set_block_meta(runtime.world, 0, y, 0, 51, 0);
    } else if (!strcmp(kind, "cactus_contact")) {
        gm_world_set_block_meta(runtime.world, 0, y - 1, 0, 12, 0);
        gm_world_set_block_meta(runtime.world, 0, y, 0, BLK_CACTUS, 0);
    } else if (!strncmp(kind, "pushout_", 8)) {
        gm_world_set_block_meta(runtime.world, 0, y, 0, BLK_STONE, 0);
        if (!strcmp(kind, "pushout_up")) {
            gm_world_set_block_meta(runtime.world, -1, y, 0, BLK_STONE, 0);
            gm_world_set_block_meta(runtime.world, 1, y, 0, BLK_STONE, 0);
            gm_world_set_block_meta(runtime.world, 0, y, -1, BLK_STONE, 0);
            gm_world_set_block_meta(runtime.world, 0, y, 1, BLK_STONE, 0);
        }
    }

    if (!gm_runtime_spawn_item_state_fixture(
            &runtime, 5000, item_x, (double)y, item_z,
            item_mx, item_my, item_mz, 0.0F, 0.0F,
            1, 1, 0, age, 7, 5, 6000, 0, 1, ticks,
            fire, 0, first_update, seed48)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    gm_runtime_tick(&runtime, idle);
    item = &runtime.entities.ents[0];
    printf("{\"ok\":true,\"kind\":\"%s\",\"alive\":%s,"
           "\"health\":%d,\"fire\":%d,\"in_water\":%s,"
           "\"first_update\":%s,\"age\":%d,"
           "\"pickup_delay\":%d,\"ticks_existed\":%d,"
           "\"on_ground\":%s,\"no_clip\":%s,"
           "\"entity_seed48\":%" PRIu64 ","
           "\"position_bits\":[\"%016" PRIx64 "\","
           "\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
           "\"motion_bits\":[\"%016" PRIx64 "\","
           "\"%016" PRIx64 "\",\"%016" PRIx64 "\"]}\n",
           kind, item->active ? "true" : "false",
           item->health, item->fire,
           item->in_water ? "true" : "false",
           item->first_update ? "true" : "false",
           item->age, item->pickup_delay, item->ticks_existed,
           item->on_ground ? "true" : "false",
           item->no_clip ? "true" : "false",
           (uint64_t)item->random_seed48,
           dbits(item->x - item_x), dbits(item->y - (double)y),
           dbits(item->z - item_z),
           dbits(item->mx), dbits(item->my), dbits(item->mz));
    gm_runtime_destroy(&runtime);
    return 0;
}
