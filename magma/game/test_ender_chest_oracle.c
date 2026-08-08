#include "game/runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint64_t double_bits(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static void tick(GmRuntime *runtime, GmAction action, int count)
{
    while (count-- > 0) gm_runtime_tick(runtime, action);
}

int main(int argc, char **argv)
{
    GmConfig config;
    GmRuntime runtime;
    GmRuntimeEnderChest tile;
    GmAction idle;
    ICStack slot;
    char error[256];
    uint64_t world_seed = UINT64_C(0x23456789abcd);
    uint64_t display_seed = UINT64_C(0x13579bdf2468);
    int open_ticks = 10, close_ticks = 6;
    int x = 8, y = 220, z = 8;
    int blocked_result, blocked_open, blocked_viewers;
    int open_result, open_container, open_viewers;
    uint32_t open_lid, open_prev_lid;
    int open_sync, close_viewers;
    if (argc != 1 && argc != 5) return 2;
    if (argc == 5) {
        open_ticks = atoi(argv[1]);
        close_ticks = atoi(argv[2]);
        world_seed = strtoull(argv[3], NULL, 10);
        display_seed = strtoull(argv[4], NULL, 10);
    }
    if (open_ticks < 0 || open_ticks > 40
            || close_ticks < 1 || close_ticks > 40
            || world_seed >= (UINT64_C(1) << 48)
            || display_seed >= (UINT64_C(1) << 48))
        return 2;
    gm_config_defaults(&config);
    config.seed = 42;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.render = GM_RENDER_OFF;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    memset(&idle, 0, sizeof idle);
    runtime.randtick_enabled = 0;
    if (!gm_runtime_set_world_random_seed48(&runtime, world_seed)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    gm_runtime_set_pose(
        &runtime, x + 0.5, y, z - 2.0, 180.0F, 0.0F);
    if (!gm_runtime_set_block(&runtime, x, y, z, 130, 2)
            || !gm_runtime_set_block(&runtime, x, y + 1, z, 1, 0)
            || !gm_runtime_ender_chest_set_slot(
                &runtime, 0, ic_mk(264, 5, 0))) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    blocked_result = gm_runtime_use_block(&runtime, x, y, z);
    blocked_open = runtime.container != 0;
    if (!gm_runtime_ender_chest_tile_get(
            &runtime, 0, x, y, z, &tile)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    blocked_viewers = tile.num_players_using;
    if (!gm_runtime_set_block(&runtime, x, y + 1, z, 0, 0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    open_result = gm_runtime_use_block(&runtime, x, y, z);
    open_container = runtime.container == 3
        && runtime.active_chest == GM_ACTIVE_ENDER_CHEST;
    if (!gm_runtime_ender_chest_tile_get(
            &runtime, 0, x, y, z, &tile)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    open_viewers = tile.num_players_using;
    tick(&runtime, idle, open_ticks);
    slot = gm_runtime_ender_chest_get_slot(&runtime, 0);
    if (!gm_runtime_ender_chest_tile_get(
            &runtime, 0, x, y, z, &tile)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    open_lid = float_bits(tile.lid_angle);
    open_prev_lid = float_bits(tile.prev_lid_angle);
    open_sync = tile.ticks_since_sync;

    {
        GmAction close = idle;
        close.close_container = 1;
        gm_runtime_tick(&runtime, close);
        tick(&runtime, idle, close_ticks - 1);
    }
    if (!gm_runtime_ender_chest_tile_get(
            &runtime, 0, x, y, z, &tile)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    close_viewers = tile.num_players_using;
    printf("{\"ok\":true,\"blocked_result\":%s,"
           "\"blocked_open\":%s,\"blocked_viewers\":%d,"
           "\"open_result\":%s,\"open_container\":%s,"
           "\"open_viewers\":%d,\"slot_item\":%d,"
           "\"slot_count\":%d,\"slot_meta\":%d,"
           "\"open_ticks\":%d,\"open_lid_bits\":\"%08" PRIx32 "\","
           "\"open_prev_lid_bits\":\"%08" PRIx32 "\","
           "\"open_sync_ticks\":%d,\"close_viewers\":%d,"
           "\"close_ticks\":%d,\"close_lid_bits\":\"%08" PRIx32 "\","
           "\"close_prev_lid_bits\":\"%08" PRIx32 "\","
           "\"close_sync_ticks\":%d,\"sounds\":[",
           blocked_result ? "true" : "false",
           blocked_open ? "true" : "false", blocked_viewers,
           open_result ? "true" : "false",
           open_container ? "true" : "false", open_viewers,
           slot.item, slot.count, slot.meta, open_ticks,
           open_lid, open_prev_lid, open_sync, close_viewers,
           close_ticks, float_bits(tile.lid_angle),
           float_bits(tile.prev_lid_angle), tile.ticks_since_sync);
    for (int index = 0;
            index < gm_runtime_sound_event_count(&runtime); ++index) {
        GmRuntimeSoundEvent event;
        const char *name;
        if (!gm_runtime_sound_event_get(&runtime, index, &event)) continue;
        if (event.sound == GM_SOUND_ENDER_CHEST_OPEN)
            name = "minecraft:block.enderchest.open";
        else if (event.sound == GM_SOUND_ENDER_CHEST_CLOSE)
            name = "minecraft:block.enderchest.close";
        else
            continue;
        if (index) putchar(',');
        printf("{\"seq\":%d,\"sound\":\"%s\","
               "\"category\":\"block\","
               "\"x_bits\":\"%016" PRIx64 "\","
               "\"y_bits\":\"%016" PRIx64 "\","
               "\"z_bits\":\"%016" PRIx64 "\","
               "\"volume_bits\":\"%08" PRIx32 "\","
               "\"pitch_bits\":\"%08" PRIx32 "\"}",
               index, name, double_bits(event.x - x),
               double_bits(event.y - y), double_bits(event.z - z),
               float_bits(event.volume), float_bits(event.pitch));
    }
    printf("],\"world_seed48\":%" PRIu64 ",\"display_particles\":[",
           runtime.world_random_seed48);
    if (gm_runtime_ender_chest_random_display_tick(
            &runtime, x, y, z, &display_seed) != 3) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    for (int index = 0;
            index < gm_runtime_particle_event_count(&runtime); ++index) {
        GmRuntimeParticleEvent event;
        if (!gm_runtime_particle_event_get(&runtime, index, &event)
                || event.kind != GM_PARTICLE_PORTAL)
            continue;
        if (index) putchar(',');
        printf("{\"seq\":%d,\"id\":24,\"ignore_range\":false,"
               "\"parameters\":[],\"payload_bits\":["
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\"]}",
               index,
               double_bits(event.x - x), double_bits(event.y - y),
               double_bits(event.z - z), double_bits(event.motion_x),
               double_bits(event.motion_y), double_bits(event.motion_z));
    }
    printf("],\"display_seed48\":%" PRIu64 "}\n", display_seed);
    gm_runtime_destroy(&runtime);
    return 0;
}
