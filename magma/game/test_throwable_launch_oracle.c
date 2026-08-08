#include "game/runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

int main(int argc, char **argv) {
    GmConfig config;
    GmRuntime runtime;
    GmRuntimeProjectile *projectile = NULL;
    ICStack stack;
    char error[256];
    uint64_t entity_seed, uuid_seed;
    double x, y, z, vx, vy, vz;
    float yaw, pitch;
    int next_id, on_ground, item, meta, ticks = 0;
    if (argc != 15 && argc != 16) return 2;
    entity_seed = strtoull(argv[1], NULL, 10);
    uuid_seed = strtoull(argv[2], NULL, 10);
    next_id = atoi(argv[3]);
    x = strtod(argv[4], NULL);
    y = strtod(argv[5], NULL);
    z = strtod(argv[6], NULL);
    vx = strtod(argv[7], NULL);
    vy = strtod(argv[8], NULL);
    vz = strtod(argv[9], NULL);
    yaw = strtof(argv[10], NULL);
    pitch = strtof(argv[11], NULL);
    on_ground = atoi(argv[12]);
    item = atoi(argv[13]);
    meta = atoi(argv[14]);
    if (argc == 16) ticks = atoi(argv[15]);
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    gm_runtime_set_pose_state(
        &runtime, x, y, z, yaw, pitch,
        vx, vy, vz, on_ground, 0.0F);
    gm_runtime_set_entity_seed_generator_seed48(&runtime, entity_seed);
    gm_runtime_set_server_uuid_random_seed48(&runtime, uuid_seed);
    gm_runtime_set_entity_id_cursor(&runtime, next_id);
    stack = ic_mk(item, 5, meta);
    isr_set_stack(&runtime.player.inv, 0, stack);
    runtime.player.inv.current_item = 0;
    if (!gm_runtime_throw_player_item_now(&runtime, item, meta)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (runtime.projectiles[i].active
                && runtime.projectiles[i].eid == next_id) {
            projectile = &runtime.projectiles[i];
            break;
        }
    if (!projectile) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    if (ticks > 0) {
        int slot = (int)(projectile - runtime.projectiles);
        gm_runtime_set_pose_state(
            &runtime, 1000000.0, 220.0, 1000000.0,
            0.0F, 0.0F, 0.0, 0.0, 0.0, 1, 0.0F);
        for (int tick = 0; tick < ticks && projectile->active; ++tick)
            if (!gm_runtime_tick_projectile_now(&runtime, slot)) {
                gm_runtime_destroy(&runtime);
                return 1;
            }
    }
    printf("{\"ok\":true,\"eid\":%d,\"position_bits\":["
           "\"%016" PRIx64 "\",\"%016" PRIx64 "\","
           "\"%016" PRIx64 "\"],\"motion_bits\":["
           "\"%016" PRIx64 "\",\"%016" PRIx64 "\","
           "\"%016" PRIx64 "\"],\"rotation_bits\":["
           "\"%08" PRIx32 "\",\"%08" PRIx32 "\","
           "\"%08" PRIx32 "\",\"%08" PRIx32 "\"],"
           "\"seed48\":%" PRIu64 ",\"have_gaussian\":%s,"
           "\"next_gaussian_bits\":\"%016" PRIx64 "\","
           "\"uuid_most\":%" PRId64 ",\"uuid_least\":%" PRId64 ","
           "\"entity_seed48\":%" PRIu64 ","
           "\"server_uuid_seed48\":%" PRIu64 ","
           "\"next_entity_id\":%d",
           projectile->eid,
           double_bits(projectile->x), double_bits(projectile->y),
           double_bits(projectile->z), double_bits(projectile->vx),
           double_bits(projectile->vy), double_bits(projectile->vz),
           float_bits(projectile->yaw), float_bits(projectile->pitch),
           float_bits(projectile->prev_yaw),
           float_bits(projectile->prev_pitch),
           projectile->random_seed48,
           projectile->random_have_gaussian ? "true" : "false",
           double_bits(projectile->random_next_gaussian),
           (int64_t)projectile->uuid_most,
           (int64_t)projectile->uuid_least,
           runtime.entity_seed_generator_seed48,
           runtime.server_uuid_random_seed48,
           runtime.next_entity_id);
    if (argc == 16)
        printf(",\"ticks_existed\":%d,\"ticks_in_air\":%d,"
               "\"ignore_player\":%s,\"ignore_player_time\":%d,"
               "\"portal_cooldown\":%d,\"dead\":%s",
               projectile->age, projectile->throwable_ticks_in_air,
               projectile->ignore_player ? "true" : "false",
               projectile->ignore_player_time,
               projectile->portal_cooldown,
               projectile->active ? "false" : "true");
    puts("}");
    gm_runtime_destroy(&runtime);
    return 0;
}
